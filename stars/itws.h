#pragma once
#include <optional>

#include "nexrad.h"

namespace stars {
struct ItwsGrid {
  QString source;
  qint64 generated{}, expires{};  // Product time, UTC milliseconds.
  int width{}, rows{}, columns{};
  double latitude{}, longitude{}, rotation{}, x{}, y{}, dx{}, dy{};
  QByteArray cells;
  std::shared_ptr<const NexradFrame> render() const;
};
std::optional<ItwsGrid> decodeItws(QByteArray xml);
}  // namespace stars
