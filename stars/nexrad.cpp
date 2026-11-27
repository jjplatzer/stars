#include "nexrad.h"

#include <zlib.h>

#include <QDateTime>
#include <QImage>
#include <QTimeZone>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "../renderer/builders.h"

namespace stars {
namespace {
constexpr int maxBytes = 32 * 1024 * 1024;

void check(bool valid, const char* reason) {
  if (!valid) throw std::runtime_error(reason);
}

}  // namespace
QByteArray inflateWeatherGzip(const QByteArray& gzip) {
  check(!gzip.isEmpty() && gzip.size() <= maxBytes,
        "Invalid weather download size");
  z_stream stream{};
  check(inflateInit2(&stream, MAX_WBITS + 16) == Z_OK,
        "Cannot initialize gzip");
  struct End {
    z_stream* stream;
    ~End() { inflateEnd(stream); }
  } end{&stream};
  stream.next_in =
      reinterpret_cast<Bytef*>(const_cast<char*>(gzip.constData()));
  stream.avail_in = gzip.size();
  QByteArray out;
  std::array<char, 65536> block;
  int status;
  do {
    stream.next_out = reinterpret_cast<Bytef*>(block.data());
    stream.avail_out = block.size();
    status = inflate(&stream, Z_NO_FLUSH);
    check(status == Z_OK || status == Z_STREAM_END,
          "Invalid weather gzip stream");
    out.append(block.data(), block.size() - stream.avail_out);
    check(out.size() <= maxBytes,
          "Inflated weather product exceeds size limit");
  } while (status != Z_STREAM_END);
  return out;
}

namespace {
int signedMagnitude(quint32 n, quint32 sign) {
  return n & sign ? -int(n & (sign - 1)) : int(n);
}

// Merge adjacent equal-level cells horizontally, then coalesce identical runs
// across rows. Each rectangle contributes four vertices even for large echoes.
struct Run {
  int first, end, level, top;
};
struct Grid {
  QImage image;
  double west, north, dx, dy;
  bool reversedX, reversedY;
  std::array<quint8, 65536> level{};
};

void buildLevels(NexradFrame& frame, const Grid& grid) {
  const auto& area = frame.coverage;
  const int w = grid.image.width(), h = grid.image.height();
  const double west =
      grid.west +
      360 *
          std::round((area.center().x() - (grid.west + w * grid.dx / 2)) / 360);
  auto column = [&](double lon) {
    return std::clamp((lon - west) / grid.dx, 0., double(w));
  };
  auto row = [&](double lat) {
    return std::clamp((grid.north - lat) / grid.dy, 0., double(h));
  };
  const int x0 = std::floor(column(area.left())),
            x1 = std::ceil(column(area.right()));
  const int y0 = std::floor(row(area.bottom())),
            y1 = std::ceil(row(area.top()));
  std::array<renderer::TrianglesBuilder, 6> meshes;
  auto emitRun = [&](Run r, int endRow) {
    const float left = west + r.first * grid.dx, right = west + r.end * grid.dx;
    const float top = grid.north - r.top * grid.dy,
                bottom = grid.north - endRow * grid.dy;
    meshes[r.level - 1].addQuad({left, top}, {right, top}, {right, bottom},
                                {left, bottom});
  };
  std::vector<Run> previous, next;
  for (int y = y0; y < y1; ++y) {
    const auto* line = grid.image.constScanLine(grid.reversedY ? h - 1 - y : y);
    const auto* words = reinterpret_cast<const quint16*>(line);
    auto levelAt = [&](int x) {
      const int i = grid.reversedX ? w - 1 - x : x;
      return grid.level[grid.image.depth() == 16 ? words[i] : line[i]];
    };
    size_t p = 0;
    for (int x = x0; x < x1;) {
      const int first = x, level = levelAt(x++);
      while (x < x1 && levelAt(x) == level) ++x;
      if (!level) continue;
      while (p < previous.size() && previous[p].first < first)
        emitRun(previous[p++], y);
      Run run{first, x, level, y};
      if (p < previous.size() && previous[p].first == first) {
        const auto old = previous[p++];
        if (old.end == x && old.level == level)
          run.top = old.top;
        else
          emitRun(old, y);
      }
      next.push_back(run);
    }
    while (p < previous.size()) emitRun(previous[p++], y);
    previous.swap(next);
    next.clear();
  }
  for (auto r : previous) emitRun(r, y1);
  buildWeatherLevels(frame, meshes);
}
}  // namespace

void buildWeatherLevels(
    NexradFrame& frame,
    const std::array<renderer::TrianglesBuilder, 6>& meshes) {
  using renderer::DrawMode;
  constexpr std::array patterns{DrawMode::Solid,        DrawMode::StippleLight,
                                DrawMode::StippleDense, DrawMode::Solid,
                                DrawMode::StippleLight, DrawMode::StippleDense};
  for (size_t i = 0; i < meshes.size(); ++i) {
    if (meshes[i].indices.empty()) continue;
    auto commands = [&](DrawMode mode) {
      auto cb = std::make_shared<renderer::CmdBuffer>();
      meshes[i].generateCommands(*cb, mode);
      return cb;
    };
    frame.levels[i].fill = commands(DrawMode::Solid);
    auto& level = frame.levels[i];
    if (patterns[i] != DrawMode::Solid) level.stipple = commands(patterns[i]);
    // REDS uses the sparse legacy mask on even levels for FAA-HF-STD-010A.
    if (i % 2 == 1)
      level.faaStipple = patterns[i] == DrawMode::StippleLight
                             ? level.stipple
                             : commands(DrawMode::StippleLight);
  }
}

std::shared_ptr<const NexradFrame> decodeNexrad(const QByteArray& gzip,
                                                QRectF coverage) {
  const auto grib = inflateWeatherGzip(gzip);
  auto u32 = [](const char* p) { return qFromBigEndian<quint32>(p); };
  auto u16 = [](const char* p) { return qFromBigEndian<quint16>(p); };
  check(
      grib.size() >= 20 && grib.startsWith("GRIB") && grib[7] == 2 &&
          grib.endsWith("7777") &&
          qFromBigEndian<quint64>(grib.constData() + 8) == quint64(grib.size()),
      "Invalid MRMS GRIB2 message");
  std::array<QByteArrayView, 8> sections{};
  const int end = grib.size() - 4;
  for (int offset = 16; offset < end;) {
    check(end - offset >= 5, "Truncated GRIB section header");
    const char* p = grib.constData() + offset;
    const auto length = u32(p);
    const unsigned kind = quint8(p[4]);
    check(length >= 5 && length <= unsigned(end - offset) && kind > 0 &&
              kind < sections.size() && sections[kind].empty(),
          "Invalid GRIB section");
    sections[kind] = QByteArrayView(p, length);
    offset += length;
  }
  auto section = [&](int id, int minimum) {
    check(sections[id].size() >= minimum, "Missing or truncated MRMS section");
    return sections[id].data();
  };
  const char *time = section(1, 21), *coords = section(3, 72),
             *packing = section(5, 21);
  check(u16(coords + 12) == 0 && (coords[71] & 0x30) == 0 &&
            u16(packing + 9) == 41 && quint8(section(6, 6)[5]) == 255,
        "Unsupported MRMS grid or packing");
  const auto w = u32(coords + 30), h = u32(coords + 34);
  const char* png = section(7, 34) + 5;
  check(w && h && quint64(w) * h <= 50000000 && u32(png + 16) == w &&
            u32(png + 20) == h && png[25] == 0 &&
            (png[24] == 8 || png[24] == 16) && packing[19] > 0 &&
            packing[19] <= png[24],
        "Invalid MRMS grayscale grid");
  Grid grid;
  grid.image = QImage::fromData(reinterpret_cast<const uchar*>(png),
                                sections[7].size() - 5, "PNG");
  check(grid.image.format() == QImage::Format_Grayscale16 ||
            grid.image.format() == QImage::Format_Grayscale8,
        "Cannot decode MRMS grayscale samples");
  grid.reversedX = (coords[71] & 0x80) != 0;
  grid.reversedY = (coords[71] & 0x40) != 0;
  grid.dx = u32(coords + 63) * 1e-6;
  grid.dy = u32(coords + 67) * 1e-6;
  check(grid.dx > 0 && grid.dy > 0, "Invalid MRMS grid spacing");
  auto degrees = [&](const char* p) {
    return signedMagnitude(u32(p), 0x80000000u) * 1e-6;
  };
  grid.west = degrees(coords + 50) - (grid.reversedX ? w - .5 : .5) * grid.dx;
  grid.north = degrees(coords + 46) + (grid.reversedY ? h - .5 : .5) * grid.dy;
  const double reference = std::bit_cast<float>(u32(packing + 11));
  const double step =
      std::ldexp(1., signedMagnitude(u16(packing + 15), 0x8000));
  const double scale =
      std::pow(10., -signedMagnitude(u16(packing + 17), 0x8000));
  check(std::isfinite(reference) && std::isfinite(step) && std::isfinite(scale),
        "Invalid MRMS reflectivity scale");
  constexpr std::array thresholds{18, 30, 41, 46, 50, 57};
  for (int raw = 0; raw < (1 << packing[19]); ++raw) {
    const auto dbz = std::ceil(float((reference + raw * step) * scale));
    grid.level[raw] =
        std::upper_bound(thresholds.begin(), thresholds.end(), dbz) -
        thresholds.begin();
  }
  auto frame = std::make_shared<NexradFrame>();
  const QDateTime utc(QDate(u16(time + 12), time[14], time[15]),
                      QTime(time[16], time[17], time[18]), QTimeZone::utc());
  check(utc.isValid(), "Invalid MRMS observation time");
  frame->observed = utc.toSecsSinceEpoch();
  frame->coverage = coverage;
  buildLevels(*frame, grid);
  return frame;
}
}  // namespace stars
