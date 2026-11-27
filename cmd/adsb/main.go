package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

const (
	maxArchiveBytes  int64 = 32 << 30
	maxMetadataBytes int64 = 16 << 20
)

var (
	errNotReady = errors.New("upstream archive not available")
	client      = &http.Client{Timeout: 20 * time.Minute}
)

type part struct {
	ID     int64  `json:"id"`
	Name   string `json:"name"`
	URL    string `json:"browser_download_url"`
	Size   int64  `json:"size"`
	Start  int64  `json:"start"`
	Digest string `json:"digest,omitempty"`
	State  string `json:"state,omitempty"`
}

type source struct {
	Date       string `json:"date"`
	Repository string `json:"repository"`
	Tag        string `json:"tag"`
	ReleaseID  int64  `json:"release_id"`
	Parts      []part `json:"parts"`
}

type index struct {
	Version       int     `json:"version"`
	Attribution   string  `json:"attribution"`
	License       string  `json:"license"`
	TraceEncoding string  `json:"trace_encoding"`
	Source        source  `json:"source"`
	Entries       []entry `json:"entries"`
}

func get(ctx context.Context, address, byteRange string) (*http.Response, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return nil, err
	}

	// Byte offsets must refer to the original TAR representation.
	req.Header.Set("Accept-Encoding", "identity")
	req.Header.Set("User-Agent", "stars-adsb-index/1")

	if byteRange != "" {
		req.Header.Set("Range", byteRange)
	}

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}

	encoding := resp.Header.Get("Content-Encoding")
	if encoding != "" && encoding != "identity" {
		resp.Body.Close()
		return nil, fmt.Errorf("unexpected HTTP encoding: %s", encoding)
	}

	return resp, nil
}

// Discover the release preferred by the upstream publisher.
// Do not assume prod-0 is always the most complete archive.
func discover(ctx context.Context, day time.Time) (source, error) {
	repo := fmt.Sprintf("adsblol/globe_history_%d", day.Year())
	base := "https://github.com/" + repo + "/releases/download/"
	prefix := base + "v" + day.Format("2006.01.02") + "-"

	preferred := "https://raw.githubusercontent.com/" +
		repo + "/main/PREFERRED_RELEASES.txt"

	resp, err := get(ctx, preferred, "")
	if err != nil {
		return source{}, err
	}
	defer resp.Body.Close()

	if resp.StatusCode == http.StatusNotFound {
		return source{}, errNotReady
	}
	if resp.StatusCode != http.StatusOK {
		return source{}, fmt.Errorf("preferred releases: %s", resp.Status)
	}

	var tag string

	scanner := bufio.NewScanner(io.LimitReader(resp.Body, maxMetadataBytes))
	for scanner.Scan() {
		first := strings.TrimSpace(
			strings.SplitN(scanner.Text(), ",", 2)[0],
		)

		if strings.HasPrefix(first, prefix) {
			tag = strings.SplitN(
				strings.TrimPrefix(first, base), "/", 2,
			)[0]
			break
		}
	}

	if err := scanner.Err(); err != nil {
		return source{}, err
	}
	if tag == "" {
		return source{}, errNotReady
	}

	// Obtain authoritative asset IDs, lengths and checksums.
	api := "https://api.github.com/repos/" + repo +
		"/releases/tags/" + url.PathEscape(tag)

	meta, err := get(ctx, api, "")
	if err != nil {
		return source{}, err
	}
	defer meta.Body.Close()

	if meta.StatusCode == http.StatusNotFound {
		return source{}, errNotReady
	}
	if meta.StatusCode != http.StatusOK {
		return source{}, fmt.Errorf("release metadata: %s", meta.Status)
	}

	var release struct {
		ID     int64  `json:"id"`
		Tag    string `json:"tag_name"`
		Draft  bool   `json:"draft"`
		Assets []part `json:"assets"`
	}

	if err := json.NewDecoder(
		io.LimitReader(meta.Body, maxMetadataBytes),
	).Decode(&release); err != nil {
		return source{}, err
	}

	if release.Draft || release.Tag != tag {
		return source{}, errNotReady
	}

	var parts []part
	for _, asset := range release.Assets {
		if asset.Name == tag+".tar" ||
			strings.HasPrefix(asset.Name, tag+".tar.") {
			parts = append(parts, asset)
		}
	}

	if len(parts) == 0 {
		return source{}, errNotReady
	}

	sort.Slice(parts, func(i, j int) bool {
		return parts[i].Name < parts[j].Name
	})

	var offset int64

	for i := range parts {
		p := &parts[i]

		// Support both a single TAR and contiguous split parts.
		if !(len(parts) == 1 && p.Name == tag+".tar") {
			if i >= 26*26 {
				return source{}, errors.New("too many TAR parts")
			}

			expected := fmt.Sprintf("%s.tar.%c%c",
				tag, 'a'+i/26, 'a'+i%26)

			if p.Name != expected {
				return source{}, fmt.Errorf(
					"missing or unexpected part: %s", p.Name)
			}
		}

		if p.State != "uploaded" || p.Size <= 0 {
			return source{}, errNotReady
		}

		// Only accept the expected upstream release URLs.
		if p.URL != base+tag+"/"+p.Name {
			return source{}, fmt.Errorf("unexpected asset URL: %s", p.URL)
		}

		if p.Size > maxArchiveBytes-offset {
			return source{}, errors.New("archive exceeds safety limit")
		}

		p.Start = offset
		offset += p.Size
	}

	return source{
		Date:       day.Format("2006-01-02"),
		Repository: repo,
		Tag:        tag,
		ReleaseID:  release.ID,
		Parts:      parts,
	}, nil
}

// Publish a local index only after all validation has succeeded.
func writeIndex(filename string, idx index) error {
	f, err := os.CreateTemp(filepath.Dir(filename), ".adsb-index-*")
	if err != nil {
		return err
	}

	defer os.Remove(f.Name())
	defer f.Close()

	if err := json.NewEncoder(f).Encode(idx); err != nil {
		return err
	}

	if err := f.Close(); err != nil {
		return err
	}

	return os.Rename(f.Name(), filename)
}

func run(date, output string) error {
	day, err := time.Parse("2006-01-02", date)
	if err != nil {
		return err
	}

	if !day.Before(time.Now().UTC().Truncate(24 * time.Hour)) {
		return errors.New("archive date must be a completed UTC day")
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	src, err := discover(ctx, day)
	if err != nil {
		return err
	}

	// Check Range support before transferring the full archive.
	for _, p := range src.Parts {
		if err := readRange(ctx, span{
			Part: p, First: 0, Last: 0,
		}, io.Discard); err != nil {
			return fmt.Errorf("range probe: %w", err)
		}

		log.Printf("Part %s: %d bytes", p.Name, p.Size)
	}

	reader := stream(ctx, src.Parts)
	defer reader.Close()

	entries, samples, err := scan(reader, src.Parts)
	if err != nil {
		return err
	}

	if err := verify(ctx, src.Parts, samples); err != nil {
		return err
	}

	idx := index{
		Version:       1,
		Attribution:   "ADSB.lol contributors — https://www.adsb.lol/",
		License:       "https://opendatacommons.org/licenses/odbl/1-0/",
		TraceEncoding: "gzip",
		Source:        src,
		Entries:       entries,
	}

	if err := writeIndex(output, idx); err != nil {
		return err
	}

	log.Printf("Indexed %d aircraft; verified %d traces",
		len(entries), len(samples))

	return nil
}

func main() {
	date := flag.String("date",
		time.Now().UTC().AddDate(0, 0, -1).Format("2006-01-02"),
		"UTC archive date")

	output := flag.String("out", "index.json", "output index")
	flag.Parse()

	if err := run(*date, *output); err != nil {
		if errors.Is(err, errNotReady) {
			log.Print(err)
			os.Exit(3)
		}
		log.Fatal(err)
	}
}
