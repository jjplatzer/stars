#include "adsbcache.h"

#include <QDirIterator>
#include <QFile>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
namespace stars::net::adsb {
int retentionDays() { return STARS_ADSB_RETENTION_DAYS; }
QString indexKey(QDate day) {
  return "indexes/" + day.toString(Qt::ISODate) + "/index.json.zst";
}
namespace {
bool validKey(const QString& root, const QString& key) {
  static const QRegularExpression pattern(
      R"(^(indexes/[0-9]{4}-[0-9]{2}-[0-9]{2}/index\.json\.zst|traces/[0-9]{4}-[0-9]{2}-[0-9]{2}/[0-9a-f]{64}/[0-9a-f]{6}-[0-9]+-[0-9]+\.json\.gz)$)");
  return QDir::isAbsolutePath(root) && pattern.match(key).hasMatch();
}
}  // namespace
QByteArray Disk::read(const QString& key, qint64 limit) const {
  if (!validKey(root, key) || !QFileInfo::exists(QDir(root).filePath(key)))
    return {};
  QLockFile lock(QDir(root).filePath("maintenance.lock"));
  if (!lock.tryLock(2000)) return {};
  QFile f(QDir(root).filePath(key));
  if (!f.open(QIODevice::ReadOnly)) return {};
  const auto date = QDate::fromString(key.section('/', 1, 1), Qt::ISODate);
  const auto size = f.size();
  if (date < QDateTime::currentDateTimeUtc().date().addDays(-days) ||
      size <= 0 || size > std::min(limit, kCompressedLimit)) {
    f.remove();
    return {};
  }
  auto data = f.read(size);
  if (data.size() != size) return {};
  f.setFileTime(QDateTime::currentDateTimeUtc(),
                QFileDevice::FileModificationTime);
  return data;
}
void Disk::discard(const QString& key, const QByteArray& observed) const {
  if (!validKey(root, key)) return;
  QLockFile lock(QDir(root).filePath("maintenance.lock"));
  if (!lock.tryLock(500)) return;
  QFile f(QDir(root).filePath(key));
  if (f.open(QIODevice::ReadOnly) &&
      (f.size() > kCompressedLimit ||
       (f.size() == observed.size() && f.read(kCompressedLimit) == observed)))
    f.remove();
}
bool Disk::store(const QString& key, QDate date,
                 const QByteArray& bytes) const {
  const auto cutoff = QDateTime::currentDateTimeUtc().date().addDays(-days);
  if (!validKey(root, key) ||
      key.section('/', 1, 1) != date.toString(Qt::ISODate) || bytes.isEmpty() ||
      bytes.size() > std::min(budget, kCompressedLimit) || date < cutoff ||
      !QDir().mkpath(root))
    return false;
  QDir dir(root);
  // Serialize maintenance and atomic commits across processes. Reads hold an
  // open descriptor only until their bounded compressed copy is complete.
  QLockFile lock(dir.filePath("maintenance.lock"));
  if (!lock.tryLock(2000)) return false;
  const auto path = dir.filePath(key);
  QList<QFileInfo> oldest;
  qint64 used = 0;
  for (const auto& family :
       {QStringLiteral("indexes"), QStringLiteral("traces")}) {
    QDirIterator files(dir.filePath(family),
                       QDir::Files | QDir::NoSymLinks | QDir::Hidden,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) {
      files.next();
      const auto info = files.fileInfo();
      if (info.filePath() == path) continue;
      const auto relative = dir.relativeFilePath(info.filePath());
      if (!validKey(root, relative)) {
        // QSaveFile uses a sibling temporary. All writers take this lock, so
        // one left alongside a known payload belongs to an interrupted commit.
        static const QRegularExpression suffix(R"(\.(tmp|[A-Za-z0-9]{6})$)");
        auto match = suffix.match(relative);
        if (match.hasMatch() &&
            validKey(root, relative.left(match.capturedStart())))
          QFile::remove(info.filePath());
        continue;
      }
      const auto day =
          QDate::fromString(relative.section('/', 1, 1), Qt::ISODate);
      if (day < cutoff && QFile::remove(info.filePath())) {
        dir.rmpath(
            info.absolutePath());  // The held lock keeps the root present.
        continue;
      }
      used += info.size();
      oldest.push_back(info);
    }
  }
  std::ranges::sort(oldest, {},
                    [](const QFileInfo& info) { return info.lastModified(); });
  for (const auto& entry : oldest) {
    if (used <= budget - bytes.size()) break;
    if (QFile::remove(entry.filePath())) {
      used -= entry.size();
      dir.rmpath(entry.absolutePath());
    }
  }
  if (used > budget - bytes.size() ||
      !dir.mkpath(QFileInfo(path).absolutePath()))
    return false;
  QSaveFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() &&
         f.commit();
}
}  // namespace stars::net::adsb
