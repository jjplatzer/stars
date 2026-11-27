#pragma once
#include <QDateTime>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <optional>

namespace stars::ui {
class Playback {
 public:
  static qint64 earliest(qint64 now);
  static qint64 latest(qint64 now);
  void select(qint64 time, qint64 now);
  bool loading() const { return !loadDeadline_.hasExpired(); }
  void dataReady() { loadDeadline_ = QDeadlineTimer{}; }
  void toggle(qint64 now);
  bool canSeek(int seconds, qint64 now) const;
  void seek(int seconds, qint64 now);
  qint64 position(qint64 now) const;
  qint64 currentMs(qint64 now) const;
  bool selected() const { return positionMs_.has_value(); }
  bool playing() const { return elapsed_.isValid(); }

 private:
  std::optional<qint64> positionMs_;
  QElapsedTimer elapsed_;
  QDeadlineTimer loadDeadline_;
};
}  // namespace stars::ui
