#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QTimer>
#include <algorithm>

#include "adsb.h"
using namespace stars::net::adsb;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  app.setApplicationName("STARS");
  auto args = app.arguments();
  Options opts;
  opts.offline = args.removeAll("--offline") > 0;
  const auto cache = args.indexOf("--cache");
  if (cache >= 0) {
    opts.disk.root = args.value(cache + 1);
    if (!QDir::isAbsolutePath(opts.disk.root)) return 2;
    args.remove(cache, 2);
  }
  const bool trace = args.value(1) == "trace";
  const auto day = QDate::fromString(args.value(2), Qt::ISODate);
  if (!day.isValid() ||
      (trace ? args.size() != 6 : args.size() != 3 || args[1] != "index")) {
    qCritical() << "Usage: adsbcheck index DATE | trace DATE ICAO[,ICAO...] "
                   "BEGIN_UTC END_UTC [--offline] [--cache ABSOLUTE_PATH]";
    return 2;
  }
  Service service(opts);
  QList<Consumer> ids;
  QList<QDate> days{day};
  qint64 begin = 0, end = 0;
  if (trace) {
    const auto start = QDateTime::fromString(args[4], Qt::ISODateWithMs);
    const auto stop = QDateTime::fromString(args[5], Qt::ISODateWithMs);
    if (!args[4].endsWith('Z') || !args[5].endsWith('Z') || !start.isValid() ||
        !stop.isValid() || start.date() != day)
      return 2;
    begin = start.toMSecsSinceEpoch();
    end = stop.toMSecsSinceEpoch();
    auto last = stop.addMSecs(-1).date();
    if (last != day) days.append(last);
    for (const auto& address : args[3].split(',')) {
      auto id = service.subscribe();
      if (!service.request(id, address, begin, end)) return 2;
      ids.append(id);
    }
  } else {
    auto id = service.subscribe();
    if (!service.requestIndex(id, day)) return 2;
    ids.append(id);
  }
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, &app, [&] {
    for (auto id : ids)
      for (auto d : days)
        if (service.status(id, d) == Availability::Pending) return;
    int exitCode = 0;
    for (auto d : days) {
      if (auto idx = service.index(d))
        qInfo().noquote() << d.toString(Qt::ISODate) << "schema 1"
                          << idx->repository << idx->tag << "parts"
                          << idx->parts.size() << "archive bytes" << idx->bytes
                          << "entries" << idx->entries.size()
                          << idx->attribution << idx->license;
      for (auto id : ids) {
        auto state = service.status(id, d);
        static constexpr const char* names[]{
            "ready",           "pending",           "index unavailable",
            "aircraft absent", "trace unavailable", "temporary failure",
            "rejected",        "cancelled"};
        qInfo() << "lookup" << names[int(state)];
        if (state != Availability::Ready) {
          exitCode = 1;
          continue;
        }
        if (auto slice = service.available(id, d)) {
          QMap<QString, int> sources;
          static constexpr const char* types[]{"unknown",   "adsb_icao",
                                               "adsr_icao", "tisb_icao",
                                               "mlat",      "other"};
          QList<qint64> intervals;
          qint64 first = -1, last = -1;
          int count = 0, stale = 0, legs = 0;
          for (const auto& p : slice->points) {
            if (p.timeMs < begin || p.timeMs >= end) continue;
            ++count;
            ++sources[types[int(p.source)]];
            stale += bool(p.flags & 1);
            legs += bool(p.flags & 2);
            if (first < 0) first = p.timeMs;
            if (last >= 0 && !p.breakBefore) intervals.append(p.timeMs - last);
            last = p.timeMs;
          }
          std::ranges::sort(intervals);
          qInfo() << "requested (ms)" << begin << end << "coverage (ms)"
                  << first << last << "observations" << count << "sources"
                  << sources << "stale" << stale << "legs" << legs;
          if (!intervals.isEmpty())
            qInfo() << "interval median/p95 (ms)"
                    << intervals[intervals.size() / 2]
                    << intervals[(intervals.size() - 1) * 95 / 100];
        }
      }
    }
    const auto s = service.statistics();
    qInfo() << "HTTP operations" << s.requests << "ranges" << s.ranges
            << "received bytes" << s.downloadBytes << "index bytes"
            << s.indexBytes << "disk hits/misses" << s.diskHits << s.diskMisses
            << "index/trace disk hits" << s.indexHits
            << s.diskHits - s.indexHits << "validation/write failures"
            << s.corruptions << s.writeFailures << "download/decode ms"
            << s.downloadMs << s.decodeMs << "peak trace jobs/decode workers"
            << s.peakJobs << s.peakDecodes;
    app.exit(exitCode);
  });
  timer.start(25);
  return app.exec();
}
