#include <cmath>

#include "net/tais.h"
#include "stars.h"

namespace stars {
void STARSPane::resetTraffic() {
  msawSync_.reset();
  msawApproaches_.clear();
  msawEvaluated_ = false;
  if (alertSite_.use_count() <= 2) alertSite_->lowAltitude.clear();
  selections_.clear();
  minPair_ = {};
  minFirst_.clear();
  dwellTrack_.clear();
  // The registry retains one site reference. Other open panes may still be
  // displaying an owned SPC that was acknowledged at this workstation.
  if (alertSite_.use_count() <= 2) alertSite_->acknowledged.clear();
  archiveAvailable_ = false;
  activeConditions_.clear();
  traffic_.reset();
  ghostSelections_.clear();
  ghostHits_.clear();
  listRows_.clear();
  listNumbers_.clear();
  nextListNumber_ = 0;
  atpa_.resetTracks();
  setTracks({});
}

void STARSPane::updateTraffic(const net::TaisCache& cache, qint64 ms) {
  if (ms < trafficMs_ || ms - trafficMs_ > net::Traffic::coastSeconds * 1000) {
    msawSync_.reset();
    msawApproaches_.clear();
    msawEvaluated_ = false;
    if (alertSite_.use_count() <= 2) alertSite_->lowAltitude.clear();
    atpa_.resetTracks();
    ghostSelections_.clear();
    ghostHits_.clear();
    listNumbers_.clear();
    nextListNumber_ = 0;
    minPair_ = {};
    minFirst_.clear();
    dwellTrack_.clear();
    selections_.clear();
  }
  trafficMs_ = ms;
  const bool changed = traffic_.advance(cache, ms);
  archiveAvailable_ = cache.availableAt(ms / 1000) && traffic_.initialized();
  atpa_.update(traffic_.tracks(), changed);
  updateMsaw(changed);
  if (!changed) return;
  pruneSelections();
  updateListRows();
  QMap<QString, Track> tools;
  activeConditions_.clear();
  for (auto it = traffic_.tracks().cbegin(); it != traffic_.tracks().cend();
       ++it) {
    const auto& r = it->report;
    const auto code = specialCondition(r.beacon);
    if (!code.isEmpty()) activeConditions_.insert(code);
    tools.insert(it.key(), {*r.location, r.plan ? r.plan->acid : QString{},
                            r.plan ? r.plan->assignedBeacon : QString{},
                            r.speed, bool(r.plan)});
  }
  setTracks(std::move(tools));
}

void STARSPane::drawTraffic(renderer::CmdBuffer& cb, const Fonts& fonts,
                            QSizeF size, float dpi) const {
  if (traffic_.tracks().isEmpty()) return;
  using namespace renderer;
  ColoredTrianglesBuilder dots, targets;
  ColoredLinesBuilder leaders, ptls;
  TextBuilder outlines, symbols, text;
  const int ps = prefs_.charSize.positionSymbols,
            db = prefs_.charSize.datablocks;
  const auto& font = fonts.sizes[db];
  const auto projection = transformations(size);
  const auto screen = [&](QPointF location) {
    auto p = projection.screen(location);
    return PointVertex{float(p.x()), float(p.y())};
  };
  const auto octagon = [](ColoredTrianglesBuilder& builder, PointVertex c,
                          float radius, RGB color) {
    constexpr float a = .92387953f, b = .38268343f;
    constexpr PointVertex offsets[] = {{a, b},   {b, a},   {-b, a}, {-a, b},
                                       {-a, -b}, {-b, -a}, {b, -a}, {a, -b}};
    for (int i = 0; i < 8; ++i) {
      const auto v = offsets[i], w = offsets[(i + 1) % 8];
      builder.addTriangleRGB(c, {c.x + v.x * radius, c.y + v.y * radius},
                             {c.x + w.x * radius, c.y + w.y * radius}, color);
    }
  };
  const float length =
      std::array{0, 17, 32, 47, 62, 77, 114, 152}[prefs_.leaderLength];
  constexpr float u = .70710678f;
  constexpr PointVertex directions[] = {{0, -1}, {u, -u}, {1, 0},  {u, u},
                                        {0, 1},  {-u, u}, {-1, 0}, {-u, -u}};
  for (const auto& target : traffic_.tracks()) {
    const auto& r = target.report;
    const auto c = screen(*r.location);
    const auto d = trackDatablock(target);
    if (d.visible && prefs_.brightness.history > 0 &&
        !r.status.startsWith("coast")) {
      auto n = target.history.size();
      for (int age = 0; age < prefs_.historyCount; ++age) {
        const auto cutoff = r.mrtMs - 4500 * (age + 1);
        while (n > 0 && target.history[n - 1].time > cutoff) --n;
        if (!n) break;
        octagon(dots, screen(target.history[--n].point), 4,
                colors::brightness(palette_.TrackHistory[std::min(age, 4)],
                                   prefs_.brightness.history));
      }
    }
    if (c.x < -300 || c.y < -220 || c.x > size.width() + 300 ||
        c.y > size.height() + 220)
      continue;
    if (prefs_.ptlHalfMinutes && prefs_.brightness.lines && r.velocity) {
      const auto selection = selections_.constFind(r.number);
      if (automaticPtl(target) ||
          (selection != selections_.cend() && selection->singlePtl)) {
        const auto delta = *r.velocity * (prefs_.ptlHalfMinutes / 120.);
        const auto scale = radar::longitudeScaleFactorForLat(r.location->y());
        if (scale > 0 && !delta.isNull()) {
          const auto end =
              *r.location + QPointF(delta.x() / (60 * scale), delta.y() / 60);
          ptls.addLineRGB(
              c, screen(end),
              colors::brightness(palette_.PTL, prefs_.brightness.lines));
        }
      }
    }
    if (prefs_.brightness.primarySymbols)
      octagon(targets, c, 7,
              colors::brightness(palette_.TrackGeometry,
                                 prefs_.brightness.primarySymbols));
    if (d.showSymbol) {
      const auto symbol = d.symbol.toStdString();
      const auto glyph = [&](TextBuilder& builder, const FontSize& f,
                             RGB color) {
        const auto* g = f.glyph(d.symbol.front().unicode());
        if (!g) return;
        const PointVertex origin{
            c.x + .5f - f.measure(symbol).x * .5f - g->offsetX,
            c.y - .5f - g->height * .5f - g->offsetY};
        builder.addText(f, symbol, origin, {color.rgba()});
      };
      glyph(outlines, fonts.outlines[ps], palette_.PositionSymbolOutline);
      glyph(symbols, fonts.sizes[ps], d.symbolColor);
    }
    if (!d.visible) continue;
    const bool west = d.direction >= 4;
    const auto v = directions[d.direction];
    PointVertex end{c.x + v.x * length, c.y + v.y * length};
    if (length && d.showLeader)
      leaders.addLineRGB({c.x + v.x * 6.5f, c.y + v.y * 6.5f}, end,
                         d.leaderColor);
    if (!length) end.x += west ? -10.5f : 10.5f;
    for (int i = 0; i < 5; ++i) {
      const auto& row = d.rows[i];
      if (row.isEmpty()) continue;
      const auto width =
          row.size() + (i == d.alertRow ? d.alertSuffix.size() : 0);
      const float x = west ? end.x - 4 - width * font.charSize().x : end.x + 4;
      const float y = end.y + (i - 2.5f) * font.lineHeight;
      if (i == 4 && d.fieldLength) {
        text.addText(font, row.left(d.fieldLength).toStdString(), {x, y},
                     {d.fieldColor.rgba()});
        text.addText(font, row.mid(d.fieldLength).toStdString(),
                     {x + d.fieldLength * font.charSize().x, y},
                     {d.color.rgba()});
        continue;
      }
      if (i == d.alertRow && !d.alertSuffix.isEmpty())
        text.addText(font, d.alertSuffix.toStdString(),
                     {x + row.size() * font.charSize().x, y},
                     {d.alertSuffixColor.rgba()});
      text.addText(font, row.toStdString(), {x, y},
                   {(i == d.alertRow ? d.alertColor : d.color).rgba()});
    }
  }
  cb.loadProjectionMatrix(screenOrtho(size.width(), size.height()));
  cb.lineWidth(std::max(1.f, dpi));
  ptls.generateCommands(cb);
  dots.generateCommands(cb);
  targets.generateCommands(cb);
  cb.lineWidth(std::max(1.f, dpi));
  leaders.generateCommands(cb);
  outlines.generateCommands(cb, fonts.outlineTextures[ps]);
  symbols.generateCommands(cb, fonts.textures[ps]);
  text.generateCommands(cb, fonts.textures[db]);
}
}  // namespace stars
