#pragma once

#include "net/tais.h"
#include "net/track.h"
#include "net/trajectory.h"

namespace stars {
enum class ReconstructionMode { Original, Hz1, Hz2 };

struct FusionTrack {
  QPointF position;
  QList<QPointF> history;
};

// Owned by one pane. Workers capture immutable inputs, never this object.
class Fusion {
 public:
  ~Fusion();
  Fusion() = default;
  Fusion(const Fusion&) = delete;
  Fusion& operator=(const Fusion&) = delete;
  void setMode(ReconstructionMode mode);
  ReconstructionMode mode() const { return mode_; }
  void reset();
  bool update(const net::TaisCache::Snapshots& snapshots,
              const QMap<QString, net::RadarTrack>& tracks, qint64 playbackMs,
              int historyCount, bool trafficChanged);
  const FusionTrack* display(const QString& number) const;

 private:
  struct Measurement {
    QString number, address;
    net::TrajectoryAnchor anchor;
    qint64 receiptNs;
    bool usable, drop, acquisition;
  };
  using Measurements = QList<Measurement>;
  struct Parsed {
    std::shared_ptr<const net::TaisWindow> source;
    std::shared_ptr<const Measurements> measurements;
  };
  struct Run {
    // Pointers are stable for the lifetime of Index::windows. Keep each raw
    // observation once, even when the playback window moves.
    QList<const Measurement*> points;
    // First epoch at which this run can no longer supply even a history trail.
    std::optional<qint64> discontinuity;
  };
  using Runs = QMap<QString, QList<Run>>;
  struct Index {
    QMap<qint64, Parsed> windows;
    Runs runs;
  };
  static void index(QPromise<Index>& work,
                    const net::TaisCache::Snapshots& snapshots,
                    QMap<qint64, Parsed> previous);
  QFuture<Index> pending_;
  net::TaisCache::Snapshots requested_;
  Index index_;
  bool working_ = false, dirty_ = true;
  quint64 generation_ = 0, jobGeneration_ = 0;
  qint64 epoch_ = -1;
  int historyCount_ = -1;
  ReconstructionMode mode_ = ReconstructionMode::Original;
  QMap<QString, FusionTrack> displayed_;
};
}  // namespace stars
