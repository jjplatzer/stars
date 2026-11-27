#pragma once
#include <QList>
#include <memory>

#include "prefs.h"

namespace stars {
struct Restriction {
  QChar kind;
  QStringList text;
  QList<QPointF> vertices;
  QPointF label;
  double radius{};
  int color = 1;
  bool blink = false, shaded = false;
  QString serial;
};
class Restrictions {
 public:
  void configure(const QString& site, bool dod);
  void cancel();
  // On a successful step the caller clears input. A complete command exits F12.
  QString enter(QString input, std::optional<QPointF> click, Preferences& prefs,
                bool& complete);
  bool cycling() const { return colorId_ != 0; }
  QString stepColor();
  const QMap<int, Restriction>& areas() const { return *areas_; }
  const std::optional<Restriction>& draft() const { return draft_; }
  bool waitingLabel() const { return labelPending_; }

 private:
  std::shared_ptr<QMap<int, Restriction>> areas_ =
      std::make_shared<QMap<int, Restriction>>();
  std::optional<Restriction> draft_;
  bool labelPending_ = false, dod_ = false, moveBlink_ = false,
       colorChanged_ = false;
  int moveId_ = 0, colorId_ = 0;
  QString create(QPointF label);
  QString move(int id, QPointF label, bool blink);
};
}  // namespace stars
