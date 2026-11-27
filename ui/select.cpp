#include "select.h"

#include <QApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyleOptionButton>
#include <QStyleOptionSlider>
#include <QStylePainter>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>

#include "scrollbar.h"
#include "theme.h"

namespace stars::ui {

SearchSelect::SearchSelect(const QString& placeholder, QWidget* parent)
    : QPushButton(parent),
      popup_(new QFrame(this, Qt::Popup)),
      search_(new QLineEdit(popup_)),
      list_(new QListWidget(popup_)),
      empty_(new QLabel(tr("No Matches"), popup_)) {
  setText({});
  setProperty("selector", true);
  setMinimumHeight(38);
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  popup_->setObjectName("selectionPopup");
  auto* layout = new QVBoxLayout(popup_);
  layout->setContentsMargins(1, 1, 1, 1);
  layout->setSpacing(4);
  search_->setObjectName("selectionSearch");
  configureSearchField(search_, tr("Search Controller Positions"));
  search_->setMinimumHeight(34);
  installOverlayScrollbar(list_);
  list_->setObjectName("selectionList");
  // QAbstractScrollArea's default minimum hint otherwise adds empty space
  // beneath short lists, even when the popup requests exactly one row.
  list_->setMinimumHeight(0);
  list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
  list_->setAccessibleName(placeholder);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list_->setMouseTracking(true);
  list_->setIconSize(QSize(12, 12));
  empty_->setObjectName("emptySelection");
  empty_->setAlignment(Qt::AlignCenter);
  layout->addWidget(search_);
  layout->addWidget(list_, 1);
  layout->addWidget(empty_);
  popup_->installEventFilter(this);
  search_->installEventFilter(this);
  list_->installEventFilter(this);
  connect(this, &QPushButton::clicked, this, &SearchSelect::openPopup);
  connect(search_, &QLineEdit::textChanged, this, &SearchSelect::filter);
  connect(list_, &QListWidget::itemClicked, this, [this] { chooseCurrent(); });
}

void SearchSelect::setSearchEnabled(bool enabled) {
  searchEnabled_ = enabled;
  search_->setVisible(enabled);
  search_->clear();
}

void SearchSelect::setOptions(const QList<SelectionOption>& options) {
  popup_->hide();
  options_ = options;
  list_->clear();
  for (const auto& option : options) {
    auto* item = new QListWidgetItem(option.label, list_);
    item->setData(Qt::UserRole, option.id);
    item->setData(Qt::UserRole + 1, option.label + " " + option.searchText);
    item->setToolTip(option.label);
    item->setSizeHint(QSize(0, 34));
  }
  const bool changed = !selectedId_.isEmpty();
  selectedId_.clear();
  setText({});
  setToolTip({});
  setEnabled(!options.isEmpty());
  if (changed) emit selectionChanged();
}

void SearchSelect::select(const QString& id) {
  for (const auto& option : options_) {
    if (option.id != id) continue;
    const bool changed = selectedId_ != id;
    selectedId_ = id;
    setText(option.label);
    setToolTip(option.label);
    if (changed) emit selectionChanged();
    return;
  }
}

void SearchSelect::openPopup() {
  if (options_.isEmpty()) return;
  search_->clear();
  filter({});
  for (int i = 0; i < list_->count(); ++i) {
    if (list_->item(i)->data(Qt::UserRole).toString() == selectedId_)
      list_->setCurrentRow(i);
  }
  list_->scrollToItem(list_->currentItem(), QAbstractItemView::PositionAtTop);
  resizePopup();
  popup_->show();
  if (searchEnabled_)
    search_->setFocus(Qt::PopupFocusReason);
  else
    list_->setFocus(Qt::PopupFocusReason);
  if (selectedId_.isEmpty()) {
    list_->setCurrentRow(-1);
    list_->clearSelection();
  }
}

void SearchSelect::resizePopup() {
  const auto origin = mapToGlobal(QPoint(0, height() + 4));
  const auto* screen = QGuiApplication::screenAt(origin);
  const auto available =
      (screen ? screen : QGuiApplication::primaryScreen())->availableGeometry();
  int visibleRows = 0;
  for (int i = 0; i < list_->count(); ++i)
    if (!list_->item(i)->isHidden()) ++visibleRows;
  const int contentHeight =
      visibleRows ? 34 * std::min(visibleRows, 6) : empty_->sizeHint().height();
  const auto margins =
      popup_->contentsMargins() + popup_->layout()->contentsMargins();
  const int searchHeight =
      searchEnabled_
          ? std::max(search_->minimumHeight(), search_->sizeHint().height()) +
                popup_->layout()->spacing()
          : 0;
  const int popupHeight =
      margins.top() + margins.bottom() + searchHeight + contentHeight;
  popup_->resize(std::min(width(), available.width()),
                 std::min(popupHeight, available.height()));
  const int x = std::clamp(origin.x(), available.left(),
                           available.right() - popup_->width() + 1);
  int y = origin.y();
  if (y + popup_->height() > available.bottom() + 1)
    y = mapToGlobal(QPoint(0, -4)).y() - popup_->height();
  popup_->move(x, std::clamp(y, available.top(),
                             available.bottom() - popup_->height() + 1));
}

void SearchSelect::filter(const QString& query) {
  const auto words = query.simplified().split(' ', Qt::SkipEmptyParts);
  int first = -1;
  for (int i = 0; i < list_->count(); ++i) {
    auto* item = list_->item(i);
    const auto text = item->data(Qt::UserRole + 1).toString();
    const bool matches =
        std::all_of(words.begin(), words.end(), [&text](const auto& word) {
          return text.contains(word, Qt::CaseInsensitive);
        });
    item->setHidden(!matches);
    if (matches && first == -1) first = i;
  }
  list_->setCurrentRow(query.trimmed().isEmpty() ? -1 : first);
  if (query.trimmed().isEmpty()) list_->clearSelection();
  empty_->setVisible(first == -1);
  list_->setVisible(first != -1);
  if (popup_->isVisible()) resizePopup();
}

void SearchSelect::chooseCurrent() {
  const auto* item = list_->currentItem();
  if (!item || item->isHidden()) return;
  const auto id = item->data(Qt::UserRole).toString();
  popup_->hide();
  setFocus(Qt::PopupFocusReason);
  select(id);
}

bool SearchSelect::eventFilter(QObject* watched, QEvent* event) {
  if (watched == popup_ &&
      (event->type() == QEvent::Show || event->type() == QEvent::Hide))
    update();
  if (event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      popup_->hide();
      setFocus(Qt::PopupFocusReason);
      return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      chooseCurrent();
      return true;
    }
    if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
      popup_->hide();
      setFocus(Qt::PopupFocusReason);
      focusNextPrevChild(key->key() == Qt::Key_Tab);
      return true;
    }
    if (watched == search_ &&
        (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up ||
         key->key() == Qt::Key_PageDown || key->key() == Qt::Key_PageUp)) {
      QApplication::sendEvent(list_, key);
      return true;
    }
  }
  return QPushButton::eventFilter(watched, event);
}

void SearchSelect::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Return ||
      event->key() == Qt::Key_Enter) {
    openPopup();
    event->accept();
    return;
  }
  QPushButton::keyPressEvent(event);
}

void SearchSelect::paintEvent(QPaintEvent*) {
  QStylePainter painter(this);
  QStyleOptionButton option;
  initStyleOption(&option);
  painter.drawControl(QStyle::CE_PushButtonBevel, option);
  option.rect.adjust(12, 0, -32, 0);
  option.text = fontMetrics().elidedText(text(), Qt::ElideRight,
                                         std::max(0, width() - 54));
  painter.drawControl(QStyle::CE_PushButtonLabel, option);
  if (!isEnabled()) painter.setOpacity(0.4);
  painter.setFont(materialSymbolsFont());
  painter.setPen(QColor("#A9A9C0"));
  painter.translate(width() - 19, height() / 2.0);
  if (popup_->isVisible()) painter.rotate(180);
  painter.drawText(QRect(-10, -height() / 2, 20, height()), Qt::AlignCenter,
                   QString(QChar(0xE5CF)));  // expand_more
}

}  // namespace stars::ui
