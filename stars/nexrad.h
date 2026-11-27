#pragma once

#include <QByteArray>
#include <QRectF>
#include <array>

#include "../renderer/builders.h"

namespace stars {

// Immutable per-observation geometry in (longitude, latitude) degrees.
struct NexradFrame {
  struct Level {
    std::shared_ptr<const renderer::CmdBuffer> fill, stipple, faaStipple;
  };
  std::array<Level, 6> levels;
  qint64 observed{};
  QRectF coverage;
};

QByteArray inflateWeatherGzip(const QByteArray& gzip);
void buildWeatherLevels(
    NexradFrame& frame,
    const std::array<renderer::TrianglesBuilder, 6>& meshes);
std::shared_ptr<const NexradFrame> decodeNexrad(const QByteArray& gzip,
                                                QRectF coverage);
// Index zero is the current observation, followed by its two predecessors.
using WeatherPictures = std::array<std::shared_ptr<const NexradFrame>, 3>;
}  // namespace stars
