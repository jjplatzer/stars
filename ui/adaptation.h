#pragma once
#include <functional>
#include <memory>

#include "palette.h"

class QDialog;

namespace stars::ui {
class Adaptation {
 public:
  Adaptation();
  ~Adaptation();
  using SaveColors = std::function<void(const ColorOverrides& changes,
                                        const ColorOverrides& updated)>;
  void configure(QList<PaletteColor> defaults, ColorOverrides overrides,
                 SaveColors save);
  void configureSymbols(QStringList codes, QStringList selected,
                        std::function<void(const QStringList&)> save);
  void configureStipple(bool enabled, std::function<void(bool)> change);
  void configureFont(bool setB, std::function<void(bool)> change);
  void open();

 private:
  QStringList codes_, subsetSymbols_;
  std::function<void(const QStringList&)> saveSymbols_;
  bool faaStipple_ = false;
  std::function<void(bool)> changeStipple_;
  bool setB_ = true;
  std::function<void(bool)> changeFont_;
  QList<PaletteColor> defaults_;
  ColorOverrides overrides_;
  SaveColors save_;
  std::unique_ptr<QDialog> dialog_;
};
}  // namespace stars::ui
