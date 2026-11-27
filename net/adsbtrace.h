#pragma once
#include <QPointF>

#include "adsbindex.h"

namespace stars::net::adsb {
enum class Source : quint8 { Unknown, Adsb, Adsr, Tisb, Mlat, Other };
struct Observation {
  qint64 timeMs;
  QPointF position;
  quint32 flags;
  Source source;
  bool breakBefore;
  bool hasPosition;  // False for a retained discontinuity with invalid
                     // coordinates.
};
struct Slice {
  QString key;
  QDate date;
  qint64 beginMs, endMs;
  std::vector<Observation>
      points;  // Includes the nearest bracket on either side.
};
Slice decodeTrace(const QByteArray& gzip, const QString& icao, QDate date,
                  const QString& key, qint64 beginMs, qint64 endMs);
}  // namespace stars::net::adsb
