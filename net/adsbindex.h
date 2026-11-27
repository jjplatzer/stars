#pragma once
#include <QDate>
#include <QUrl>
#include <optional>
#include <vector>

namespace stars::net::adsb {
constexpr qint64 kMiB = 1024 * 1024;
constexpr qint64 kCompressedLimit = 16 * kMiB;
std::optional<QString> normalizeIcao(QStringView address);
QUrl indexUrl(QDate date);

struct Part {
  QUrl url;
  qint64 start, size;
};
struct Entry {
  quint32 icao;
  qint64 offset, size;
};
struct Span {
  Part part;
  qint64 first, last;
  QByteArray range() const;
  QByteArray contentRange() const;
};
struct Index {
  QDate date;
  QString repository, tag, attribution, license, fingerprint;
  qint64 releaseId{}, bytes{};
  std::vector<Part> parts;
  std::vector<Entry> entries;
  const Entry* find(QStringView icao) const;
  QString traceKey(const Entry& entry) const;
};
// Parsing functions throw std::runtime_error on invalid input.
Index decodeIndex(const QByteArray& compressed, QDate date);
std::vector<Span> spans(const Index& index, qint64 offset, qint64 size);
}  // namespace stars::net::adsb
