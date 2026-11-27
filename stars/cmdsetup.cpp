#include <QDateTime>
#include <QRegularExpression>
#include <tuple>

#include "stars.h"

namespace stars {
bool STARSPane::setupList(std::optional<QPointF> position) {
  if ((position && input_ == "S") || input_ == "TX" || input_ == "TM" ||
      input_ == "TO" || input_ == "TQ") {
    const bool valid =
        position ? input_ != "TO" : input_ == "TO" || input_ == "TX";
    if (!valid) {
      response_ = "FORMAT";
      return true;
    }
    auto& list = prefs_.lists[input_ == "TO" ? QStringLiteral("TQ") : input_];
    if (position) {
      list.position = *position;
      list.visible = true;
    } else
      list.visible = !list.visible;
    input_.clear();
    response_.clear();
    mode_ = Mode::None;
    return true;
  }
  const auto id = input_.section(' ', 0, 0);
  if (id == "S")
    return false;  // Preserve the S-prefixed general information commands.
  if (!prefs_.lists.contains(id) && !id.startsWith('P')) return false;
  static const QRegularExpression syntax(
      "^(T|TV|TC|TL|TRA|TN|P[A-Z0-9]{1,3})(?: ([0-9]+))?$");
  const auto match = syntax.match(input_);
  if (!match.hasMatch() ||
      (match.hasCaptured(2) &&
       (position || id == "TL" || id == "TRA" || id == "TN"))) {
    response_ = "FORMAT";
    return true;
  }
  int lines = 0;
  if (match.hasCaptured(2)) {
    lines = match.captured(2).toInt();
    if (lines < 1 || lines > 100) {
      response_ = "ILL PARAM";
      return true;
    }
  }
  if ((id == "TN" && crda_.pairs.isEmpty()) ||
      (id == "TL" && facilityStations_.isEmpty()) ||
      (id.startsWith('P') && (!prefs_.lists.contains(id) ||
                              id.mid(1).toInt() > towerLists_.size()))) {
    response_ = "ILL FNCT";
    return true;
  }
  auto& list = prefs_.lists[id];
  if (position) list.position = *position;
  if (lines) list.lines = lines;
  list.visible = position || lines || !list.visible;
  input_.clear();
  response_.clear();
  mode_ = Mode::None;
  return true;
}

namespace {
QString usable(QString text) {
  static const QSet<QString> missing{"UNASSIGNED", "UNKNOWN", "UNAVAILABLE",
                                     "UNDEFINED",  "NONE",    "PENDING"};
  return missing.contains(text) ? QString{} : text;
}
bool departure(const QString& type) {
  return type == "P" || type == "D" || type.startsWith("DEP");
}
bool arrival(const QString& type) {
  return type == "A" || type.startsWith("ARR");
}
QString altitude(std::optional<int> feet) {
  if (!feet) return "OR ";
  int hundreds = qRound(*feet / 100.);
  return hundreds < 0 ? "N" + QString::number(-hundreds).rightJustified(2, '0')
                      : QString::number(hundreds).rightJustified(3, '0');
}
}  // namespace

void STARSPane::updateListRows() {
  listRows_.clear();
  const auto& plans = traffic_.flightPlans();
  struct Candidate {
    QString identity;
    const net::ListedPlan* p;
    bool coast, suspend;
    qint64 entered;
  };
  QList<Candidate> candidates;
  QSet<QString> eligible;
  QMap<QString, int> acids, codes;
  for (auto it = plans.cbegin(); it != plans.cend(); ++it) {
    const auto& p = *it;
    const auto& f = p.plan;
    const bool coast =
        p.lastMrt &&
        trafficMs_ >= p.lastMrt + net::Traffic::coastSeconds * 1000;
    const bool own = !tcp_.isEmpty() &&
                     (p.owner == tcp_ || (p.track.isEmpty() && f.cps == tcp_));
    const bool suspend = f.suspended && own;
    const bool showCoast =
        !f.suspended && coast &&
        (own || (!tcp_.isEmpty() && (p.formerOwner == tcp_ ||
                                     (f.ocr == "PENDING" && f.cps == tcp_))));
    if (p.pending || !p.track.isEmpty() || f.suspended) {
      if (!f.acid.isEmpty()) ++acids[f.acid];
      if (!f.assignedBeacon.isEmpty() && f.assignedBeacon != "0000")
        ++codes[f.assignedBeacon];
    }
    if (f.suspended && !suspend) continue;
    if (!p.pending && !f.vfr && !suspend && !showCoast) continue;
    const auto identity = it.key() + '@' + QString::number(p.firstSeen);
    candidates.append({identity, &p, showCoast, suspend,
                       suspend ? p.suspendSince
                       : showCoast
                           ? p.lastMrt + net::Traffic::coastSeconds * 1000
                           : p.firstSeen});
    eligible.insert(identity);
  }
  listNumbers_.removeIf([&](auto it) { return !eligible.contains(it.key()); });
  std::array<bool, 100> used{};
  for (int number : listNumbers_) used[number] = true;
  std::sort(
      candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return std::tuple{!(a.coast || a.suspend), a.entered, a.identity} <
               std::tuple{!(b.coast || b.suspend), b.entered, b.identity};
      });
  struct Row {
    qint64 time;
    QString acid, key, text;
  };
  QMap<QString, QList<Row>> entries;
  for (const auto& c : candidates) {
    const auto& p = *c.p;
    const auto& f = p.plan;
    if (!listNumbers_.contains(c.identity)) {
      for (int i = 0; i < 100; ++i) {
        const int n = (nextListNumber_ + i) % 100;
        if (used[n]) continue;
        listNumbers_[c.identity] = n;
        used[n] = true;
        nextListNumber_ = (n + 1) % 100;
        break;
      }
    }
    const auto number =
        listNumbers_.contains(c.identity)
            ? QString::number(listNumbers_.value(c.identity)).rightJustified(2)
            : QString("  ");
    const auto acid = f.acid.left(7).leftJustified(7);
    if (c.coast || c.suspend) {
      QString beacon = c.coast ? p.beacon : f.assignedBeacon;
      if (beacon.isEmpty()) beacon = c.coast ? f.assignedBeacon : p.beacon;
      const QString detail = c.suspend && p.coasting ? "CST"
                             : p.track.isEmpty()     ? "NT"
                             : p.frozen              ? "ZZ"
                             : p.altitude            ? altitude(p.altitude)
                                                     : QString{};
      const auto text = number + ' ' + acid + (c.coast ? " C " : " S ") +
                        beacon.rightJustified(4);
      entries["TC"].append({c.entered, f.acid, c.identity,
                            detail.isEmpty() ? text : text + ' ' + detail});
    }
    if (f.suspended) continue;
    if (!tcp_.isEmpty() && (localTCPs_.contains(f.cps) || f.cps == tcp_)) {
      if (f.cps != tcp_) continue;
    } else if (!areaAirports_.isEmpty()) {
      const QString local = airportKey(usable(f.airport));
      const auto src = airportKey(usable(f.origin)),
                 dst = airportKey(usable(f.destination));
      if (f.vfr) {
        if (!(src.isEmpty() && dst.isEmpty() && local.isEmpty()) &&
            !areaAirports_.contains(local) && !areaAirports_.contains(src) &&
            !areaAirports_.contains(dst))
          continue;
      } else if (!(f.type.startsWith('O') || f.type.startsWith('E'))) {
        const auto endpoint = arrival(f.type) && !dst.isEmpty()     ? dst
                              : departure(f.type) && !src.isEmpty() ? src
                                                                    : local;
        if (!endpoint.isEmpty() && !areaAirports_.contains(endpoint)) continue;
      }
    }
    const auto code = f.assignedBeacon == "0000" ? QString{} : f.assignedBeacon;
    const bool vfrInhibit = !f.ifr && (f.vfr || p.beacon == "1200");
    QString text = number + (acids.value(f.acid) > 1 ? "/" : " ") +
                   (vfrInhibit ? "*" : " ") + acid + ' ' +
                   (!code.isEmpty() && codes.value(code) > 1 ? "/" : " ") +
                   code.rightJustified(4);
    if (f.vfr) entries["TV"].append({p.vfrSince, f.acid, c.identity, text});
    if (p.pending) {
      QString fixes;
      if (f.vfr && !usable(f.gufi).isEmpty())
        fixes = "VFR";
      else {
        const auto entry =
            departure(f.type) ? QString{} : usable(f.entryFix).left(1);
        const auto exit =
            arrival(f.type) ? QString{} : usable(f.exitFix).left(1);
        fixes = entry.leftJustified(1) + exit.leftJustified(1) + ' ';
      }
      entries["T"].append({0, f.acid, c.identity,
                           text + ' ' + altitude(p.altitude) + ' ' + fixes});
    }
  }
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    std::sort(it->begin(), it->end(), [](const Row& a, const Row& b) {
      return std::tie(a.time, a.acid, a.key) < std::tie(b.time, b.acid, b.key);
    });
    for (const auto& row : *it) listRows_[it.key()].append(row.text);
  }
  for (int index = 0; index < towerLists_.size(); ++index) {
    const auto& tower = towerLists_[index];
    if (tower.location.isNull()) continue;
    QList<std::pair<double, QString>> rows;
    for (const auto& t : traffic_.tracks()) {
      const auto& f = t.report.plan;
      if (!f || f->acid.isEmpty() || departure(f->type) || f->suspended)
        continue;
      const auto dest = usable(f->destination);
      if (airportKey(dest.isEmpty() ? f->airport : dest) !=
          airportKey(tower.airport))
        continue;
      const auto nm = radar::distanceNM(tower.location, *t.report.location);
      if (nm <= tower.range)
        rows.append({nm, f->acid.leftJustified(7) + ' ' +
                             f->aircraftType.rightJustified(4)});
    }
    std::sort(rows.begin(), rows.end());
    for (const auto& row : rows)
      listRows_["P" + QString::number(index + 1)].append(row.second);
  }
}
QStringList STARSPane::systemListText(const QString& id) const {
  const auto pref = prefs_.lists.constFind(id);
  if (pref == prefs_.lists.cend() || !pref->visible) return {};
  QStringList result;
  if (id == "S" || id == "TX")
    return {};  // Drawn with their own symbol and map formatting.
  if (id == "TQ")
    return trafficInitialized() ? QStringList{"MCI SUPPRESSION"}
                                : QStringList{};
  if (id == "TM") {
    if (facility_.isEmpty()) return {};
    QStringList enabled;
    if (alertEnabled("MSAW")) enabled.append("LA");
    if (alertEnabled("CA")) enabled.append("CA");
    if (alertEnabled("MCI")) enabled.append("MCI");
    if (enabled.isEmpty()) return {};
    result.append(enabled.join('/'));
    QList<std::pair<qint64, QString>> entries;
    for (const auto& track : traffic_.tracks()) {
      const auto* event = msawAlert(track);
      if (!event || !track.report.plan || !track.report.altitude) continue;
      const auto acid = track.report.plan->acid.trimmed().toUpper();
      if (acid.isEmpty()) continue;
      entries.append(
          {-*event->since, QString("%1%2 LA").arg(acid, -13).arg(
                               (*track.report.altitude + 50) / 100, 4)});
    }
    std::sort(entries.begin(), entries.end());
    constexpr int capacity = 50;
    if (entries.size() > capacity)
      result.append(QString("MORE: %1/%2").arg(capacity).arg(entries.size()));
    for (int i = 0; i < entries.size() && i < capacity; ++i)
      result.append(entries[i].second);
    return result;
  }
  if (id == "TN") {
    for (const auto& pair : crda_.pairs) {
      if (std::none_of(
              ssaAirports_.begin(), ssaAirports_.end(),
              [&](const auto& a) { return a.id == airportKey(pair.airport); }))
        continue;
      result.append(QString("%1%2 %3 %4 %5")
                        .arg(pair.mode == 'D' ? QChar(' ') : pair.mode)
                        .arg(result.size() + 1)
                        .arg(pair.airport,
                             (pair.runway[0].id + '/' + pair.runway[1].id)
                                 .leftJustified(7),
                             pair.mode == 'D' ? QString{} : tcp_));
    }
    if (!result.isEmpty()) result.prepend("CRDA STATUS");
    return result;
  }
  if (id == "TRA") {
    result.append("GEO RESTRICTIONS");
    const auto& areas = restrictions_.areas();
    for (auto it = areas.cbegin(); it != areas.cend(); ++it) {
      const auto setting = prefs_.restrictionDisplay.value(it->serial);
      result.append(QString("%1%2 %3")
                        .arg(setting.hidden ? ' ' : '>')
                        .arg(it.key(), -3)
                        .arg(it->text.value(0)));
    }
    return result;
  }
  if (id == "TL") {
    if (facilityStations_.isEmpty()) return {};
    result.append("ALTIMETER");
    for (const auto& airport : facilityStations_) {
      const auto station = stationSetting(airport.id);
      QString setting;
      if (station && station->hundredths)
        setting = QString::number(station->hundredths / 100., 'f', 2) +
                  station->status;
      result.append(QString("%1 %2 %3")
                        .arg(result.size() - 1, -2)
                        .arg(airport.label, -3)
                        .arg(setting));
    }
    return result;
  }
  if (id.startsWith('P')) {
    int index = id.mid(1).toInt() - 1;
    if (index < 0 || index >= towerLists_.size()) return {};
    result.append(towerLists_[index].airport + " TOWER");
  } else
    result.append(id == "TC"  ? "COAST/SUSPEND"
                  : id == "T" ? "FLIGHT PLAN"
                              : "VFR LIST");
  const auto& rows = listRows_.value(id);
  if (rows.size() > pref->lines)
    result.append(QString("MORE: %1/%2").arg(pref->lines).arg(rows.size()));
  result.append(rows.mid(0, pref->lines));
  return result;
}
void STARSPane::drawSystemLists(renderer::CmdBuffer& cb, const Fonts& fonts,
                                QSizeF size) const {
  using namespace renderer;
  const int index = prefs_.charSize.lists;
  const auto& font = fonts.sizes[index];
  const auto color = colors::brightness(palette_.List, prefs_.brightness.lists);
  TextBuilder text;
  const bool blink = (QDateTime::currentMSecsSinceEpoch() / 500) % 2;
  cb.loadProjectionMatrix(screenOrtho(size.width(), size.height()));
  for (auto it = prefs_.lists.cbegin(); it != prefs_.lists.cend(); ++it) {
    const auto rows = systemListText(it.key());
    const float x = size.width() * it->position.x();
    float y = size.height() * it->position.y();
    for (int i = 0; i < rows.size(); ++i, y += font.lineHeight) {
      bool missing = false;
      if (it.key() == "TL" && i > 0) {
        const auto setting = stationSetting(facilityStations_[i - 1].id);
        missing = setting && setting->missing;
      }
      if (!missing || blink)
        text.addText(font, rows[i].toStdString(), {x, y},
                     {(missing ? colors::brightness(palette_.TextWarning,
                                                    prefs_.brightness.lists)
                               : color)
                          .rgba()});
    }
  }
  text.generateCommands(cb, fonts.textures[index]);
}
}  // namespace stars
