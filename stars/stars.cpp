#include "stars.h"

#include <algorithm>
#include <cmath>

#include "../radar/magnetic.h"

namespace stars {
radar::LatLonTransformations STARSPane::transformations(QSizeF size) const {
  const auto origin = center();
  return {size, origin, range_, longitudeScale_,
          math::toRadians(radar::magneticVariationAt(origin))};
}
QPointF STARSPane::geographic(QPointF pixel, QSizeF size) const {
  return transformations(size).geographic(pixel);
}
QPointF STARSPane::screen(QPointF point, QSizeF size) const {
  return transformations(size).screen(point);
}
renderer::Mat4 STARSPane::projection(QSizeF size) const {
  return transformations(size).projection();
}
QRectF STARSPane::weatherBounds(QSizeF size) const {
  const auto transform = transformations(size);
  const QPointF half(size.width() / 2, size.height() / 2);
  const auto a = transform.delta(half),
             b = transform.delta({-half.x(), half.y()});
  const QPointF radius(std::max(std::abs(a.x()), std::abs(b.x())),
                       std::max(std::abs(a.y()), std::abs(b.y())));
  return {center() - radius, center() + radius};
}

void STARSPane::updateWeatherHistory() {
  if (!historyTimer_.isValid()) return;
  const auto elapsed = historyTimer_.elapsed();
  historyIndex_ = elapsed < 5000 ? 2 : elapsed < 10000 ? 1 : 0;
  if (historyIndex_ == 0) {
    historyTimer_.invalidate();
    historyPictures_ = {};
    if (response_ == "IN PROGRESS") response_.clear();
  }
}

void STARSPane::beginPlayback(qint64 second) {
  if (!dcb_.hasSelectedSet()) prefs_.resetSsaFilters();
  resetTraffic();
  resetWeather();
  gi_ = {};
  enteredAltimeters_.clear();
  setAltimeters({}, second);
}

void STARSPane::resetWeather() {
  weather_ = {};
  historyPictures_ = {};
  historyTimer_.invalidate();
  historyIndex_ = 0;
  if (response_ == "IN PROGRESS") response_.clear();
}

void STARSPane::drawWeather(renderer::CmdBuffer& cb, QSizeF size) const {
  const auto& frame =
      historyIndex_ ? historyPictures_[historyIndex_] : weather_[0];
  if (!frame) return;
  cb.loadProjectionMatrix(projection(size));
  for (size_t i = 0; i < frame->levels.size(); ++i) {
    const auto& level = frame->levels[i];
    if (!prefs_.displayWeatherLevel[i] || !level.fill) continue;
    cb.setRGB(colors::brightness(palette_.WX[i], prefs_.brightness.weather));
    cb.call(level.fill);
    const auto& stipple = faaStipple_ ? level.faaStipple : level.stipple;
    if (stipple) {
      cb.setRGB(
          colors::brightness(palette_.WXPattern, prefs_.brightness.wxContrast));
      cb.call(stipple);
    }
  }
}

void STARSPane::pan(QPointF motion, QSizeF size) {
  userCenter_ = center() - transformations(size).delta(motion);
  userCenter_.setX(math::normalizeLongitude(userCenter_.x()));
  offCenter_ = true;
}
void STARSPane::zoom(double wheel, bool control, QPointF mouse, QSizeF size) {
  const double next = std::clamp(range_ + wheel * (control ? 3 : 1), 6., 512.);
  if (next == range_ || size.isEmpty()) return;
  auto anchor = geographic(mouse, size), current = center();
  userCenter_ = {
      math::normalizeLongitude(
          anchor.x() +
          next / range_ * math::normalizeLongitude(current.x() - anchor.x())),
      anchor.y() + next / range_ * (current.y() - anchor.y())};
  range_ = next;
  offCenter_ = true;
}
void STARSPane::motion(QPointF mouse, QPointF movement, QSizeF size,
                       bool rightButton, bool canPan) {
  hover(mouse);
  if (dcb_.adjusting()) {
    dcb_.adjustMotion(movement.y());
    return;
  }
  if (mouse.y() >= dcbHeight() && canPan && (rightButton || placingCenter()))
    pan(movement, size);
}
void STARSPane::wheel(QPointF mouse, float delta, bool control, QSizeF size) {
  dcb_.motion(mouse);
  if (dcb_.adjusting()) {
    dcb_.wheel(delta, size.width());
    return;
  }
  if (mouse.y() >= dcbHeight())
    zoom(delta, control, mouse, size);
  else if (mouse.y() >= 0)
    dcb_.wheel(delta, size.width());
}
void STARSPane::press(QPointF mouse, QSizeF size) {
  dcb_.motion(mouse);
  scopePressed_ = mouse.y() >= dcbHeight() && !adjusting() && !placing_;
  if (scopePressed_ && mode_ == Mode::Multi &&
      (input_ == "S" || input_ == "TM" || input_ == "TX") && size.width() > 0 &&
      size.height() > 0 && QRectF(QPointF(), size).contains(mouse)) {
    setupList(QPointF(mouse.x() / size.width(), mouse.y() / size.height()));
    scopePressed_ = false;
    return;
  }
  if (mouse.y() < 0 || !showDcb_) return;
  if (mouse.y() < dcbHeight()) {
    restrictions_.cancel();
    mode_ = Mode::None;
    input_.clear();
    response_.clear();
    start_.reset();
  }
  if (dcb_.placingRings() && mouse.y() >= dcbHeight() &&
      QRectF(QPointF(), size).contains(mouse)) {
    prefs_.userRingCenter = geographic(mouse, size);
    prefs_.useUserRingCenter = true;
  }
  try {
    dcb_.press(size.width());
  } catch (...) {
    dcb_.release();
    throw;
  }
}
void STARSPane::release(QPointF mouse, QSizeF size) {
  const bool select =
      mode_ == Mode::Multi
          ? ((input_.size() == 2 && input_.front() == 'L' &&
              input_.back().isDigit()) ||
             input_.startsWith('B') || input_ == "R" || input_ == "RA" ||
             input_ == "RAI" || input_ == "RAE" || input_ == "Q" ||
             input_ == "V" || input_ == "D*" || input_ == "N" ||
             input_ == "N*" || prefs_.lists.contains(input_) ||
             input_.startsWith('P'))
          : mode_ == Mode::Restriction || mode_ == Mode::Min ||
                mode_ == Mode::None &&
                    ((input_.size() == 1 && input_.front().isDigit()) ||
                     input_.isEmpty() || input_ == "*T" ||
                     input_.startsWith("*A") || input_.startsWith("*B") ||
                     input_.startsWith("*D") || input_.startsWith("*J") ||
                     input_.startsWith("*P"));
  if (scopePressed_ && select &&
      QRectF(QPointF(0, dcbHeight()), QPointF(size.width(), size.height()))
          .contains(mouse))
    slew(mouse, size);
  scopePressed_ = false;
  if (mouse.y() >= dcbHeight()) finishCenter();
  dcb_.motion(mouse);
  dcb_.release();
}
void STARSPane::draw(renderer::CmdBuffer& cb, const Fonts& fonts, int width,
                     int height, int pw, int ph, int top) {
  using namespace renderer;
  if (width <= 0 || height <= top || pw <= 0 || ph <= 0) return;
  const int y = int(std::lround(double(top) * ph / height)), h = ph - y;
  if (h <= 0) return;
  // Background contrast affects the scope only, not the application's title
  // bar.
  cb.viewport({0, 0, pw, h});
  cb.scissor({0, 0, pw, h});
  cb.loadProjectionMatrix(screenOrtho(float(width), float(height - top)));
  const float contrast = prefs_.brightness.backgroundContrast / 100.f;
  const auto& base = palette_.Background;
  const auto& ceiling = palette_.BackgroundCeiling;
  ColoredTrianglesBuilder fill;
  fill.addQuad({0, 0}, {float(width), 0}, {float(width), float(height - top)},
               {0, float(height - top)},
               {std::lerp(base.r, ceiling.r, contrast),
                std::lerp(base.g, ceiling.g, contrast),
                std::lerp(base.b, ceiling.b, contrast)});
  fill.generateCommands(cb);
  updateWeatherHistory();
  drawWeather(cb, QSizeF(width, height - top));
  drawRangeRings(cb, width, height - top, float(pw) / width);
  // The DCB overlays the full pane, as in REDS; it does not move its center.
  cb.viewport({0, 0, pw, h});
  cb.scissor({0, 0, pw, h});
  cb.loadProjectionMatrix(projection({double(width), double(height - top)}));
  drawVideoMaps(cb, float(pw) / width);
  drawAtpa(cb, fonts, QSizeF(width, height - top), float(pw) / width);
  updateDwell({double(width), double(height - top)});
  drawRestrictions(cb, fonts, {double(width), double(height - top)},
                   float(pw) / width);
  drawCrdaDiagrams(cb, {double(width), double(height - top)},
                   float(pw) / width);
  drawMinSep(cb, fonts, {double(width), double(height - top)},
             float(pw) / width);
  drawCompass(cb, fonts, width, height - top, pw, h);
  const auto lists = std::clamp(prefs_.charSize.lists, 0, 5);
  drawVideoMapsList(cb, fonts.sizes[lists], fonts.textures[lists], width,
                    height - top);
  drawSystemLists(cb, fonts, QSizeF(width, height - top));
  drawSSA(cb, fonts, width, height - top);
  // REDS places lists immediately below target history, symbols, and data
  // blocks.
  drawTraffic(cb, fonts, QSizeF(width, height - top), float(pw) / width);
  drawRangeBearingLines(cb, fonts, width, height - top, float(pw) / width);
  drawCrdaGhosts(cb, fonts, {double(width), double(height - top)},
                 float(pw) / width);
  drawPreviewArea(cb, fonts, width, height - top);
  cb.disableScissor();
  cb.viewport({0, 0, pw, ph});
  const auto dcb = std::clamp(prefs_.charSize.dcb, 0, 2);
  if (showDcb_)
    dcb_.draw(cb, fonts.sizes[dcb], fonts.textures[dcb], width, height, pw, ph,
              top);
}
}  // namespace stars
