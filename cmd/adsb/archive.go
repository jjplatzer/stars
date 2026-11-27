package main

import (
	"archive/tar"
	"bufio"
	"context"
	"crypto/sha256"
	"errors"
	"fmt"
	"io"
	"net/http"
	"path"
	"regexp"
	"sort"
	"strings"
)

const bufferSize = 256 * 1024

var traceName = regexp.MustCompile(
	`^trace_full_([0-9a-f]{6})\.json(\.gz)?$`,
)

type entry struct {
	ICAO   string `json:"icao"`
	Offset int64  `json:"offset"`
	Size   int64  `json:"size"`
}

type sample struct {
	Entry entry
	Hash  string
}

type span struct {
	Part        part
	First, Last int64
}

// Convert a global TAR offset to one or more part-relative
// inclusive HTTP byte ranges.
func spans(parts []part, offset, size int64) ([]span, error) {
	if offset < 0 || size <= 0 || offset+size < offset {
		return nil, errors.New("invalid archive range")
	}

	end := offset + size
	var result []span
	var covered int64

	for _, p := range parts {
		first := max(offset, p.Start)
		last := min(end, p.Start+p.Size)

		if first >= last {
			continue
		}

		result = append(result, span{
			Part:  p,
			First: first - p.Start,
			Last:  last - p.Start - 1,
		})

		covered += last - first
	}

	if covered != size {
		return nil, errors.New("range outside archive")
	}

	return result, nil
}

// Never accept HTTP 200 as a successful Range response.
// This prevents accidental multi-gigabyte downloads by clients.
func readRange(ctx context.Context, s span, w io.Writer) error {
	if s.First < 0 || s.Last < s.First || s.Last >= s.Part.Size {
		return errors.New("invalid part-relative range")
	}

	header := fmt.Sprintf("bytes=%d-%d", s.First, s.Last)

	resp, err := get(ctx, s.Part.URL, header)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusPartialContent {
		return fmt.Errorf("expected HTTP 206, got %s", resp.Status)
	}

	expected := fmt.Sprintf("bytes %d-%d/%d",
		s.First, s.Last, s.Part.Size)

	if resp.Header.Get("Content-Range") != expected {
		return fmt.Errorf("unexpected Content-Range: %s",
			resp.Header.Get("Content-Range"))
	}

	size := s.Last - s.First + 1

	n, err := io.Copy(w, io.LimitReader(resp.Body, size+1))
	if err != nil {
		return err
	}
	if n != size {
		return fmt.Errorf("range length: got %d, expected %d", n, size)
	}

	return nil
}

// Present consecutive HTTP assets as one continuous TAR stream.
// Only one asset connection is open at any time.
func stream(ctx context.Context, parts []part) *io.PipeReader {
	pr, pw := io.Pipe()

	go func() {
		err := func() error {
			buffer := make([]byte, bufferSize)

			for _, p := range parts {
				resp, err := get(ctx, p.URL, "")
				if err != nil {
					return err
				}

				if resp.StatusCode != http.StatusOK {
					resp.Body.Close()
					return fmt.Errorf("download %s: %s",
						p.Name, resp.Status)
				}

				hash := sha256.New()

				n, err := io.CopyBuffer(
					io.MultiWriter(pw, hash),
					io.LimitReader(resp.Body, p.Size+1),
					buffer,
				)

				resp.Body.Close()

				if err != nil {
					return err
				}
				if n != p.Size {
					return fmt.Errorf("%s: incorrect asset size", p.Name)
				}

				digest := fmt.Sprintf("sha256:%x", hash.Sum(nil))

				if p.Digest != "" && p.Digest != digest {
					return fmt.Errorf("%s: asset checksum mismatch", p.Name)
				}
			}

			return nil
		}()

		pw.CloseWithError(err)
	}()

	return pr
}

// Count bytes delivered to archive/tar, not bytes prefetched
// by bufio.Reader. This distinction is essential for offsets.
type counter struct {
	r io.Reader
	n int64
}

func (c *counter) Read(b []byte) (int, error) {
	n, err := c.r.Read(b)
	c.n += int64(n)
	return n, err
}

// TAR may contain zero padding after its end marker.
// Reject unexpected trailing data.
type zeroWriter struct{}

func (zeroWriter) Write(b []byte) (int, error) {
	for i, v := range b {
		if v != 0 {
			return i, errors.New("nonzero data after TAR end")
		}
	}
	return len(b), nil
}

func scan(r io.Reader, parts []part) ([]entry, []sample, error) {
	if len(parts) == 0 {
		return nil, nil, errors.New("empty archive")
	}

	last := parts[len(parts)-1]
	total := last.Start + last.Size

	c := &counter{r: bufio.NewReaderSize(r, bufferSize)}
	tr := tar.NewReader(c)

	var entries []entry
	var samples []sample

	for {
		h, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, nil, err
		}

		// Only index full daily aircraft traces.
		// Heatmaps and recent-trace files are ignored.
		if !strings.Contains("/"+h.Name, "/traces/") {
			continue
		}

		match := traceName.FindStringSubmatch(
			strings.ToLower(path.Base(h.Name)),
		)
		if match == nil {
			continue
		}

		if h.Typeflag != tar.TypeReg && h.Typeflag != tar.TypeRegA {
			return nil, nil, fmt.Errorf("unsupported trace: %s", h.Name)
		}

		for key := range h.PAXRecords {
			if strings.Contains(key, "sparse") ||
				key == "SCHILY.realsize" {
				return nil, nil, errors.New("sparse traces unsupported")
			}
		}

		// Next has consumed the TAR header, including any
		// extended headers. The counter points to the payload.
		e := entry{
			ICAO:   match[1],
			Offset: c.n,
			Size:   h.Size,
		}

		if e.Size < 2 || e.Offset > total ||
			e.Size > total-e.Offset {
			return nil, nil, fmt.Errorf("invalid trace size: %s", h.Name)
		}

		// Validate encoding without decompressing the trace.
		var magic [2]byte
		if _, err := io.ReadFull(tr, magic[:]); err != nil {
			return nil, nil, err
		}

		if magic != [2]byte{0x1f, 0x8b} {
			return nil, nil, fmt.Errorf("not a gzip trace: %s", h.Name)
		}

		// Retain checksums for the first aircraft and every
		// aircraft spanning an archive-part boundary.
		check := len(entries) == 0

		for _, p := range parts[1:] {
			if e.Offset < p.Start && p.Start < e.Offset+e.Size {
				check = true
				break
			}
		}

		if check {
			hash := sha256.New()
			hash.Write(magic[:])

			n, err := io.Copy(hash, tr)
			if err != nil {
				return nil, nil, err
			}
			if n != e.Size-2 {
				return nil, nil, errors.New("truncated trace")
			}

			samples = append(samples, sample{
				Entry: e,
				Hash:  fmt.Sprintf("%x", hash.Sum(nil)),
			})
		}

		// No decompressed aircraft positions are stored.
		// Next discards unread payload bytes and TAR padding.
		entries = append(entries, e)

		if len(entries) > 1_000_000 {
			return nil, nil, errors.New("index exceeds safety limit")
		}
	}

	// Drain the stream so asset length and SHA-256 checks finish.
	if _, err := io.Copy(zeroWriter{}, c); err != nil {
		return nil, nil, err
	}

	if c.n != total {
		return nil, nil, errors.New("incomplete TAR stream")
	}

	if len(entries) == 0 {
		return nil, nil, errors.New("no aircraft traces found")
	}

	// Sorted ICAO addresses permit client-side binary search.
	sort.Slice(entries, func(i, j int) bool {
		return entries[i].ICAO < entries[j].ICAO
	})

	for i := 1; i < len(entries); i++ {
		if entries[i-1].ICAO == entries[i].ICAO {
			return nil, nil, fmt.Errorf(
				"duplicate daily trace: %s", entries[i].ICAO)
		}
	}

	return entries, samples, nil
}

// Retrieve selected members directly from the upstream assets
// and compare them against their original streamed payloads.
func verify(ctx context.Context, parts []part, samples []sample) error {
	for _, s := range samples {
		ranges, err := spans(parts, s.Entry.Offset, s.Entry.Size)
		if err != nil {
			return err
		}

		hash := sha256.New()

		for _, r := range ranges {
			if err := readRange(ctx, r, hash); err != nil {
				return err
			}
		}

		if fmt.Sprintf("%x", hash.Sum(nil)) != s.Hash {
			return fmt.Errorf("trace checksum mismatch: %s",
				s.Entry.ICAO)
		}
	}

	return nil
}
