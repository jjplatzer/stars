#include <cmath>

#include "stars.h"

namespace stars {
namespace {
QString degrees(double value) {
  const int seconds = qRound(std::abs(value) * 3600);
  return QString("%1 %2 %3")
      .arg(seconds / 3600, 2, 10, QChar('0'))
      .arg(seconds / 60 % 60, 2, 10, QChar('0'))
      .arg(seconds % 60, 2, 10, QChar('0'));
}
}  // namespace
bool STARSPane::automaticPtl(const net::RadarTrack& t) const {
  if (!t.report.plan) return false;
  const bool own =
      !tcp_.isEmpty() && (t.owner == tcp_ || t.formerOwner == tcp_ ||
                          (t.pending && t.report.plan->cps == tcp_));
  return prefs_.ptlAll || (prefs_.ptlOwn && own);
}
void STARSPane::updateDwell(QSizeF size) {
  const auto& tracks = traffic_.tracks();
  if (!prefs_.dwell || cursor_.y() < dcbHeight() ||
      !QRectF({}, size).contains(cursor_))
    dwellTrack_.clear();
  else {
    const auto it = tracks.constFind(dwellTrack_);
    if (prefs_.dwell != 2 || cursor_ != dwellCursor_ || it == tracks.cend() ||
        it->firstMrtMs != dwellGeneration_) {
      dwellTrack_ = closestTrack(cursor_, size);
      const auto target = tracks.constFind(dwellTrack_);
      if (target != tracks.cend()) dwellGeneration_ = target->firstMrtMs;
    }
  }
  dwellCursor_ = cursor_;
}
bool STARSPane::toolSlew(const QString& key, QPointF point) {
  if (mode_ == Mode::Min) {
    if (!input_.isEmpty())
      response_ = "FORMAT";
    else if (!key.isEmpty()) {
      if (minFirst_.isEmpty()) {
        minFirst_ = key;
        minPendingGeneration_ = traffic_.tracks().value(key).firstMrtMs;
        response_.clear();
      } else if (key == minFirst_)
        response_ = "DUP TRK";
      else if (!tracks_.contains(minFirst_) ||
               traffic_.tracks().value(minFirst_).firstMrtMs !=
                   minPendingGeneration_) {
        response_ = "ILL TRK";
        minFirst_.clear();
      } else {
        minPair_ = {minFirst_, key};
        minGeneration_ = {minPendingGeneration_,
                          traffic_.tracks().value(key).firstMrtMs};
        minFirst_.clear();
        mode_ = Mode::None;
        response_.clear();
      }
    }
    return true;
  }
  if (mode_ != Mode::Multi) return false;
  if (input_ == "D*") {
    response_ = degrees(point.y()) + (point.y() < 0 ? "S/" : "/") +
                degrees(point.x()) + (point.x() > 0 ? "E" : "");
    input_.clear();
    mode_ = Mode::None;
    return true;
  }
  const bool ptl = input_ == "R";
  if (!ptl && input_ != "RA" && input_ != "RAE" && input_ != "RAI")
    return false;
  const auto track = traffic_.tracks().constFind(key);
  if (track == traffic_.tracks().cend()) {
    response_ = ptl ? "ILL TRK" : "NO TRK";
    return true;
  }
  const auto& plan = track->report.plan;
  if (ptl ? (track->report.status != "active" || automaticPtl(*track))
          : (!plan || plan->suspended || !trackDatablock(*track).full)) {
    response_ = "ILL TRK";
    return true;
  }
  if (!ptl && plan->requestedAltitude <= 0) {
    response_ = "ILL FNCT";
    return true;
  }
  auto& selection = selections_[key];
  selection.firstMrt = track->firstMrtMs;
  selection.address = track->report.address;
  selection.associationSince = track->associationSinceMs;
  if (ptl)
    selection.singlePtl = !selection.singlePtl;
  else {
    selection.planId = plan->sfpn;
    selection.acid = plan->acid;
    selection.requestedAltitude =
        input_ == "RA"
            ? !selection.requestedAltitude.value_or(requestedAltitude_)
            : input_ == "RAE";
    input_.clear();
    mode_ = Mode::None;
  }
  response_.clear();
  return true;
}
void STARSPane::drawMinSep(renderer::CmdBuffer& cb, const Fonts& fonts,
                           QSizeF size, float dpi) const {
  using namespace renderer;
  if (!prefs_.brightness.lines || minPair_[0].isEmpty()) return;
  const auto& tracks = traffic_.tracks();
  const auto a = tracks.constFind(minPair_[0]),
             b = tracks.constFind(minPair_[1]);
  if (a == tracks.cend() || b == tracks.cend()) return;
  const auto pa = *a->report.location, pb = *b->report.location;
  const auto va = a->report.velocity.value_or(QPointF{}),
             vb = b->report.velocity.value_or(QPointF{});
  const double scale = radar::longitudeScaleFactorForLat((pa.y() + pb.y()) / 2);
  if (scale < 1e-9) return;
  QPointF separation(math::normalizeLongitude(pb.x() - pa.x()) * 60 * scale,
                     (pb.y() - pa.y()) * 60);
  const auto relative = vb - va;
  const auto speed2 = QPointF::dotProduct(relative, relative);
  const double hours =
      speed2 > 0
          ? std::max(0., -QPointF::dotProduct(separation, relative) / speed2)
          : 0;
  const auto pixel = [&](QPointF p, QPointF velocity = {}) {
    p += QPointF(velocity.x() / scale, velocity.y()) * (hours / 60);
    const auto pos = screen(p, size);
    return PointVertex{float(pos.x()), float(pos.y())};
  };
  const auto endA = pixel(pa, va), endB = pixel(pb, vb);
  const auto color = colors::brightness(palette_.PTL, prefs_.brightness.lines);
  LinesBuilder lines;
  ColoredTrianglesBuilder markers;
  lines.addLine(endA, endB);
  if (hours > 0) {
    lines.addLine(pixel(pa), endA);
    lines.addLine(pixel(pb), endB);
    for (auto pos : {endA, endB})
      markers.addTriangleRGB({pos.x, pos.y - 6}, {pos.x - 5, pos.y + 4},
                             {pos.x + 5, pos.y + 4}, color);
  }
  cb.loadProjectionMatrix(screenOrtho(size.width(), size.height()));
  cb.lineWidth(std::max(1.f, dpi));
  cb.setRGB(color);
  lines.generateCommands(cb);
  markers.generateCommands(cb);
  separation += relative * hours;
  const QString distance =
      QString::number(std::hypot(separation.x(), separation.y()), 'f', 2) +
      " NM";
  const int index = prefs_.charSize.tools;
  const auto& font = fonts.sizes[index];
  const float w = std::max(int(distance.size()), hours > 0 ? 0 : 7) *
                      font.charSize().x +
                  8,
              h = font.lineHeight * (hours > 0 ? 1 : 2) + 8;
  const float x = (endA.x + endB.x - w) / 2, y = (endA.y + endB.y - h) / 2;
  ColoredTrianglesBuilder backing;
  backing.addQuad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h},
                  palette_.Background);
  backing.generateCommands(cb);
  LinesBuilder border;
  border.addLineLoop(std::array<PointVertex, 4>{
      {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}});
  border.generateCommands(cb);
  TextBuilder text;
  const auto addRow = [&](QString row, float top) {
    text.addText(font, row.toStdString(),
                 {x + (w - row.size() * font.charSize().x) / 2, top},
                 {color.rgba()});
  };
  addRow(distance, y + 4);
  if (hours <= 0) addRow("NO XING", y + 4 + font.lineHeight);
  text.generateCommands(cb, fonts.textures[index]);
}
}  // namespace stars
