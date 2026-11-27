#include "theme.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QLineEdit>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <stdexcept>

#include "../util/resources.h"

namespace {

QString loadFont(const QString& path) {
  const auto font = stars::util::readCompressedResource(path);
  const int id = QFontDatabase::addApplicationFontFromData(font);
  const QStringList families = QFontDatabase::applicationFontFamilies(id);
  if (id < 0 || families.isEmpty()) {
    throw std::runtime_error("Cannot register embedded UI font");
  }
  return families.first();
}

}  // namespace

namespace stars::ui {
using util::readCompressedResource;

QFont interBlackFont() {
  static const QString family =
      loadFont(QStringLiteral(":/fonts/Inter-Black.ttf.zst"));
  QFont font(family);
  font.setWeight(QFont::Black);
  font.setStyleName(QStringLiteral("Black"));
  return font;
}

QFont materialSymbolsFont() {
  static const QString family =
      loadFont(QStringLiteral(":/fonts/material-symbols.ttf.zst"));
  QFont font(family);
  font.setPixelSize(20);
  return font;
}

void configureSearchField(QLineEdit* field, const QString& accessibleName) {
  const auto symbolIcon = [](QChar symbol) {
    QPixmap glyph(48, 48);
    glyph.setDevicePixelRatio(3);
    glyph.fill(Qt::transparent);
    QPainter painter(&glyph);
    auto font = materialSymbolsFont();
    font.setPixelSize(16);
    painter.setFont(font);
    painter.setPen(QColor("#A9A9C0"));
    painter.drawText(QRect(0, 0, 16, 16), Qt::AlignCenter, QString(symbol));
    painter.end();
    return QIcon(glyph);
  };
  static const QIcon searchIcon = symbolIcon(QChar(0xE8B6));  // search
  static const QIcon clearIcon = symbolIcon(QChar(0xE5CD));   // close
  field->setAccessibleName(accessibleName);
  auto* action = field->addAction(searchIcon, QLineEdit::TrailingPosition);
  const auto updateAction = [action, accessibleName](const QString& text) {
    action->setIcon(text.isEmpty() ? searchIcon : clearIcon);
    action->setText(text.isEmpty() ? accessibleName
                                   : QLineEdit::tr("Clear Search"));
  };
  updateAction(field->text());
  QObject::connect(field, &QLineEdit::textChanged, action, updateAction);
  QObject::connect(action, &QAction::triggered, field, [field] {
    field->clear();
    field->setFocus();
  });
}

void applyTheme(QApplication& app) {
  materialSymbolsFont();
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  const QString family =
      loadFont(QStringLiteral(":/fonts/Inter-Regular.ttf.zst"));
  const QString boldFamily = interBlackFont().family();
  const QString semiboldFamily =
      loadFont(QStringLiteral(":/fonts/Inter-SemiBold.ttf.zst"));
  QFont font(family);
  font.setPixelSize(14);
  app.setFont(font);

  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#2A2B34"));
  palette.setColor(QPalette::Base, QColor("#2A2B34"));
  palette.setColor(QPalette::WindowText, QColor("#F0F0F0"));
  palette.setColor(QPalette::Text, QColor("#F0F0F0"));
  palette.setColor(QPalette::Button, QColor("#3D3D4C"));
  palette.setColor(QPalette::ButtonText, QColor("#F0F0F0"));
  palette.setColor(QPalette::Highlight, QColor("#5C5F74"));
  palette.setColor(QPalette::HighlightedText, QColor("#F0F0F0"));
  app.setPalette(palette);

  QFile style(QStringLiteral(":/ui/profiles.qss"));
  if (!style.open(QIODevice::ReadOnly)) {
    throw std::runtime_error("Cannot open embedded profile stylesheet");
  }
  // Native macOS widget defaults can override QApplication's font once QSS
  // changes its size. Name both embedded families explicitly in the sheet.
  app.setStyleSheet(
      QString::fromUtf8(style.readAll()) +
      QStringLiteral("\nQWidget { font-family: \"%1\"; }\nQLabel#heading, "
                     "QLabel#fieldLabel { font-family: \"%2\"; }")
          .arg(family, boldFamily) +
      QStringLiteral(
          "\nQLabel#profileName { font-family: \"%1\"; font-weight: 600; }")
          .arg(semiboldFamily));
}

}  // namespace stars::ui
