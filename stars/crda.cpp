#include "crda.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include <numbers>

#include "../radar/magnetic.h"
#include "stars.h"

namespace stars {
namespace {
constexpr double radians = std::numbers::pi / 180.;
QStringList strings(const QJsonValue& array) {
  QStringList result;
  for (const auto& v : array.toArray()) result.append(v.toString().toUpper());
  return result;
}
Crda::Runway runway(const QJsonObject& config) {
  Crda::Runway r;
  auto point = [&](const char* name) {
    const auto xy = config[name].toObject();
    return QPointF(xy["lon"].toDouble(), xy["lat"].toDouble());
  };
  r.id = config["runwayId"].toString();
  r.target = point("targetReferencePoint");
  r.image = point("imageReferencePoint");
  r.heading = config["targetReferenceLineHeading"].toDouble();
  r.imageHeading = config["imageReferenceLineHeading"].toDouble();
  r.targetLength = config["targetReferenceLineLength"].toDouble();
  r.imageLength = config["imageReferenceLineLength"].toDouble();
  r.nearDistance = config["nearSideDistance"].toDouble();
  r.length = config["regionLength"].toDouble();
  r.nearWidth = config["nearSideHalfWidth"].toDouble();
  r.farWidth = config["farSideHalfWidth"].toDouble();
  r.tieOffset = config["tieModeOffset"].toDouble();
  r.headingTolerance = config["headingTolerance"].toDouble();
  r.descentDistance = config["descentPointDistance"].toDouble();
  r.descentAltitude = config["descentPointAltitude"].toDouble();
  r.targetAltitude = config["targetReferencePointAltitude"].toDouble();
  r.above = config["abovePathTolerance"].toDouble();
  r.below = config["belowPathTolerance"].toDouble();
  r.primary = strings(config["scratchpadPatterns"]);
  r.secondary = strings(config["secondaryScratchpadPatterns"]);
  auto direction = config["defaultLeaderDirection"];
  const auto value = direction.toVariant().toString().toUpper();
  r.direction = leaderDirections.indexOf(value);
  if (r.direction < 0 && value.size() == 1)
    r.direction = QString("89632147").indexOf(value);
  return r;
}
bool matches(const QStringList& patterns, const QString& scratch) {
  return patterns.isEmpty() ||
         (!scratch.isEmpty() && std::any_of(patterns.begin(), patterns.end(),
                                            [&](const QString& pattern) {
                                              return scratch.contains(
                                                  pattern, Qt::CaseInsensitive);
                                            }));
}
}  // namespace
void Crda::configure(const QJsonArray& adaptation) {
  pairs.clear();
  inhibited = false;
  forceAll_ = false;
  for (const auto& v : adaptation) {
    const auto obj = v.toObject();
    Pair pair;
    pair.airport = obj["airportId"].toString();
    pair.tie = obj["positionSymbolTie"].toString();
    pair.stagger = obj["positionSymbolStagger"].toString();
    pair.runway = {runway(obj["masterRunway"].toObject()),
                   runway(obj["slaveRunway"].toObject())};
    pairs.append(pair);
  }
}
bool Crda::enabled() const {
  return !inhibited && std::any_of(pairs.cbegin(), pairs.cend(),
                                   [](const Pair& p) { return p.mode != 'D'; });
}
double Crda::scaleAt(double lat) {
  return (59.998660261511354 / 60.) * std::cos(lat * radians);
}
QPointF Crda::offset(QPointF from, double nm, double heading, double scale,
                     double variation) {
  const double a = (heading - variation) * radians;
  return from + QPointF(std::sin(a) / scale, std::cos(a)) * (nm / 60.);
}
std::array<QPointF, 4> Crda::boundary(const Runway& r, double variation) {
  const double scale = scaleAt(r.target.y()), out = r.heading + 180;
  const auto near = offset(r.target, r.nearDistance, out, scale, variation),
             far = offset(near, r.length, out, scale, variation);
  return {offset(near, r.nearWidth, out - 90, scale, variation),
          offset(far, r.farWidth, out - 90, scale, variation),
          offset(far, r.farWidth, out + 90, scale, variation),
          offset(near, r.nearWidth, out + 90, scale, variation)};
}
QString Crda::command(const QString& entry, QString& output) {
  if (pairs.isEmpty()) return "ILL FNCT";
  if (entry == "N") {
    inhibited = !inhibited;
    if (inhibited)
      for (auto& pair : pairs) {
        pair.mode = 'D';
        pair.control = {};
      }
    return {};
  }
  if (inhibited) return "ILL FNCT - CRDA OFF";
  if (entry == "N*ALL") {
    forceAll_ = !forceAll_;
    return {};
  }
  static const QRegularExpression rpc("^N([A-Z0-9]{3})([1-9])([TSD])$");
  if (const auto match = rpc.match(entry); match.hasMatch()) {
    const int idx = match.captured(2).toInt() - 1;
    if (idx >= pairs.size() || pairs[idx].airport != match.captured(1))
      return "FORMAT";
    auto& pair = pairs[idx];
    const auto mode = match.captured(3)[0];
    if (mode == 'D')
      pair.control = {};
    else if (pair.mode == 'D')
      pair.control[0].enabled = true;
    pair.mode = mode;
    output = pair.airport + ' ' + pair.runway[0].id + '/' + pair.runway[1].id +
             (mode == 'D' ? " INHIBITED" : " ENABLED");
    return {};
  }
  static const QRegularExpression operation(
      "^N(L?)([A-Z0-9]{3})((?:0[1-9]|[12][0-9]|3[0-6])[LRC]?)([EI]?| "
      "[BL]|[1-9])$");
  const auto match = operation.match(entry);
  if (!match.hasMatch()) return "FORMAT";
  const bool leader = !match.captured(1).isEmpty();
  const auto action = match.captured(4);
  const bool overlay = action.startsWith(' ');
  const int direction = QString("89632147").indexOf(action);
  if (leader != (action.size() == 1 && action[0].isDigit()) ||
      (leader && direction < 0))
    return "FORMAT";
  for (auto& pair : pairs) {
    if (pair.airport != match.captured(2) ||
        ((leader || overlay) && pair.mode == 'D'))
      continue;
    for (int side = 0; side < 2; ++side) {
      if (pair.runway[side].id != match.captured(3)) continue;
      auto& c = pair.control[side];
      if (leader)
        c.direction = direction;
      else if (action == " B")
        c.boundary = !c.boundary;
      else if (action == " L")
        c.lines = !c.lines;
      else {
        c.enabled = action.isEmpty() ? !c.enabled : action == "E";
        output = pair.airport + ' ' + pair.runway[side].id +
                 (c.enabled ? " GHOSTING ENABLED" : " GHOSTING DISABLED");
        if (c.enabled && pair.mode == 'D') pair.mode = 'S';
        if (!pair.control[0].enabled && !pair.control[1].enabled) {
          pair.mode = 'D';
          pair.control = {};
        }
      }
      return {};
    }
  }
  return "FORMAT";
}
std::optional<Crda::Ghost> Crda::ghost(const net::RadarTrack& t,
                                       double variation,
                                       const Selection& selection) const {
  const auto& track = t.report;
  if (inhibited || !track.location || !track.plan || !track.altitude ||
      !track.velocity || track.velocity->isNull() || track.speed > 350)
    return {};
  const QPointF pos = *track.location;
  const double course =
      std::atan2(track.velocity->x(), track.velocity->y()) / radians +
      variation;
  double best = std::numeric_limits<double>::infinity();
  std::optional<Ghost> result;
  for (int i = 0; i < pairs.size(); ++i) {
    const auto& pair = pairs[i];
    if (pair.mode == 'D') continue;
    for (int side = 0; side < 2; ++side) {
      if (!pair.control[side].enabled && !selection.force) continue;
      const auto& r = pair.runway[side];
      const auto points = boundary(r, variation);
      bool inside = false;
      for (int j = 0, k = 3; j < 4; k = j++) {
        const auto a = points[j], b = points[k];
        if ((a.y() > pos.y()) != (b.y() > pos.y()) &&
            pos.x() <
                (b.x() - a.x()) * (pos.y() - a.y()) / (b.y() - a.y()) + a.x())
          inside = !inside;
      }
      if (!inside) continue;
      const double delta = std::abs(std::remainder(course - r.heading, 360.));
      if (!selection.qualify && !forceAll_) {
        const int alt = qRound(*track.altitude / 100.);
        const double distance = radar::distanceNM(r.target, pos);
        const double path = distance >= r.descentDistance
                                ? r.descentAltitude
                                : std::lerp(r.targetAltitude, r.descentAltitude,
                                            distance / r.descentDistance);
        if (delta > r.headingTolerance || alt > r.descentAltitude + r.above ||
            alt < r.targetAltitude - r.below || alt < path - r.below ||
            (distance < r.descentDistance && alt > path + r.above) ||
            !matches(r.primary, track.plan->primary) ||
            !matches(r.secondary, track.plan->secondary))
          continue;
      }
      if (delta < best) {
        best = delta;
        const auto scale = scaleAt(r.target.y());
        const auto dx =
                       math::normalizeLongitude(pos.x() - r.target.x()) * scale,
                   dy = pos.y() - r.target.y();
        const auto bearing = std::atan2(dx, dy) / radians + variation +
                             r.imageHeading - r.heading;
        const double distance =
            60 * std::hypot(dx, dy) + (pair.mode == 'T' ? r.tieOffset : 0);
        result = Ghost{offset(r.image, distance, bearing, scale, variation), i,
                       side, pair.mode == 'T' ? pair.tie : pair.stagger};
      }
      break;
    }
  }
  return result;
}
}  // namespace stars

namespace stars {
bool STARSPane::crdaCommand() {
  if (!input_.startsWith('N')) return false;
  QString output;
  response_ = crda_.command(input_, output);
  if (response_.isEmpty()) {
    if (input_.size() == 6 && (input_.endsWith('T') || input_.endsWith('S')))
      prefs_.lists["TN"].visible = true;
    ghostHits_.clear();
    for (auto& selection : ghostSelections_) {
      if (crda_.inhibited ||
          (selection.pair >= 0 && crda_.pairs[selection.pair].mode == 'D')) {
        selection.suppressed = false;
        selection.full = true;
        selection.pair = -1;
      }
    }
    mode_ = Mode::None;
    input_.clear();
    response_ = output;
  }
  return true;
}
bool STARSPane::crdaSlew(QPointF mouse, const QString& key) {
  const bool multi = mode_ == Mode::Multi && (input_ == "N" || input_ == "N*");
  if (!multi && (mode_ != Mode::None || !input_.isEmpty())) return false;
  QString hit;
  double nearest = std::numeric_limits<double>::infinity();
  for (auto it = ghostHits_.cbegin(); it != ghostHits_.cend(); ++it) {
    const double distance =
        std::hypot(mouse.x() - it->center.x(), mouse.y() - it->center.y());
    const bool label = std::any_of(it->labels.begin(), it->labels.end(),
                                   [&](QRectF r) { return r.contains(mouse); });
    if ((label || (multi && distance < 20)) && distance < nearest) {
      nearest = distance;
      hit = it.key();
    }
  }
  if (!multi && hit.isEmpty()) return false;
  const auto& tracks = traffic_.tracks();
  auto t = tracks.constFind(hit.isEmpty() ? key : hit);
  if (crda_.pairs.isEmpty() || crda_.inhibited)
    response_ = crda_.inhibited ? "ILL FNCT - CRDA OFF" : "ILL FNCT";
  else if (t == tracks.cend())
    response_ = "NO TRK";
  else if (!t->report.plan)
    response_ = "ILL TRK";
  else {
    const auto& plan = *t->report.plan;
    auto& selection = ghostSelections_[t.key()];
    selection.generation = t->firstMrtMs;
    selection.plan = plan.sfpn;
    selection.acid = plan.acid;
    response_.clear();
    if (!hit.isEmpty()) {
      selection.pair = ghostHits_.value(hit).pair;
      if (input_ == "N*") {
        auto type = plan.aircraftType;
        if (!type.isEmpty() && !plan.equipmentSuffix.isEmpty())
          type += '/' + plan.equipmentSuffix;
        QStringList fields{
            plan.acid, type,
            t->report.beacon.isEmpty() ? plan.assignedBeacon : t->report.beacon,
            t->owner};
        fields.removeAll({});
        response_ = fields.join(' ');
      } else if (multi) {
        selection.suppressed = true;
        selection.force = false;
        ghostHits_.remove(hit);
      } else
        selection.full = !selection.full;
    } else if (input_ == "N*")
      selection.qualify = !selection.qualify;
    else if (ghostHits_.contains(key))
      response_ = "ILL TRK";
    else {
      selection.force = true;
      selection.suppressed = false;
    }
    if (multi && response_.isEmpty()) {
      mode_ = Mode::None;
      input_.clear();
    }
  }
  return true;
}
void STARSPane::drawCrdaDiagrams(renderer::CmdBuffer& cb, QSizeF size,
                                 float dpi) {
  using namespace renderer;
  if (!crda_.enabled()) return;
  cb.loadProjectionMatrix(screenOrtho(size.width(), size.height()));
  cb.lineWidth(std::max(1.f, dpi));
  const double mag = radar::magneticVariationAt(center());
  const auto pixel = [&](QPointF p) {
    const auto q = screen(p, size);
    return PointVertex{float(q.x()), float(q.y())};
  };
  LinesBuilder overlays;
  for (const auto& pair : crda_.pairs) {
    if (pair.mode == 'D') continue;
    for (int side = 0; side < 2; ++side) {
      const auto& r = pair.runway[side];
      const auto& c = pair.control[side];
      if (c.boundary) {
        const auto ring = Crda::boundary(r, mag);
        std::array<PointVertex, 4> points;
        std::transform(ring.begin(), ring.end(), points.begin(), pixel);
        overlays.addLineLoop(points);
      }
      if (c.lines) {
        const auto scale = Crda::scaleAt(r.target.y());
        overlays.addLine(pixel(r.target),
                         pixel(Crda::offset(r.target, r.targetLength,
                                            r.heading + 180, scale, mag)));
        overlays.addLine(pixel(r.image),
                         pixel(Crda::offset(r.image, r.imageLength,
                                            r.imageHeading + 180, scale, mag)));
        overlays.addCircle(pixel(r.target), 2.25f, 16);
        overlays.addCircle(pixel(r.image), 2.25f, 16);
      }
    }
  }
  if (prefs_.brightness.lines) {
    cb.setRGB(
        colors::brightness(palette_.GhostDatablock, prefs_.brightness.lines));
    overlays.generateCommands(cb);
  }
}

void STARSPane::drawCrdaGhosts(renderer::CmdBuffer& cb, const Fonts& fonts,
                               QSizeF size, float dpi) {
  using namespace renderer;
  ghostHits_.clear();
  const auto& tracks = traffic_.tracks();
  ghostSelections_.removeIf([&](auto it) {
    auto t = tracks.constFind(it.key());
    return t == tracks.cend() || !t->report.plan ||
           it->generation != t->firstMrtMs ||
           it->plan != t->report.plan->sfpn || it->acid != t->report.plan->acid;
  });
  if (!crda_.enabled()) return;
  cb.loadProjectionMatrix(screenOrtho(size.width(), size.height()));
  cb.lineWidth(std::max(1.f, dpi));
  const double mag = radar::magneticVariationAt(center());
  const auto pixel = [&](QPointF p) {
    const auto q = screen(p, size);
    return PointVertex{float(q.x()), float(q.y())};
  };
  LinesBuilder leaders;
  const auto level = colors::brightness(palette_.GhostDatablock,
                                        prefs_.brightness.otherTracks);
  TextBuilder symbols, outlines, labels;
  const auto ps = prefs_.charSize.positionSymbols,
             db = prefs_.charSize.datablocks;
  const auto& font = fonts.sizes[db];
  const float length =
      std::array{0, 17, 32, 47, 62, 77, 114, 152}[prefs_.leaderLength];
  for (auto it = tracks.cbegin(); it != tracks.cend(); ++it) {
    const auto selection = ghostSelections_.value(it.key());
    const auto g = crda_.ghost(*it, mag, selection);
    if (!g) {
      if (selection.suppressed || !selection.full) {
        auto& old = ghostSelections_[it.key()];
        old.suppressed = false;
        old.full = true;
        old.pair = -1;
      }
      continue;
    }
    if (auto old = ghostSelections_.find(it.key());
        old != ghostSelections_.end())
      old->pair = g->pair;
    if (selection.suppressed || !prefs_.brightness.otherTracks ||
        g->symbol.isEmpty())
      continue;
    const auto& pair = crda_.pairs[g->pair];
    const auto center = pixel(g->position);
    const auto glyph = [&](TextBuilder& b, const FontSize& f, RGB color) {
      if (const auto* ch = f.glyph(g->symbol.front().unicode()))
        b.addText(f, g->symbol.toStdString(),
                  {center.x + .5f - ch->width * .5f - ch->offsetX,
                   center.y - .5f - ch->height * .5f - ch->offsetY},
                  {color.rgba()});
    };
    glyph(outlines, fonts.outlines[ps], palette_.PositionSymbolOutline);
    glyph(symbols, fonts.sizes[ps], level);
    int direction = pair.control[g->side].direction;
    if (direction < 0) direction = pair.runway[g->side].direction;
    if (direction < 0 || direction > 7) direction = prefs_.leaderDirection;
    const float dx = std::sin(direction * std::numbers::pi / 4),
                dy = -std::cos(direction * std::numbers::pi / 4);
    const PointVertex end{center.x + length * dx, center.y + length * dy};
    if (length > 7)
      leaders.addLine({center.x + 7 * dx, center.y + 7 * dy}, end);
    auto& hit = ghostHits_[it.key()];
    hit.center = {center.x, center.y};
    hit.pair = g->pair;
    const auto& plan = *it->report.plan;
    const auto speed =
        QString::number(qRound(it->report.speed / 10.)).rightJustified(2, '0');
    QStringList rows;
    if (selection.full && !plan.acid.isEmpty())
      rows = {plan.acid, speed};
    else
      rows = {speed + plan.category.left(1)};
    for (int i = 0; i < rows.size(); ++i) {
      float width = rows[i].size() * font.charSize().x;
      const float x = direction >= 4 ? end.x - 4 - width : end.x + 4,
                  y = end.y + (i - .5f) * font.lineHeight;
      labels.addText(font, rows[i].toStdString(), {x, y}, {level.rgba()});
      hit.labels.append(QRectF(x - 2, y - 2, width + 4, font.lineHeight + 4));
    }
  }
  cb.setRGB(level);
  leaders.generateCommands(cb);
  outlines.generateCommands(cb, fonts.outlineTextures[ps]);
  symbols.generateCommands(cb, fonts.textures[ps]);
  labels.generateCommands(cb, fonts.textures[db]);
}
}  // namespace stars
