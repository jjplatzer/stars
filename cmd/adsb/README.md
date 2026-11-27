# ADS-B archive index

Builds a compact byte-offset index of ADSB.lol daily archives.

The command streams upstream TAR parts sequentially. It does not
extract or decompress aircraft trajectories and does not store
the worldwide archive locally.

## Run

    go run ./cmd/adsb -date 2026-11-23 -out index.json
    zstd -3 index.json -o index.json.zst

Exit code 3 indicates that the preferred upstream archive is not
available yet. Other nonzero exit codes indicate a failure.

## Index format v1

The JSON document contains:

- version: index schema version.
- attribution and license: upstream data attribution.
- trace_encoding: gzip.
- source: archive date, repository, tag, release ID and asset metadata.
- source.parts: ordered archive parts, including global start offsets,
  asset IDs, sizes, URLs and upstream checksums when available.
- entries: aircraft records sorted lexicographically by ICAO address.

Each entry contains:

    {"icao":"abcdef","offset":512,"size":12345}

Offsets are global TAR payload offsets, not header offsets or
part-relative offsets. Sizes are compressed payload lengths.

For an entry with global interval [offset, offset + size), intersect
it with each part's interval [start, start + size). Convert the
intersection to a part-relative inclusive HTTP Range request.

Concatenate the returned bytes in archive-part order, then
decompress the resulting gzip stream.

Always require HTTP 206 and an exact Content-Range. Never silently
fall back to downloading the complete archive.

The client should cache each trajectory by source release, UTC date
and aircraft address. Multiple flight legs share the same daily
aircraft trace.

## GitHub Actions

The workflow supports manual execution and scheduled discovery of
missing indexes within the preceding seven UTC days.

It processes at most one daily archive per invocation.

Scheduled processing is enabled by setting the repository variable
ADSB_INDEX_ENABLED to true after validating a manual run.

Indexes are published as prerelease assets:

    adsb-index-YYYY-MM-DD/index.json.zst

Previously published indexes are not overwritten or automatically
deleted. They contain no private TAIS or ITWS data.

## Limitations

The index supports six-digit ICAO addresses and full daily gzip
traces. Non-ICAO identifiers, heatmaps and recent traces are ignored.

The indexer does not perform TAIS association or position fusion.

Source archives are provided by ADSB.lol under ODbL 1.0:
https://www.adsb.lol/docs/open-data/historical/

Derived indexes retain source attribution and license information.
