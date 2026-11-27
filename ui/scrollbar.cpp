#include "scrollbar.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QVariantAnimation>

namespace stars::ui {
namespace {
class ThinScrollBar final : public QScrollBar {
 public:
  explicit ThinScrollBar(QWidget* parent) : QScrollBar(Qt::Vertical, parent) {
    setObjectName("overlayScrollBar");
    setStyleSheet(
        "QScrollBar:vertical { width: 12px; background: transparent; margin: "
        "0; }"
        "QScrollBar::handle:vertical { min-height: 24px; margin: 0; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
        "height: 0; }");
  }
  void setOpacity(qreal opacity) {
    opacity_ = opacity;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QStyleOptionSlider option;
    initStyleOption(&option);
    auto thumb = style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                         QStyle::SC_ScrollBarSlider, this);
    const int thickness = underMouse() || isSliderDown() ? 6 : 3;
    thumb.setLeft(width() - thickness - 2);
    thumb.setWidth(thickness);
    QPainter painter(this);
    painter.setOpacity(opacity_);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(isSliderDown() ? "#C0C0C5" : "#9D9DA5"));
    painter.drawRoundedRect(thumb, thickness / 2.0, thickness / 2.0);
  }
  void enterEvent(QEnterEvent* event) override {
    QScrollBar::enterEvent(event);
    update();
  }
  void leaveEvent(QEvent* event) override {
    QScrollBar::leaveEvent(event);
    update();
  }

 private:
  qreal opacity_ = 1;
};

class OverlayScrollbar final : public QObject {
 public:
  explicit OverlayScrollbar(QAbstractScrollArea* area)
      : QObject(area), area_(area), overlay_(new ThinScrollBar(area)) {
    // A viewport child is translated by pixel scrolling. Keep the overlay
    // as its sibling so it remains anchored to the visible list instead.
    area_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area_->viewport()->installEventFilter(this);
    area_->viewport()->setMouseTracking(true);
    area_->setAttribute(Qt::WA_Hover);
    qApp->installEventFilter(this);
    idle_.setSingleShot(true);
    idle_.setInterval(2000);
    idle_.setTimerType(Qt::PreciseTimer);
    fade_.setDuration(180);
    connect(&idle_, &QTimer::timeout, this, [this] {
      if (overlay_->isSliderDown()) {
        idle_.start();
        return;
      }
      fade_.setStartValue(1.0);
      fade_.setEndValue(0.0);
      fade_.start();
    });
    connect(&fade_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
              overlay_->setOpacity(value.toReal());
            });
    connect(&fade_, &QVariantAnimation::finished, overlay_, &QWidget::hide);
    connect(area_->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this] {
              updateOverlay();
              activity();
            });
    connect(area_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] {
              updateOverlay();
              activity();
            });
    connect(overlay_, &QScrollBar::valueChanged, area_->verticalScrollBar(),
            &QScrollBar::setValue);
    connect(overlay_, &QScrollBar::sliderReleased, this,
            [this] { activity(); });
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == area_->viewport() &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
      updateOverlay();
      activity();
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    if (area_->isVisible() && widget &&
        (widget == area_->parentWidget() ||
         area_->parentWidget()->isAncestorOf(widget)) &&
        (event->type() == QEvent::MouseMove ||
         event->type() == QEvent::HoverMove || event->type() == QEvent::Wheel ||
         event->type() == QEvent::KeyPress))
      activity();
    return QObject::eventFilter(watched, event);
  }

 private:
  void activity() {
    if (!area_->isVisible() || overlay_->maximum() <= overlay_->minimum())
      return;
    fade_.stop();
    overlay_->setOpacity(1);
    overlay_->show();
    overlay_->raise();
    idle_.start();
  }
  void updateOverlay() {
    const QSignalBlocker block(overlay_);
    auto* source = area_->verticalScrollBar();
    overlay_->setRange(source->minimum(), source->maximum());
    overlay_->setPageStep(source->pageStep());
    overlay_->setSingleStep(source->singleStep());
    overlay_->setValue(source->value());
    const auto area = area_->viewport()->geometry();
    overlay_->setGeometry(area.right() - 11, area.top(), 12, area.height());
    if (source->maximum() <= source->minimum()) overlay_->hide();
  }
  QAbstractScrollArea* area_;
  ThinScrollBar* overlay_;
  QTimer idle_;
  QVariantAnimation fade_;
};
}  // namespace
void installOverlayScrollbar(QAbstractScrollArea* area) {
  new OverlayScrollbar(area);
}
}  // namespace stars::ui
