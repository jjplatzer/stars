package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestSplitArchive(t *testing.T) {
	var payload bytes.Buffer

	gz := gzip.NewWriter(&payload)
	if _, err := gz.Write([]byte(`{"trace":[]}`)); err != nil {
		t.Fatal(err)
	}
	if err := gz.Close(); err != nil {
		t.Fatal(err)
	}

	var archive bytes.Buffer
	tw := tar.NewWriter(&archive)

	for _, name := range []string{
		"2026/11/26/traces/ef/trace_full_abcdef.json",
		"2026/11/26/traces/34/trace_full_123434.json",
	} {
		h := &tar.Header{
			Name:     name,
			Mode:     0644,
			Typeflag: tar.TypeReg,
			Size:     int64(payload.Len()),
		}

		if err := tw.WriteHeader(h); err != nil {
			t.Fatal(err)
		}

		if _, err := tw.Write(payload.Bytes()); err != nil {
			t.Fatal(err)
		}
	}

	if err := tw.Close(); err != nil {
		t.Fatal(err)
	}

	data := archive.Bytes()

	// Split inside the first compressed aircraft payload.
	boundary := 512 + 3

	parts := []part{
		{Start: 0, Size: int64(boundary)},
		{
			Start: int64(boundary),
			Size:  int64(len(data) - boundary),
		},
	}

	r := io.MultiReader(
		bytes.NewReader(data[:boundary]),
		bytes.NewReader(data[boundary:]),
	)

	entries, samples, err := scan(r, parts)
	if err != nil {
		t.Fatal(err)
	}

	if len(entries) != 2 || len(samples) == 0 {
		t.Fatal("unexpected index contents")
	}

	for _, e := range entries {
		got := data[e.Offset : e.Offset+e.Size]

		if !bytes.Equal(got, payload.Bytes()) {
			t.Fatalf("incorrect payload offset: %s", e.ICAO)
		}
	}

	for _, e := range entries {
		if e.ICAO != "abcdef" {
			continue
		}

		ranges, err := spans(parts, e.Offset, e.Size)
		if err != nil {
			t.Fatal(err)
		}

		if len(ranges) != 2 || ranges[1].First != 0 {
			t.Fatal("incorrect cross-part ranges")
		}
	}
}

func TestRangeBoundary(t *testing.T) {
	parts := []part{
		{Start: 0, Size: 100},
		{Start: 100, Size: 200},
	}

	ranges, err := spans(parts, 100, 10)
	if err != nil {
		t.Fatal(err)
	}

	if len(ranges) != 1 ||
		ranges[0].First != 0 ||
		ranges[0].Last != 9 {
		t.Fatal("incorrect exact-boundary range")
	}

	if _, err := spans(parts, 295, 10); err == nil {
		t.Fatal("accepted an out-of-bounds range")
	}
}

func TestHTTPRange(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			if r.Header.Get("Range") != "bytes=2-4" {
				t.Error("incorrect Range header")
			}

			w.Header().Set("Content-Range", "bytes 2-4/6")
			w.WriteHeader(http.StatusPartialContent)
			w.Write([]byte("cde"))
		},
	))
	defer server.Close()

	var out bytes.Buffer

	err := readRange(context.Background(), span{
		Part:  part{URL: server.URL, Size: 6},
		First: 2,
		Last:  4,
	}, &out)

	if err != nil || out.String() != "cde" {
		t.Fatalf("range retrieval failed: %v", err)
	}
}

func TestRejectFullResponse(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(
		func(w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusOK)
			w.Write([]byte("abcdef"))
		},
	))
	defer server.Close()

	err := readRange(context.Background(), span{
		Part:  part{URL: server.URL, Size: 6},
		First: 0,
		Last:  0,
	}, io.Discard)

	if err == nil {
		t.Fatal("accepted HTTP 200 instead of HTTP 206")
	}
}
