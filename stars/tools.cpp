#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "datablock.h"
#include "stars.h"

namespace stars {
void STARSPane::drawRangeRings(renderer::CmdBuffer& cb, int width, int height,
                               float dpi) const {
  if (!prefs_.brightness.rangeRings || prefs_.ringSpacing <= 0 || range_ <= 0)
    return;
  const auto origin = prefs_.useUserRingCenter
                          ? prefs_.userRingCenter.value_or(defaultCenter_)
                          : defaultCenter_;
  const auto center = screen(origin, QSizeF(width, height));
  const float spacing =
      prefs_.ringSpacing * std::min(width, height) / (2 * range_);
  renderer::LinesBuilder rings;
  for (int i = 1; i <= 39; ++i)
    rings.addCircle({float(center.x()), float(center.y())}, i * spacing, 360);
  const auto color =
      colors::brightness(palette_.RangeRing, prefs_.brightness.rangeRings);
  cb.loadProjectionMatrix(renderer::screenOrtho(width, height));
  cb.setRGB(color);
  cb.lineWidth(std::max(1.f, dpi));
  rings.generateCommands(cb);
}

void STARSPane::drawCompass(renderer::CmdBuffer& cb, const Fonts& fonts,
                            int width, int height, int pixelWidth,
                            int pixelHeight) const {
  using namespace renderer;
  const int top = dcbHeight();
  if (height <= top || prefs_.brightness.compass == 0) return;

  // The magnetic projection already puts north at screen-up. The DCB clips
  // the top edge but does not displace the projection's center.
  const float cx = width * .5f, cy = height * .5f;
  const int size = std::clamp(prefs_.charSize.tools, 0, 5);
  const auto& font = fonts.sizes[size];
  const auto color =
      colors::brightness(palette_.Compass, prefs_.brightness.compass);
  LinesBuilder ticks;
  TextBuilder labels;
  enum Edge { Left, Right, Top, Bottom };
  for (int heading = 5; heading <= 360; heading += 5) {
    const double angle = heading * std::numbers::pi / 180;
    const float dx = std::sin(angle), dy = -std::cos(angle);
    float distance = std::numeric_limits<float>::infinity();
    Edge edge = Left;
    auto consider = [&](float t, Edge candidate) {
      const float x = cx + t * dx, y = cy + t * dy;
      if (t >= 0 && t < distance && x >= -.125f && x <= width + .125f &&
          y >= top - .125f && y <= height + .125f) {
        distance = t;
        edge = candidate;
      }
    };
    if (dx > 1e-6f) consider((width - cx) / dx, Right);
    if (dx < -1e-6f) consider(-cx / dx, Left);
    if (dy > 1e-6f) consider((height - cy) / dy, Bottom);
    if (dy < -1e-6f) consider((top - cy) / dy, Top);
    if (!std::isfinite(distance)) continue;

    const float x = cx + distance * dx, y = cy + distance * dy;
    ticks.addLine({x, y}, {x - 10 * dx, y - 10 * dy});
    if (heading % 10) continue;
    const char label[] = {char('0' + heading / 100),
                          char('0' + heading / 10 % 10), '0', 0};
    float advance = 0;
    for (char digit : std::string_view(label))
      if (auto glyph = font.glyph(digit)) advance += glyph->advance;
    PointVertex origin{x - 14 * dx, y - 14 * dy};
    switch (edge) {
      case Right:
        origin.x -= advance;
        [[fallthrough]];
      case Left:
        origin.y -= font.lineHeight * .5f;
        break;
      case Bottom:
        origin.y -= font.lineHeight;
        [[fallthrough]];
      case Top:
        origin.x -= advance * .5f;
        break;
    }
    labels.addText(font, label, origin, {color.rgba(), {}});
  }
  cb.scissor(
      {0, 0, pixelWidth,
       pixelHeight - int(std::lround(double(top) * pixelHeight / height))});
  cb.loadProjectionMatrix(screenOrtho(width, height));
  cb.setRGB(color);
  cb.lineWidth(std::max(1.f, float(pixelWidth) / width));
  ticks.generateCommands(cb);
  labels.generateCommands(cb, fonts.textures[size]);
  cb.scissor({0, 0, pixelWidth, pixelHeight});
}

namespace {
QPointF labelAnchor(QPointF end, QPointF start, double width, double height) {
  double enter = 0, exit = std::numeric_limits<double>::infinity();
  const QPointF dir = start - end;
  auto clip = [&](double origin, double delta, double limit) {
    if (std::abs(delta) < 1e-6) return origin >= 0 && origin <= limit;
    double a = -origin / delta, b = (limit - origin) / delta;
    if (a > b) std::swap(a, b);
    enter = std::max(enter, a);
    exit = std::min(exit, b);
    return enter <= exit;
  };
  return clip(end.x(), dir.x(), width) && clip(end.y(), dir.y(), height)
             ? end + enter * dir
             : end;
}
}  // namespace
void STARSPane::drawRangeBearingLines(renderer::CmdBuffer& cb,
                                      const Fonts& fonts, int width, int height,
                                      float dpi) const {
  if ((lines_.empty() && !start_) || !prefs_.brightness.lines) return;
  using namespace renderer;
  const QSizeF size(width, height);
  const auto transform = transformations(size);
  const double variation = transform.rotationRadians;
  const auto& font = fonts.sizes.at(prefs_.charSize.tools);
  const auto color =
      colors::brightness(palette_.RangeBearingLine, prefs_.brightness.lines);
  LinesBuilder lines;
  TextBuilder text;
  int id = 0;
  auto draw = [&](const Endpoint& first, const Endpoint& second) {
    const auto location = [&](const Endpoint& e) {
      return e.track.isEmpty() ? e.location : tracks_[e.track].location;
    };
    const auto a = location(first), b = location(second);
    const double lon = math::normalizeLongitude(b.x() - a.x()),
                 lat = b.y() - a.y();
    const double bearing = std::atan2(lon * longitudeScale_, lat) + variation;
    const int degrees =
        qRound(std::fmod(bearing * 180 / std::numbers::pi + 360., 360.));
    const double nm = radar::distanceNM(a, b);
    QString label =
        QString(" %1/%2")
            .arg(degrees ? degrees : 360, 3, 10, QChar('0'))
            .arg(nm > 999.99 ? "******" : QString::number(nm, 'f', 2));
    if (first.track.isEmpty() != second.track.isEmpty()) {
      const auto speed =
          tracks_[first.track.isEmpty() ? second.track : first.track]
              .groundSpeed;
      if (speed > 0) {
        const auto minutes = 60 * nm / speed;
        label += '/' + (minutes > 99 ? QString("**")
                                     : QString::number(qRound(minutes)));
      }
    }
    label += '-' + QString::number(++id);
    const auto p = transform.screen(a), q = transform.screen(b);
    lines.addLine({float(p.x()), float(p.y())}, {float(q.x()), float(q.y())});
    const auto chars = label.toStdString();
    int advance = 0;
    for (char c : chars)
      if (auto glyph = font.glyph(c)) advance += glyph->advance;
    auto anchor = labelAnchor(q, p, width, height);
    if (q.x() > width - advance)
      anchor.ry() += p.y() < q.y() ? 4 : -int(font.lineHeight) - 4;
    text.addText(
        font, chars,
        {float(
             std::clamp(anchor.x(), 0., double(std::max(0, width - advance)))),
         float(std::clamp(anchor.y(), 0.,
                          double(std::max(0, height - int(font.lineHeight)))))},
        {color.rgba(), {}});
  };
  for (const auto& line : lines_) draw(line[0], line[1]);
  if (start_ && QRectF(QPointF(), size).contains(cursor_))
    draw(*start_, {transform.geographic(cursor_), {}});
  cb.loadProjectionMatrix(screenOrtho(width, height));
  cb.setRGB(color);
  cb.lineWidth(std::max(1.f, dpi));
  lines.generateCommands(cb);
  text.generateCommands(cb, fonts.textures.at(prefs_.charSize.tools));
}

namespace {
renderer::PointVertex vertex(QPointF p) { return {float(p.x()), float(p.y())}; }

// Leave the bitmap mileage label's ink and a two-pixel margin clear on both
// cone sides.
void coneSide(renderer::ColoredLinesBuilder& lines, QPointF a, QPointF b,
              QRectF gap, renderer::RGB color) {
  const auto delta = b - a;
  double first = 0, last = 1;
  const auto slab = [&](double origin, double step, double low, double high) {
    if (step == 0) return origin >= low && origin <= high;
    auto t1 = (low - origin) / step, t2 = (high - origin) / step;
    if (t2 < t1) std::swap(t1, t2);
    first = std::max(first, t1);
    last = std::min(last, t2);
    return first <= last;
  };
  if (gap.isEmpty() || !slab(a.x(), delta.x(), gap.left(), gap.right()) ||
      !slab(a.y(), delta.y(), gap.top(), gap.bottom())) {
    lines.addLineRGB(vertex(a), vertex(b), color);
    return;
  }
  if (first > 0) lines.addLineRGB(vertex(a), vertex(a + delta * first), color);
  if (last < 1) lines.addLineRGB(vertex(a + delta * last), vertex(b), color);
}
}  // namespace
void STARSPane::drawAtpa(renderer::CmdBuffer& cb, const Fonts& fonts,
                         QSizeF size, float dpi) const {
  const auto& pairs = atpa_.pairs();
  if (!atpa_.hasGraphics() || !prefs_.brightness.lines) return;
  renderer::ColoredLinesBuilder lines;
  renderer::TextBuilder text;
  const auto projection = transformations(size);
  const double scale = std::min(size.width(), size.height()) / (2 * range_);
  const int fontSize = prefs_.charSize.datablocks;
  const auto& font = fonts.sizes.at(fontSize);
  const auto& tracks = traffic_.tracks();
  for (const auto& t : tracks) {
    const auto d = atpa_.display(t, prefs_);
    if (!d.ring && !d.manualCone && d.cone == AtpaStatus::None) continue;
    const auto center = projection.screen(*t.report.location);
    const auto base = colors::brightness(palette_.TerminalProximityAlert,
                                         prefs_.brightness.lines);
    if (d.ring > 0) {
      const double radius = d.ring * scale;
      const auto point = [&](int i) {
        const double angle = math::toRadians(i);
        return vertex(center +
                      QPointF(std::sin(angle), std::cos(angle)) * radius);
      };
      for (int i = 1; i <= 360; ++i)
        lines.addLineRGB(point(i - 1), point(i), base);
      if (d.mileage) {
        const auto label = QString::number(d.ring).toStdString();
        const int dir = trackDatablock(t).direction;
        const double angle = math::toRadians(dir * 45.);
        auto pos = center + QPointF(-std::sin(angle), std::cos(angle)) * radius;
        const auto bounds = font.measure(label);
        text.addText(font, label,
                     vertex(pos - QPointF(bounds.x, bounds.y) * .5),
                     {base.rgba()});
      }
    }
    QPointF delta;
    double nm = d.manualCone;
    if (d.cone != AtpaStatus::None) {
      const auto pair = pairs.constFind(t.report.number);
      const auto lead = tracks.constFind(pair->lead);
      if (lead == tracks.cend()) continue;
      delta = projection.screen(*lead->report.location) - center;
      nm = pair->minimum;
    } else if (nm > 0 && t.report.velocity) {
      const auto position = *t.report.location, speed = *t.report.velocity;
      const auto offset =
          QPointF(speed.x() / radar::longitudeScaleFactorForLat(position.y()),
                  speed.y()) /
          60;
      delta = projection.screen(position + offset) - center;
    } else
      continue;
    const double norm = std::hypot(delta.x(), delta.y());
    if (norm <= 0) continue;
    const auto step = delta / norm;
    const auto tip = center + step * (scale * nm);
    const QPointF side(-5 * step.y(), 5 * step.x());
    const auto baseColor = d.cone == AtpaStatus::Warning ? palette_.ATPAWarning
                           : d.cone == AtpaStatus::Alert
                               ? palette_.ATPAAlert
                               : palette_.TerminalProximityAlert;
    const auto color = colors::brightness(baseColor, prefs_.brightness.lines);
    QRectF gap;
    if (d.mileage) {
      const auto label = QString::number(nm);
      QRectF ink;
      double x = 0;
      for (const auto ch : label)
        if (auto g = font.glyph(ch.unicode())) {
          ink = ink.united(
              QRectF(x + g->offsetX, g->offsetY, g->width, g->height));
          x += g->advance;
        }
      const auto origin = (center + tip) / 2 - ink.center();
      text.addText(font, label.toStdString(), vertex(origin), {color.rgba()});
      gap = ink.translated(origin).adjusted(-2, -2, 2, 2);
    }
    coneSide(lines, center, tip + side, gap, color);
    coneSide(lines, center, tip - side, gap, color);
    lines.addLineRGB(vertex(tip + side), vertex(tip - side), color);
  }
  cb.loadProjectionMatrix(renderer::screenOrtho(size.width(), size.height()));
  cb.lineWidth(std::max(1.f, dpi));
  lines.generateCommands(cb);
  text.generateCommands(cb, fonts.textures[fontSize]);
}
}  // namespace stars
