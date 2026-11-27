#pragma once
#include <QImage>
#include <QObject>
#include <functional>
#include <memory>

#include "../stars/prefs.h"
#include "palette.h"

namespace stars::net {
class TaisCache;
}
namespace stars::ui {
// Qt owns the lifecycle; the radar window and presentation use SDL GPU.
class Pane final : public QObject {
  Q_OBJECT
 public:
  explicit Pane(const QString& title, QObject* parent = nullptr,
                const QString& position = {}, const QString& tcp = {},
                const QString& suffix = {});
  void setAdaptation(const ColorOverrides& overrides,
                     std::function<void(const ColorOverrides&)> save);
  void configurePrefSets(const QJsonObject& sets, SavePrefSets save);
  void configureSymbols(QStringList selected,
                        std::function<void(const QStringList&)> save);
  void configureStipple(bool enabled, std::function<void(bool)> save);
  void configureFont(bool setB, std::function<void(bool)> save);
  ~Pane() override;
  QString title() const;
  const net::TaisCache& traffic() const;
  void raise();
  QImage snapshot();
 signals:
  void switchProfileRequested();
  void playRequested();
  void seekRequested(int seconds);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace stars::ui
