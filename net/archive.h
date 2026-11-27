#pragma once
#include <QSet>

#include "tais.h"

namespace stars::net {
inline constexpr qint64 windowSeconds = 300;
struct TaisWindow {
  QList<Delivery> events;
  QSet<QString> parts;
};
std::shared_ptr<const TaisWindow> loadWindow(
    const ArchiveSource& source, const QString& facility, qint64 start,
    std::shared_ptr<const TaisWindow> cached,
    const std::atomic_bool& cancelled);
}  // namespace stars::net
