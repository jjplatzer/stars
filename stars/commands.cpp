#include <QRegularExpression>

#include "../aviation/cifp.h"
#include "datablock.h"
#include "stars.h"

namespace stars {
QString Dcb::preview() const {
  if (page_ == Page::Pref)
    return error_ + '\n' + (saveAs_ ? "PREF SET NAME" : "PREF") + '\n' + input_;
  if (!adjusting() || placingRings()) return {};
  const QString prompt = active_ == "Ring spacing"   ? "RR"
                         : page_ == Page::Brite      ? "BRT"
                         : page_ == Page::CharSize   ? "CHAR"
                         : active_.startsWith("LDR") ? "LDR"
                                                     : active_;
  return error_ + '\n' + prompt + '\n' + input_;
}

void Dcb::cancelCommand() {
  finishAdjustment();
  page_ = (page_ == Page::Aux || page_ == Page::TPA) ? Page::Aux : Page::Main;
  release();
}

bool Dcb::key(const QString& key) {
  if (page_ == Page::Pref && radar_ && key != "Escape") return prefKey(key);
  if (key == "Escape") {
    if (page_ == Page::Main || page_ == Page::Aux) {
      if (!adjusting()) return false;
      finishAdjustment();
      release();
      return true;
    }
    // REDS cancels an active CHAR SIZE entry one level at a time.
    if (page_ != Page::CharSize || !adjusting())
      page_ = page_ == Page::TPA ? Page::Aux : Page::Main;
    finishAdjustment();
    release();
    return true;
  }
  if (page_ == Page::TPA) return true;
  if (!adjusting()) return false;
  if (placingRings()) return true;
  if (key == "Backspace") {
    error_.clear();
    input_.chop(1);
    return true;
  }
  if (key != "Enter") {
    if (key.size() != 1 || !key.front().isPrint()) return true;
    if (!error_.isEmpty()) input_.clear();
    error_.clear();
    input_ += key.toUpper();
    return true;
  }
  if (active_ == "PTL LNTH") {
    static const QRegularExpression length(
        R"(^(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)$)");
    bool ok = false;
    const double steps = input_.toDouble(&ok) * 2;
    if (!length.match(input_).hasMatch() || !ok || steps > 10 ||
        steps != std::floor(steps))
      error_ = "FORMAT";
    else {
      preferences().ptlHalfMinutes = int(steps);
      finishAdjustment();
    }
    return true;
  }
  if (active_ == "DWELL" &&
      (input_.size() != 1 || input_.front() < '0' || input_.front() > '2')) {
    error_ = "FORMAT";
    return true;
  }
  if (input_.isEmpty() || input_.contains(QRegularExpression("[^0-9]"))) {
    error_ = "FORMAT";
    return true;
  }
  bool ok = false;
  const int value = input_.toInt(&ok);
  if (!ok || !setActiveValue(value)) {
    error_ = "ILL VALUE";
    return true;
  }
  finishAdjustment();
  return true;
}

QString STARSPane::preview() const {
  if (dcb_.adjusting() || dcb_.page() == Dcb::Page::Pref) return dcb_.preview();
  if (mode_ == Mode::Restriction) return response_ + "\nRESTR AREA\n" + input_;
  if (mode_ == Mode::Min) return response_ + "\nMIN\n" + input_;
  if (mode_ == Mode::CA) return response_ + "\nCA\n" + input_;
  if (mode_ == Mode::Multi)
    return response_ + "\nF" + input_.left(1) + '\n' +
           input_.mid(1).simplified().replace(' ', '\n');
  if (input_.isEmpty() && response_.isEmpty()) return {};
  return response_ + '\n' + input_.simplified().replace(' ', '\n');
}

void STARSPane::designate(Endpoint endpoint) {
  if (!start_ && lines_.size() >= 9) {
    response_ = "CAPACITY";
    return;
  }
  response_.clear();
  if (start_) {
    lines_.push_back({*start_, endpoint});
    start_.reset();
    input_.clear();
  } else {
    start_ = endpoint;
    input_ = "*T";
  }
}

QString STARSPane::closestTrack(QPointF mouse, QSizeF size) const {
  QString key;
  double closest = 20;
  for (auto it = tracks_.cbegin(); it != tracks_.cend(); ++it) {
    const auto delta = screen(it->location, size) - mouse;
    const double distance = std::hypot(delta.x(), delta.y());
    if (distance < closest) {
      closest = distance;
      key = it.key();
    }
  }
  return key;
}
void STARSPane::slew(QPointF mouse, QSizeF size) {
  if (mode_ == Mode::Restriction) {
    restrictionCommand(geographic(mouse, size));
    return;
  }
  if (mode_ == Mode::Multi &&
      setupList(QPointF(mouse.x() / size.width(), mouse.y() / size.height())))
    return;
  Endpoint endpoint{geographic(mouse, size), closestTrack(mouse, size)};
  if (mode_ == Mode::Multi && msawCommand(endpoint.track)) return;
  const bool explicitLeader = mode_ == Mode::Multi && input_.startsWith('L');
  const auto digit = explicitLeader ? input_.mid(1) : input_;
  if ((explicitLeader || mode_ == Mode::None) && digit.size() == 1 &&
      digit.front().isDigit()) {
    const int direction = QStringLiteral("89632147").indexOf(digit);
    const auto track = traffic_.tracks().constFind(endpoint.track);
    if (direction < 0 && digit != "5")
      response_ = "FORMAT";
    else if (track == traffic_.tracks().cend()) {
      if (explicitLeader) response_ = "NO TRK";
    } else if (!track->report.plan)
      response_ = "ILL TRK";
    else {
      auto& selection = selections_[endpoint.track];
      selection.firstMrt = track->firstMrtMs;
      selection.address = track->report.address;
      selection.associationSince = track->associationSinceMs;
      if (digit == "5")
        selection.direction.reset();
      else
        selection.direction = direction;
      response_.clear();
      if (!explicitLeader) input_.clear();
    }
    return;
  }
  if (crdaSlew(mouse, endpoint.track)) return;
  if (toolSlew(endpoint.track, endpoint.location)) return;
  if (mode_ == Mode::Multi || input_.isEmpty())
    selectTrack(endpoint.track);
  else if (input_ == "*T")
    designate(endpoint);
  else
    atpaCommand(true, endpoint.track);
}

void STARSPane::setTracks(QMap<QString, Track> tracks) {
  tracks_ = std::move(tracks);
  const auto stale = [&](const Endpoint& e) {
    return !e.track.isEmpty() && !tracks_.contains(e.track);
  };
  std::erase_if(lines_, [&](const auto& line) {
    return stale(line[0]) || stale(line[1]);
  });
  if (start_ && stale(*start_)) {
    start_.reset();
    input_.clear();
    response_.clear();
  }
}

bool STARSPane::key(const QString& key) {
  if (mode_ == Mode::Restriction &&
      (key.startsWith("Ctrl+") || key.startsWith("F") && key.size() > 1 ||
       key == "Escape" || key == "End"))
    restrictions_.cancel();
  if (key == "F12") {
    mode_ = Mode::Restriction;
    dcb_.cancelCommand();
    start_.reset();
    input_.clear();
    response_.clear();
    return true;
  }
  if (key == "Ctrl+F12") {
    mode_ = Mode::None;
    input_.clear();
    response_.clear();
    start_.reset();
    if (showDcb_) dcb_.openPref();
    return true;
  }
  if (key == "End" || key == "F7" || key == "Ctrl+F8" || key == "F11") {
    dcb_.cancelCommand();
    input_.clear();
    response_.clear();
    start_.reset();
    minFirst_.clear();
    mode_ = key == "End" ? Mode::Min : key == "F7" ? Mode::Multi : Mode::CA;
    return true;
  }
  if (key == "Ctrl+F9") {
    showDcb_ = !showDcb_;
    dcb_.cancelCommand();
    cancelPointer();
    mode_ = Mode::None;
    input_.clear();
    response_.clear();
    start_.reset();
    return true;
  }
  if (showDcb_ && dcb_.key(key)) return true;
  if (key == "Escape") {
    mode_ = Mode::None;
    start_.reset();
    input_.clear();
    response_.clear();
    return true;
  }
  if (key == "Backspace") {
    input_.chop(1);
    return true;
  }
  if (mode_ == Mode::Restriction && restrictions_.cycling() && key == "+") {
    response_ = restrictions_.stepColor();
    input_.clear();
    return true;
  }
  if (key != "Enter") {
    if (key.size() != 1) return false;
    const bool delta = QStringLiteral("`\u0300△∆Δ\u0080").contains(key);
    if (!delta && !key.front().isPrint()) return false;
    if (input_.isEmpty()) response_.clear();
    input_ += delta ? QString(QChar(0x80)) : key.toUpper();
    return true;
  }
  if (mode_ == Mode::Restriction) {
    restrictionCommand();
    return true;
  }
  if (mode_ == Mode::Min) {
    if (!input_.isEmpty())
      response_ = "FORMAT";
    else {
      minPair_ = {};
      minFirst_.clear();
      mode_ = Mode::None;
      response_.clear();
    }
    return true;
  }
  if (mode_ == Mode::Multi &&
      (input_ == "DE" || input_ == "DI" || input_ == "DL")) {
    prefs_.dwell = input_ == "DI" ? 0 : input_ == "DE" ? 1 : 2;
    input_ = "D";
    response_.clear();
    return true;
  }
  if (mode_ == Mode::Multi && crdaCommand()) return true;
  if (mode_ == Mode::Multi && setupList()) return true;
  if (supervisorCommand() || atpaCommand()) return true;
  if (mode_ == Mode::Multi) {
    if (input_ == "B" || input_ == "BE" || input_ == "BI" || input_ == "RA" ||
        input_ == "RAE" || input_ == "RAI") {
      if (input_.startsWith('B')) clearBeaconOverrides();
      auto& value =
          input_.startsWith('B') ? prefs_.ldbBeaconCodes : requestedAltitude_;
      value = input_.endsWith('E')   ? true
              : input_.endsWith('I') ? false
                                     : !value;
      mode_ = Mode::None;
      input_.clear();
      response_.clear();
      return true;
    }
    if (input_.startsWith('B')) {
      auto& codes = prefs_.selectedBeacons;
      const auto code = input_.mid(1);
      if (code == "*")
        codes.clear();
      else if (!Preferences::validBeacon(code)) {
        response_ = "FORMAT";
        return true;
      } else if (codes.contains(code))
        codes.removeAll(code);
      else if (codes.size() < 10)
        codes.append(code);
      else {
        response_ = "CAPACITY";
        return true;
      }
      mode_ = Mode::None;
      input_.clear();
      response_.clear();
      return true;
    }
    if (input_.startsWith('F')) {
      static const QRegularExpression syntax(
          "^F(C)?([0-9]{6})(?: +([0-9]{6}))?$");
      auto match = syntax.match(input_);
      if (input_ == "F")
        response_ = altitudeFilters(false);
      else if (!match.hasMatch() ||
               (match.hasCaptured(1) && match.hasCaptured(3))) {
        response_ = "FORMAT";
        return true;
      } else {
        const auto range = [](QString digits) {
          int a = digits.left(3).toInt() * 100,
              b = digits.right(3).toInt() * 100;
          if (!a) a = -9900;
          if (!b) b = -9900;
          return std::array{std::min(a, b), std::max(a, b)};
        };
        (match.hasCaptured(1) ? prefs_.associatedAltitude
                              : prefs_.unassociatedAltitude) =
            range(match.captured(2));
        if (match.hasCaptured(3))
          prefs_.associatedAltitude = range(match.captured(3));
        response_.clear();
      }
      mode_ = Mode::None;
      input_.clear();
      return true;
    }
    if (input_ == "K") {
      dcb_.defaultPref();
      mode_ = Mode::None;
      input_.clear();
      response_.clear();
      return true;
    }
    if (input_.startsWith('Q')) return quickLookCommand(input_.mid(1));
    if (input_.startsWith('S')) {
      response_ = modifyGeneralInfo(input_.mid(1));
      if (response_.isEmpty()) {
        mode_ = Mode::None;
        input_.clear();
      }
      return true;
    }
    if (input_.startsWith('A')) {
      response_ = modifyAltimeter(input_.mid(1));
      if (response_.isEmpty()) {
        mode_ = Mode::None;
        input_.clear();
      }
      return true;
    }
    if (input_ != QStringLiteral("W\u0080")) {
      response_ = "FORMAT";
      return true;
    }
    updateWeatherHistory();
    if (!historyIndex_) {
      historyPictures_ = weather_;
      historyTimer_.start();
      historyIndex_ = 2;
    }
    response_ = "IN PROGRESS";
    mode_ = Mode::None;
    input_.clear();
    return true;
  }
  if (input_.isEmpty()) return true;
  static const QRegularExpression impliedQuickLook(R"(^[A-Z0-9]{1,2}\+?$)");
  if (impliedQuickLook.match(input_).hasMatch())
    return quickLookCommand(input_);
  if (!input_.startsWith("*T")) {
    response_ = "FORMAT";
    return true;
  }
  const QString arg = input_.mid(2);
  static const QRegularExpression digits("^[0-9]+$"), field("^[A-Z0-9]{1,8}$"),
      beacon("^[0-7]{4}$");
  if (arg.isEmpty())
    lines_.clear();
  else if (digits.match(arg).hasMatch() &&
           !(start_ && beacon.match(arg).hasMatch())) {
    bool ok;
    const int id = arg.toInt(&ok);
    if (!ok) {
      response_ = "FORMAT";
      return true;
    }
    if (id < 1 || id > int(lines_.size())) {
      response_ = "RBL ID";
      return true;
    }
    lines_.erase(lines_.begin() + id - 1);
  } else {
    const QString ref = arg.trimmed();
    const bool trackOnly = arg.startsWith(' ');
    if (!field.match(ref).hasMatch() || (start_ && trackOnly)) {
      response_ = "FORMAT";
      return true;
    }
    if (!start_ && lines_.size() >= 9) {
      response_ = "CAPACITY";
      return true;
    }
    if (!trackOnly) {
      if (auto fix = aviation::lookupWaypoint(ref)) {
        designate({*fix, {}});
        return true;
      }
    }
    if (start_ || trackOnly) {
      QString match;
      const bool byCode = beacon.match(ref).hasMatch();
      for (auto it = tracks_.cbegin(); it != tracks_.cend(); ++it) {
        if (!it->associated) continue;
        const auto code = it->assignedBeacon.trimmed();
        if (byCode ? code != ref
                   : it->acid.trimmed().compare(ref, Qt::CaseInsensitive) != 0)
          continue;
        if (!match.isEmpty()) {
          response_ = byCode ? "DUP BCN" : "DUP ID " + ref;
          return true;
        }
        match = it.key();
      }
      if (!match.isEmpty()) {
        designate({{}, match});
        return true;
      }
    }
    response_ = "NO FLIGHT";
    return true;
  }
  input_.clear();
  response_.clear();
  start_.reset();
  return true;
}

bool STARSPane::atpaCommand(bool selected, const QString& key) {
  QString result;
  if (mode_ == Mode::Multi) {
    if (input_ == "+")
      result = atpa_.enabled() ? atpa_.disabledVolumes()
                               : QStringLiteral("ILL FNCT");
    else
      return false;
  } else if (input_ == "**J" || input_ == "**P") {
    atpa_.clearManual(input_.back());
  } else {
    const bool manual = input_.startsWith("*J") || input_.startsWith("*P");
    static const QRegularExpression syntax(R"(^\*(?:[ABD][EI]|D\+[EI]?)$)");
    if (!syntax.match(input_).hasMatch() && !(manual && selected)) return false;
    const auto control = input_.contains('+') ? QChar('+') : input_[1];
    if (selected) {
      const auto it = traffic_.tracks().constFind(key);
      if (it == traffic_.tracks().cend())
        result = "NO TRK";
      else {
        const auto d = trackDatablock(*it);
        result = atpa_.setTrackDisplay(input_, *it, d.full, prefs_);
      }
    } else if (control != '+' && !atpa_.enabled())
      result = "ILL FNCT";
    else {
      const bool on =
          input_.endsWith('+') ? !prefs_.tpaMileage : input_.endsWith('E');
      setAtpaDisplay(control, on);
      if (control == '+') result = on ? "TPA SIZE ON" : "TPA SIZE OFF";
    }
  }
  response_ = result;
  if (!selected && !result.startsWith("ILL ") && result != "FORMAT" &&
      result != "NO TRK" && result != "CAPACITY") {
    mode_ = Mode::None;
    input_.clear();
  }
  return true;
}
}  // namespace stars
