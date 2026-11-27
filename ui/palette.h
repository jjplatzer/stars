#pragma once
#include <QColor>
#include <QList>
#include <QMap>

#include "../stars/colors.h"

namespace stars::ui {
using ColorOverrides = QMap<QString, QColor>;
struct PaletteColor {
  QString key, label;
  QColor color;
};
QList<PaletteColor> paletteColors(const colors::Palette& palette);
colors::Palette adaptedPalette(colors::Palette defaults,
                               const ColorOverrides& overrides);
// An invalid color in a change set restores the workstation default.
void applyColorChanges(ColorOverrides& overrides,
                       const ColorOverrides& changes);
}  // namespace stars::ui
