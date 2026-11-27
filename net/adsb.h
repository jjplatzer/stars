#pragma once
#include <QObject>
#include <memory>

#include "adsbcache.h"
#include "adsbhttp.h"
#include "adsbtrace.h"

namespace stars::net::adsb {
enum class Availability {
  Ready,
  Pending,
  IndexUnavailable,
  Absent,
  TraceUnavailable,
  TemporaryFailure,
  Rejected,
  Cancelled
};
enum class Priority { Current, History, Extension, Prefetch };
using Consumer = quint64;
struct Options {
  Disk disk;
  bool offline = false;
  qint64 memoryBytes = 64 * kMiB;
  int maxConsumers = 512;
  HttpOptions http;
};
struct Statistics {
  qint64 requests{}, ranges{}, downloadBytes{}, diskHits{}, diskMisses{},
      failures{}, writeFailures{}, corruptions{}, indexHits{}, decodeMs{},
      downloadMs{}, indexBytes{};
  int jobs{}, peakJobs{}, peakDecodes{};
};
class Service final : public QObject {
 public:
  // Service methods run on its event-loop thread. Each subscription represents
  // one aircraft (or just its index) and may cover a UTC midnight.
  static Service& shared();
  explicit Service(Options options = {},
                   QNetworkAccessManager* transport = nullptr);
  ~Service() override;
  Consumer subscribe(QObject* pane = nullptr);
  void release(Consumer consumer);
  bool request(Consumer consumer, QStringView address, qint64 beginMs,
               qint64 endMs, Priority priority = Priority::Current);
  bool requestIndex(Consumer consumer, QDate day);
  void refreshIndex(QDate day);
  Availability status(Consumer consumer, QDate day) const;
  std::shared_ptr<const Slice> available(Consumer consumer, QDate day) const;
  std::shared_ptr<const Index> index(QDate day) const;
  quint64 revision() const;
  Statistics statistics() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace stars::net::adsb
