#include "adaptation.h"

#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

#include "scrollbar.h"
#include "theme.h"

namespace stars::ui {
namespace {
QString channels(QColor color) {
  return QStringLiteral("%1, %2, %3")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue());
}
QColor fieldColor(const QLineEdit* field) {
  if (!field->hasAcceptableInput()) return {};
  const auto parts = field->text().split(',');
  return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
}
QVBoxLayout* section(QVBoxLayout* parent, const QString& title,
                     int stretch = 0) {
  auto* container = new QWidget;
  auto* layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(parent->spacing());
  parent->addWidget(container, stretch);
  auto* heading = new QLabel(title);
  auto font = interBlackFont();
  font.setPixelSize(13);
  heading->setFont(font);
  heading->setStyleSheet(
      QStringLiteral("font-family: '%1'; font-size: 13px; font-weight: 900;")
          .arg(font.family()));
  heading->setAlignment(Qt::AlignCenter);
  layout->addWidget(heading);
  auto* rule = new QFrame;
  rule->setFixedHeight(1);
  rule->setStyleSheet("background-color: rgba(255,255,255,102);");
  layout->addWidget(rule);
  return layout;
}
QWidget* selectorSection(QVBoxLayout* parent, const QString& title,
                         const QString& prefix, const QStringList& names,
                         bool& selected,
                         const std::function<void(bool)>& change) {
  auto* layout = section(parent, title);
  layout->setContentsMargins(0, 8, 0, 0);
  auto* selector = new QWidget;
  auto* segments = new QHBoxLayout(selector);
  segments->setContentsMargins(0, 0, 0, 0);
  segments->setSpacing(0);
  auto* group = new QButtonGroup(selector);
  for (int i = 0; i < 2; ++i) {
    auto* button = new QPushButton(names[i], selector);
    button->setObjectName(prefix + names[i]);
    button->setAccessibleName(title + " " + names[i]);
    button->setProperty("segment", i == 0 ? "first" : "last");
    button->setAutoDefault(false);
    button->setCheckable(true);
    group->addButton(button, i);
    segments->addWidget(button);
  }
  group->button(selected)->setChecked(true);
  layout->addWidget(selector, 0, Qt::AlignHCenter);
  QObject::connect(group, &QButtonGroup::idClicked, selector,
                   [&selected, &change, group, selector, title](int id) {
                     const bool value = id == 1;
                     if (value == selected) return;
                     try {
                       if (change) change(value);
                       selected = value;
                     } catch (const std::exception& error) {
                       group->button(selected)->setChecked(true);
                       QMessageBox::warning(selector,
                                            "Could Not Change " + title,
                                            QString::fromUtf8(error.what()));
                     }
                   });
  return layout->parentWidget();
}

}  // namespace

Adaptation::Adaptation() = default;
Adaptation::~Adaptation() = default;
void Adaptation::configure(QList<PaletteColor> defaults,
                           ColorOverrides overrides, SaveColors save) {
  defaults_ = std::move(defaults);
  overrides_ = std::move(overrides);
  save_ = std::move(save);
}
void Adaptation::configureSymbols(
    QStringList codes, QStringList selected,
    std::function<void(const QStringList&)> save) {
  codes_ = std::move(codes);
  subsetSymbols_ = std::move(selected);
  saveSymbols_ = std::move(save);
}
void Adaptation::configureStipple(bool enabled,
                                  std::function<void(bool)> change) {
  faaStipple_ = enabled;
  changeStipple_ = std::move(change);
}
void Adaptation::configureFont(bool setB, std::function<void(bool)> change) {
  setB_ = setB;
  changeFont_ = std::move(change);
}
void Adaptation::open() {
  if (dialog_ && dialog_->isVisible()) {
    dialog_->raise();
    dialog_->activateWindow();
    return;
  }
  dialog_ = std::make_unique<QDialog>();
  auto* dialog = dialog_.get();
  dialog->setWindowTitle("Adaptation Settings");
  dialog->setObjectName("adaptationSettings");
  auto* layout = new QVBoxLayout(dialog);
  layout->setContentsMargins(16, 14, 16, 12);
  layout->setSpacing(6);
  auto* search = new QLineEdit(dialog);
  search->setObjectName("adaptationSearch");
  configureSearchField(search, "Search Adaptation Settings");
  search->setMinimumHeight(30);
  layout->addWidget(search);
  const QString paletteTitle = "Color Palette";
  auto* palette = section(layout, paletteTitle, 1);
  auto* scroll = new QScrollArea(dialog);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* rows = new QWidget;
  auto* grid = new QGridLayout(rows);
  grid->setContentsMargins(0, 4, 14, 4);
  grid->setVerticalSpacing(4);
  grid->setHorizontalSpacing(8);
  grid->setColumnStretch(0, 1);
  grid->setAlignment(Qt::AlignTop);
  const QString channel = "(25[0-5]|2[0-4][0-9]|1[0-9]{2}|[0-9]{1,2})";
  const QRegularExpression pattern(
      QString(R"(\s*%1\s*,\s*%1\s*,\s*%1\s*)").arg(channel));
  auto* validator = new QRegularExpressionValidator(pattern, dialog);
  QMap<QString, QLineEdit*> fields;
  struct ColorRow {
    QLabel* label;
    QPushButton* swatch;
    QLineEdit* input;
  };
  QList<ColorRow> colorRows;
  for (const auto& role : defaults_) {
    const auto color = overrides_.value(role.key, role.color);
    auto* label = new QLabel(role.label, rows);
    auto* rgb = new QLineEdit(channels(color), rows);
    rgb->setObjectName(role.key == "BackgroundCeiling" ? "bkcCeiling"
                                                       : "rgb" + role.key);
    rgb->setAccessibleName(role.label + " RGB");
    rgb->setToolTip("Red, green, blue (0–255 each). Default: " +
                    channels(role.color));
    rgb->setValidator(validator);
    rgb->setFixedWidth(114);
    rgb->setMinimumHeight(25);
    label->setBuddy(rgb);
    auto* swatch = new QPushButton(rows);
    swatch->setFixedSize(36, 25);
    swatch->setAutoDefault(false);
    swatch->setAccessibleName("Choose " + role.label + " Color");
    swatch->setToolTip(swatch->accessibleName());
    const auto paint = [swatch](QColor color) {
      swatch->setStyleSheet("QPushButton { background: " + color.name() +
                            "; border: 1px solid #727280; }"
                            "QPushButton:hover, QPushButton:focus { border: "
                            "2px solid #F0F0F0; }");
    };
    paint(color);
    QObject::connect(rgb, &QLineEdit::textChanged, dialog, [rgb, paint] {
      const auto current = fieldColor(rgb);
      if (current.isValid()) paint(current);
    });
    QObject::connect(swatch, &QPushButton::clicked, dialog,
                     [rgb, role, dialog] {
                       auto initial = fieldColor(rgb);
                       if (!initial.isValid()) initial = role.color;
                       const auto picked =
                           QColorDialog::getColor(initial, dialog, role.label);
                       if (picked.isValid()) rgb->setText(channels(picked));
                     });
    const int row = fields.size();
    grid->addWidget(label, row, 0);
    grid->addWidget(swatch, row, 1);
    grid->addWidget(rgb, row, 2);
    colorRows.append({label, swatch, rgb});
    fields.insert(role.key, rgb);
  }
  scroll->setWidget(rows);
  installOverlayScrollbar(scroll);
  palette->addWidget(scroll);
  auto* stipple = selectorSection(layout, "WX Stipple", "wxStipple",
                                  {"Standard", "FAA-HF-STD-010A"}, faaStipple_,
                                  changeStipple_);
  auto* font = selectorSection(layout, "Font Set", "fontSet", {"A", "B"}, setB_,
                               changeFont_);
  auto* symbols = section(layout, "Position Symbols");
  symbols->addWidget(new QLabel("Include Controller Subset"));
  auto* positions = new QListWidget;
  positions->setAccessibleName("Include Controller Subset");
  positions->setMaximumHeight(96);
  for (const auto& code : codes_) {
    auto* item = new QListWidgetItem(code, positions);
    item->setCheckState(subsetSymbols_.contains(code) ? Qt::Checked
                                                      : Qt::Unchecked);
  }
  installOverlayScrollbar(positions);
  symbols->addWidget(positions);
  symbols->parentWidget()->setVisible(positions->count() > 0);
  auto* empty = new QLabel("No Matching Settings", dialog);
  empty->setAlignment(Qt::AlignCenter);
  layout->addWidget(empty);
  empty->hide();
  layout->addStretch();
  QObject::connect(
      search, &QLineEdit::textChanged, dialog, [=](const QString& text) {
        const auto query = text.trimmed();
        const bool allColors =
            paletteTitle.contains(query, Qt::CaseInsensitive);
        bool hasColors = false;
        for (const auto& row : colorRows) {
          const bool match = allColors || row.label->text().contains(
                                              query, Qt::CaseInsensitive);
          hasColors = hasColors || match;
          row.label->setVisible(match);
          row.swatch->setVisible(match);
          row.input->setVisible(match);
        }
        palette->parentWidget()->setVisible(hasColors);
        bool hasMatch = hasColors;
        for (auto* selector : {stipple, font}) {
          const auto choices = selector->findChildren<QPushButton*>();
          const bool match = std::any_of(
              choices.cbegin(), choices.cend(), [&](const auto* button) {
                return button->accessibleName().contains(query,
                                                         Qt::CaseInsensitive);
              });
          selector->setVisible(match);
          hasMatch = hasMatch || match;
        }
        const bool allPositions =
            QStringLiteral("Position Symbols Include Controller Subset")
                .contains(query, Qt::CaseInsensitive);
        bool anyPosition = false;
        for (int i = 0; i < positions->count(); ++i) {
          auto* item = positions->item(i);
          const bool match =
              allPositions || item->text().contains(query, Qt::CaseInsensitive);
          item->setHidden(!match);
          anyPosition |= match;
        }
        symbols->parentWidget()->setVisible(anyPosition);
        empty->setVisible(!hasMatch && !anyPosition);
      });
  layout->addSpacing(6);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
  layout->addWidget(buttons);
  for (auto* field : fields)
    QObject::connect(field, &QLineEdit::textChanged, dialog, [fields, buttons] {
      const bool valid = std::all_of(
          fields.cbegin(), fields.cend(),
          [](const auto* edit) { return edit->hasAcceptableInput(); });
      buttons->button(QDialogButtonBox::Save)->setEnabled(valid);
    });
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog,
                   &QDialog::reject);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog,
                   [this, fields, positions, dialog] {
                     ColorOverrides changes;
                     for (const auto& role : defaults_) {
                       const auto value = fieldColor(fields.value(role.key));
                       if (!value.isValid()) return;
                       if (value != overrides_.value(role.key, role.color))
                         changes.insert(role.key,
                                        value == role.color ? QColor{} : value);
                     }
                     try {
                       if (!changes.isEmpty()) {
                         auto updated = overrides_;
                         applyColorChanges(updated, changes);
                         save_(changes, updated);
                         overrides_ = std::move(updated);
                       }
                       QStringList selected = subsetSymbols_;
                       for (int i = 0; i < positions->count(); ++i) {
                         const auto* item = positions->item(i);
                         if (item->checkState() == Qt::Unchecked)
                           selected.removeAll(item->text());
                         else if (!selected.contains(item->text()))
                           selected.append(item->text());
                       }
                       if (selected != subsetSymbols_) {
                         saveSymbols_(selected);
                         subsetSymbols_ = std::move(selected);
                       }
                       dialog->accept();
                     } catch (const std::exception& error) {
                       QMessageBox::warning(dialog, "Could Not Save Adaptation",
                                            QString::fromUtf8(error.what()));
                     }
                   });
  auto* screen = QGuiApplication::screenAt(QCursor::pos());
  if (!screen) screen = dialog->screen();
  dialog->resize(QSize(490, 640).boundedTo(screen->availableGeometry().size() -
                                           QSize(36, 64)));
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}
}  // namespace stars::ui
