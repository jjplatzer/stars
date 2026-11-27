#pragma once
#include "adsbindex.h"
namespace stars::net::adsb {
int retentionDays();
// Called only on disk/decode workers. Keys are generated from validated
// metadata.
struct Disk {
  QString root;
  qint64 budget = 512 * kMiB;
  int days = retentionDays();
  QByteArray read(const QString& key, qint64 limit) const;
  bool store(const QString& key, QDate date, const QByteArray& bytes) const;
  void discard(const QString& key, const QByteArray& observed) const;
};
QString indexKey(QDate day);
}  // namespace stars::net::adsb
