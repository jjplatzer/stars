#include "restrictions.h"

#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <numbers>

#include "../aviation/cifp.h"
#include "../math/proj.h"
#include "../radar/magnetic.h"

namespace stars {
namespace {
const QChar delta(0x80);
bool looksDms(const QString& token) {
  return token.contains('/') && (token.contains('N') || token.contains('S') ||
                                 token.endsWith('E') || token.endsWith('W'));
}
QString location(const QString& text, QPointF& point) {
  const auto fields = text.simplified().split(' ', Qt::SkipEmptyParts);
  if (fields.size() != 1 && fields.size() != 3) return "FORMAT";
  static const QRegularExpression dms(
      "^([0-9]{2})([0-5][0-9])([0-5][0-9])([NS])/"
      "([0-9]{3})([0-5][0-9])([0-5][0-9])([EW])$");
  const auto match = dms.match(fields[0]);
  if (match.hasMatch() && fields.size() == 1) {
    const auto degrees = [&](int i) {
      return match.captured(i).toInt() + match.captured(i + 1).toInt() / 60. +
             match.captured(i + 2).toInt() / 3600.;
    };
    const auto lat = degrees(1), lon = degrees(5);
    if (lat > 90 || lon > 180) return "ILL GEO LOC";
    point = {match.captured(8) == "W" ? -lon : lon,
             match.captured(4) == "S" ? -lat : lat};
    return {};
  }
  auto fix = aviation::lookupWaypoint(fields[0]);
  if (!fix) return "ILL GEO LOC";
  point = *fix;
  if (fields.size() == 1) return {};
  bool angleOK = false, nmOK = false;
  const double angle = fields[1].toDouble(&angleOK),
               nm = fields[2].toDouble(&nmOK);
  if (!angleOK || !nmOK ||
      !(angle >= 1 && angle <= 360 && nm >= .01 && nm <= 125))
    return "FORMAT";
  constexpr double rad = std::numbers::pi / 180;
  const double az = (angle - radar::magneticVariationAt(point)) * rad,
               d = nm / 3440.065, lat = point.y() * rad;
  const double dest = std::asin(std::sin(lat) * std::cos(d) +
                                std::cos(lat) * std::sin(d) * std::cos(az));
  point = {
      math::normalizeLongitude(
          point.x() + std::atan2(std::sin(az) * std::sin(d) * std::cos(lat),
                                 std::cos(d) - std::sin(lat) * std::sin(dest)) /
                          rad),
      dest / rad};
  return {};
}
QString label(const QStringList& fields, bool closed, Restriction& area) {
  QStringList lines;
  bool options = false, blink = false, shaded = false;
  int color = 1;
  for (auto token : fields) {
    if (token.startsWith(delta) || token.startsWith('+') ||
        token.startsWith('*')) {
      options = true;
      for (int i = 0; i < token.size(); ++i) {
        if (token[i] == delta)
          blink = true;
        else if (token[i] == '+' && closed)
          shaded = true;
        else if (token[i] == '*') {
          if (!closed || ++i >= token.size() || token[i] < '1' ||
              token[i] > '8')
            return "ILL COLOR";
          color = token[i].digitValue();
        } else
          return "FORMAT";
      }
    } else {
      if (options || lines.size() == 2) return "FORMAT";
      lines.append(token.left(32).replace('.', ' ').trimmed());
    }
  }
  if (lines.isEmpty()) return "FORMAT";
  area.text = lines;
  area.blink = blink;
  area.shaded = shaded;
  area.color = color;
  return {};
}
}  // namespace
void Restrictions::configure(const QString& site, bool dod) {
  static QMap<QString, std::shared_ptr<QMap<int, Restriction>>> sites;
  if (!site.isEmpty()) {
    auto& store = sites[site];
    if (!store) store = std::make_shared<QMap<int, Restriction>>();
    areas_ = store;
  }
  dod_ = dod;
}
void Restrictions::cancel() {
  draft_.reset();
  labelPending_ = false;
  moveId_ = 0;
  colorId_ = 0;
  colorChanged_ = false;
}
QString Restrictions::create(QPointF position) {
  for (int id = 1; id <= 100; ++id) {
    if (areas_->contains(id)) continue;
    draft_->serial = QUuid::createUuid().toString(QUuid::WithoutBraces);
    draft_->label = position;
    areas_->insert(id, *draft_);
    cancel();
    return {};
  }
  return "CAPACITY";
}
QString Restrictions::move(int id, QPointF position, bool blink) {
  auto it = areas_->find(id);
  if (it == areas_->end()) return "ILL GEO ID";
  const auto shift = position - it->label;
  auto moved = it->vertices;
  for (auto& p : moved) {
    p += shift;
    p.setX(math::normalizeLongitude(p.x()));
    if (std::abs(p.y()) > 90) return "ILL GEO LOC";
  }
  it->vertices = std::move(moved);
  it->label = position;
  it->blink |= blink;
  return {};
}
QString Restrictions::stepColor() {
  auto it = areas_->find(colorId_);
  if (it == areas_->end()) return "ILL GEO ID";
  it->color = it->color % 8 + 1;
  colorChanged_ = true;
  return {};
}
QString Restrictions::enter(QString input, std::optional<QPointF> click,
                            Preferences& prefs, bool& complete) {
  input = input.trimmed();
  complete = false;
  QPointF point;
  if (colorId_) {
    auto it = areas_->find(colorId_);
    if (it == areas_->end()) return "ILL GEO ID";
    if (!input.isEmpty() &&
        (input.size() != 1 || input.front() < '1' || input.front() > '8'))
      return "FORMAT";
    if (!input.isEmpty() || !colorChanged_)
      it->color = input.isEmpty() ? 1 : input.toInt();
    complete = true;
    cancel();
    return {};
  }
  if (moveId_) {
    if (!click) return "FORMAT";
    auto error = move(moveId_, *click, moveBlink_);
    if (error.isEmpty()) {
      prefs.restrictionDisplay.remove(areas_->value(moveId_).serial);
      complete = true;
      cancel();
    }
    return error;
  }
  if (draft_) {
    const bool poly = draft_->kind == 'P' || draft_->kind == 'A';
    if (!poly || labelPending_) {
      if (!click) {
        if (labelPending_) return "FORMAT";
        auto error = location(input, point);
        if (!error.isEmpty()) return error;
      }
      auto error = create(click.value_or(point));
      complete = error.isEmpty();
      return error;
    }
    if ((click && input.isEmpty()) ||
        (!click && location(input, point).isEmpty())) {
      if (draft_->vertices.size() >= 10) return "CAPACITY - MAX 10 VERTICES";
      draft_->vertices.append(click.value_or(point));
      return {};
    }
    if (!click && looksDms(input)) return "ILL GEO LOC";
    int minimum = draft_->kind == 'P' ? 3 : 2;
    if (draft_->vertices.size() < minimum)
      return QString("ILL FNCT - MIN %1 VERTICES").arg(minimum);
    auto error = label(input.split(' ', Qt::SkipEmptyParts),
                       draft_->kind == 'P', *draft_);
    if (!error.isEmpty()) return error;
    if (click) {
      error = create(*click);
      complete = error.isEmpty();
      return error;
    }
    labelPending_ = true;
    return {};
  }
  if (input.isEmpty()) return "FORMAT";
  if (input.front().isDigit()) {
    int n = 0;
    while (n < input.size() && input[n].isDigit()) ++n;
    const int id = input.left(n).toInt();
    const auto op = input.mid(n);
    if (id < 1 || id > 200 ||
        ((op == "DEL" || op.startsWith('*') ||
          (op.startsWith('T') && op != "T" && op != QString("T ") + delta)) &&
         id > 100))
      return "ILL GEO ID";
    auto it = areas_->find(id);
    if (it == areas_->end()) return "ILL GEO ID";
    auto settings = prefs.restrictionDisplay.value(it->serial);
    if (op == "T*") {
      if (!dod_) return "ILL FNCT";
      colorId_ = id;
      colorChanged_ = false;
      prefs.restrictionDisplay.remove(it->serial);
      return {};
    } else if (op == "DEL") {
      prefs.restrictionDisplay.remove(it->serial);
      areas_->erase(it);
    } else if (op.startsWith('*')) {
      auto where = op.mid(1).trimmed();
      const bool blink = where.startsWith(delta);
      if (blink) where = where.mid(1).trimmed();
      if (!where.isEmpty()) {
        auto error = location(where, point);
        if (!error.isEmpty()) return error;
      } else if (click)
        point = *click;
      else {
        moveId_ = id;
        moveBlink_ = blink;
        return {};
      }
      auto error = move(id, point, blink);
      if (!error.isEmpty()) return error;
      prefs.restrictionDisplay.remove(it->serial);
    } else if (op.startsWith('T') && op != "T" && op != QString("T ") + delta) {
      Restriction edited;
      auto error =
          label(op.mid(1).split(' ', Qt::SkipEmptyParts), false, edited);
      if (!error.isEmpty()) return error;
      if (!op.startsWith("T ")) edited.text[0].truncate(30);
      it->blink = edited.blink;
      it->text = edited.text;
      prefs.restrictionDisplay.remove(it->serial);
    } else {
      if (op == "I")
        settings.hidden = true;
      else if (op == "E") {
        settings.hidden = false;
        settings.hideText = false;
      } else if (op == "T" || op == QString("T ") + delta) {
        if (op != "T" && !settings.hideText) return "FORMAT";
        settings.hideText = !settings.hideText;
        settings.forceBlink = op != "T";
        if (settings.forceBlink) settings.stopBlink = false;
      } else if (op.isEmpty()) {
        if (!settings.hidden && !settings.hideText && !settings.stopBlink &&
            (it->blink || settings.forceBlink)) {
          settings.stopBlink = true;
          settings.forceBlink = false;
        } else {
          settings.hidden = !settings.hidden;
          if (!settings.hidden) settings.hideText = false;
        }
      } else
        return "FORMAT";
      prefs.restrictionDisplay[it->serial] = settings;
    }
    complete = true;
    return {};
  }
  Restriction area;
  area.kind = input.front();
  input = input.mid(1).trimmed();
  if (area.kind == 'A' || area.kind == 'P') {
    if (!input.isEmpty()) {
      auto error = location(input, point);
      if (!error.isEmpty()) return error;
      area.vertices.append(point);
    } else if (click)
      area.vertices.append(*click);
    draft_ = area;
    return {};
  }
  if (area.kind != 'C' && area.kind != 'G') return "FORMAT";
  if (area.kind == 'C') {
    static const QRegularExpression radius("^[0-9]+(?:\\.[0-9]+)?");
    const auto match = radius.match(input);
    if (!match.hasMatch()) return "FORMAT";
    area.radius = match.captured().toDouble();
    if (!(area.radius >= 1 && area.radius <= 125)) return "ILL RANGE";
    input = input.mid(match.capturedLength()).trimmed();
  }
  auto fields = input.split(' ', Qt::SkipEmptyParts);
  if (!fields.isEmpty() && looksDms(fields.back())) {
    auto error = location(fields.takeLast(), point);
    if (!error.isEmpty()) return error;
    click = point;
  } else
    for (int count : {3, 1}) {
      if (fields.size() > count &&
          location(fields.mid(fields.size() - count).join(' '), point)
              .isEmpty()) {
        fields.remove(fields.size() - count, count);
        click = point;
        break;
      }
    }
  auto error = label(fields, area.kind == 'C', area);
  if (!error.isEmpty()) return error;
  draft_ = area;
  if (click) {
    error = create(*click);
    complete = error.isEmpty();
  }
  return error;
}
}  // namespace stars

#include <QDateTime>

#include "../renderer/tess.h"
#include "stars.h"

namespace stars {
void STARSPane::restrictionCommand(std::optional<QPointF> point) {
  bool complete = false;
  response_ = restrictions_.enter(input_, point, prefs_, complete);
  if (response_.isEmpty()) input_.clear();
  if (complete) mode_ = Mode::None;
}
void STARSPane::drawRestrictions(renderer::CmdBuffer& cb, const Fonts& fonts,
                                 QSizeF size, float dpi) const {
  using namespace renderer;
  if (restrictions_.areas().isEmpty() && !restrictions_.draft()) return;
  const auto level = prefs_.brightness.videoGroupB;
  if (!level) return;
  cb.loadProjectionMatrix(screenOrtho(size.width(), size.height()));
  cb.lineWidth(std::max(1.f, dpi));
  const auto shader = dpi > 1.5 ? DrawMode::StippleRestrictionAreaHighDPI
                                : DrawMode::StippleRestrictionArea;
  const auto pixel = [&](QPointF p) {
    const auto q = screen(p, size);
    return PointVertex{float(q.x()), float(q.y())};
  };
  TextBuilder labels;
  const auto& font = fonts.sizes[prefs_.charSize.tools];
  const bool dim = QDateTime::currentMSecsSinceEpoch() / 500 % 2;
  const auto draw = [&](int id, const Restriction& area) {
    const auto settings = prefs_.restrictionDisplay.value(area.serial);
    if (settings.hidden) return;
    cb.setRGB(colors::brightness(palette_.RestrictionAreaGeom[area.color - 1],
                                 level));
    LinesBuilder lines;
    TrianglesBuilder fill;
    const auto anchor = pixel(area.label);
    if (area.kind == 'C' && id) {
      const float radius = float(
          area.radius * std::min(size.width(), size.height()) / (2 * range_));
      lines.addCircle(anchor, radius, 96);
      if (area.shaded) fill.addCircle(anchor, radius, 96);
    } else if (!area.vertices.isEmpty()) {
      std::vector<PointVertex> points;
      for (auto vertex : area.vertices) points.push_back(pixel(vertex));
      if (area.kind == 'P' && points.size() >= 3) {
        lines.addLineLoop(points);
        if (area.shaded) fill = tessellate(std::span(&points, 1));
      } else
        lines.addLineStrip(points);
      if (!id && !restrictions_.waitingLabel() && input_.isEmpty())
        lines.addLine(points.back(), {float(cursor_.x()), float(cursor_.y())});
    }
    fill.generateCommands(cb, shader);
    lines.generateCommands(cb);
    if (!id) return;
    QString text = QString("[%1]").arg(id);
    if (!settings.hideText) {
      if (!area.text.value(0).isEmpty()) text += ' ' + area.text[0];
      if (!area.text.value(1).isEmpty()) text += '\n' + area.text[1];
    }
    const auto width = std::max(text.section('\n', 0, 0).size(),
                                text.section('\n', 1).size()) *
                       font.charSize().x;
    const bool blink =
        (area.blink || settings.forceBlink) && !settings.stopBlink && dim;
    labels.addText(font, text.toStdString(),
                   {anchor.x - .5f * width, anchor.y - .5f * font.lineHeight},
                   {colors::brightness(palette_.RestrictionAreaText,
                                       blink ? level / 2 : level)
                        .rgba()});
  };
  const auto& areas = restrictions_.areas();
  for (auto it = areas.cbegin(); it != areas.cend(); ++it) draw(it.key(), *it);
  if (restrictions_.draft()) draw(0, *restrictions_.draft());
  labels.generateCommands(cb, fonts.textures[prefs_.charSize.tools]);
}
}  // namespace stars
