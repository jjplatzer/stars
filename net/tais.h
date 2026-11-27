#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QFutureWatcher>
#include <QMap>
#include <atomic>
#include <memory>
#include <optional>

namespace stars::net {

// Original recorder receipt order and JMS metadata. Body contains UTF-8 text
// or the original bytes (decoded from the archive's base64 representation).
struct Delivery {
  qint64 receivedNs{}, sequence{}, jmsTimestamp{};
  QString writer, queue, messageId, root, facility, encoding;
  bool redelivered{};
  QByteArray body;
};

struct ArchiveSource {
  QString directory, bucket;
  bool itws = false;
  static ArchiveSource configured();
};

struct TaisWindow;

// Use on the owning event-loop thread. All I/O, parsing and sorting run on
// workers.
class TaisCache final : public QObject {
 public:
  TaisCache(QString facility,
            ArchiveSource source = ArchiveSource::configured());
  ~TaisCache() override;
  bool configured() const;
  // Call on selection and on playback ticks, including while paused.
  void prepare(qint64 utcSecond, int lookbehindSeconds = 0);
  quint64 revision() const { return revision_; }
  bool availableAt(qint64 utcSecond) const;
  bool ready() const;
  QString error() const;
  // Half-open interval in Unix nanoseconds. A missing result means a window
  // isn't loaded. An empty list means a successfully read gap in the archive.
  std::optional<QList<Delivery>> between(qint64 fromNs, qint64 untilNs) const;
  static constexpr int lookaheadSeconds = 180;

 private:
  struct Entry {
    std::shared_ptr<const TaisWindow> data;
    QDeadlineTimer retry;
    QString error;
  };
  struct Result {
    std::shared_ptr<const TaisWindow> data;
    QString error;
  };
  QString facility_;
  ArchiveSource source_;
  QMap<qint64, Entry> windows_;
  std::optional<qint64> first_, active_;
  qint64 last_{};
  quint64 revision_{};
  QFutureWatcher<Result> worker_;
  std::shared_ptr<std::atomic_bool> cancelled_;
  void schedule();
};
}  // namespace stars::net
