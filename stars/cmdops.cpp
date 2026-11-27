#include <QRegularExpression>

#include "stars.h"

namespace stars {
// TI 6191.409 §5.3.2, MULTI FUNC A. The current adaptation uses inches of
// mercury.
QString STARSPane::modifyAltimeter(const QString& argument) {
  static const QRegularExpression syntax(
      QStringLiteral("^([A-Z0-9]{1,4}) +([0-9]{4})?([EA]?)$"));
  const auto match = syntax.match(argument.trimmed());
  if (!match.hasMatch()) return "FORMAT";

  const auto station = adaptedAltimeterStation(match.captured(1));
  if (station.isEmpty()) return "FORMAT";
  const auto digits = match.captured(2);
  const auto suffix = match.captured(3);
  if (digits.isEmpty()) {
    if (suffix != "A") return "FORMAT";
    enteredAltimeters_.remove(station);
    return {};
  }
  const int setting = digits.toInt();
  if (setting < 2700 || setting > 3200) return "ILL VALUE";
  const char mode = suffix.isEmpty() ? ' ' : suffix.front().toLatin1();
  enteredAltimeters_.insert(station, {setting, mode, altimeterSecond_});
  return {};
}

// TI 6191.409 §§5.3.3–11. Edits belong to this playback session.
QString STARSPane::modifyGeneralInfo(QString argument) {
  int index = 0;
  if (!argument.isEmpty() &&
      (argument.front().isDigit() || argument.front() == '.')) {
    const auto address = argument.section(' ', 0, 0);
    if (address.startsWith('.')) {
      const auto& airports = dcb_.giAirports();
      const auto airport = address.mid(1);
      const auto it = std::find(airports.begin() + 1, airports.end(), airport);
      if (airport.isEmpty() || it == airports.end()) return "ILL LINE";
      index = int(it - airports.begin());
    } else {
      if (address.size() != 1 || address == "0") return "ILL LINE";
      index = address.toInt();
    }
    argument = argument.mid(address.size()).trimmed();
  }
  auto next = gi_[index];
  if (argument.isEmpty())
    next = {};
  else {
    const auto first = argument.front();
    const bool code = first >= 'A' && first <= 'Z';
    const bool explicitCode =
        code && (!index || argument.size() == 1 || argument[1] == ' ' ||
                 argument.mid(1) == "*");
    int limit = index ? 32 : 30;
    if (first == '*' || explicitCode) {
      next.code = first == '*' ? QString{} : QString(first);
      argument.remove(0, 1);
      if (index && explicitCode) limit = 30;
      if (index && first == '*' && !argument.startsWith(' ')) limit = 31;
      if (explicitCode && argument == "*") {
        next.text.clear();
        argument.clear();
      }
      argument = argument.trimmed();
    } else if (!index)
      return "ILL ATIS";
    if (argument.size() > limit) return "FORMAT";
    if (!argument.isEmpty()) next.text = argument;
  }
  gi_[index] = std::move(next);
  return {};
}

bool STARSPane::quickLookCommand(QString argument) {
  const bool plus = argument.endsWith('+');
  if (plus) argument.chop(1);
  argument = argument.trimmed();
  auto& owners = prefs_.quickLook;
  if (argument == "ALL" && mode_ == Mode::Multi) {
    owners.insert("ALL", plus);
    response_ = plus ? "QL ALL+" : "QL ALL";
  } else if (argument.isEmpty() && mode_ == Mode::Multi) {
    owners.removeIf([plus](auto it) { return it.value() == plus; });
    response_.clear();
  } else {
    QStringList positions;
    static const QRegularExpression field(
        R"(^(?:[0-9][A-Z0-9])+$|^[A-Z][A-Z0-9]?$)");
    for (const auto& token : argument.split(' ', Qt::SkipEmptyParts)) {
      if (!field.match(token).hasMatch()) {
        response_ = "FORMAT";
        return true;
      }
      for (int i = 0; i < token.size(); i += 2) {
        auto id = token.mid(i, 2);
        if (id.size() == 1) id = tcp_.left(1) + id;
        positions.append(id);
      }
    }
    if (positions.isEmpty() || positions.size() > 10) {
      response_ = "FORMAT";
      return true;
    }
    for (const auto& id : positions) {
      if (!localTCPs_.contains(id) ||
          (id == tcp_ && (positions.size() > 1 || plus))) {
        response_ = "ILL POS";
        return true;
      }
    }
    if (positions.front() != tcp_) {
      owners.remove("ALL");
      for (const auto& id : positions) {
        if (owners.contains(id) && owners[id] == plus)
          owners.remove(id);
        else
          owners.insert(id, plus);
      }
    }
    response_ = quickLookLines().join('\n');
  }
  mode_ = Mode::None;
  input_.clear();
  return true;
}

void STARSPane::setAltimeters(aviation::Altimeters readings, qint64 second) {
  altimeterSecond_ = second;
  altimeters_ = std::move(readings);
  // An optional pressure supplied with A holds until the first later automatic
  // observation. Repeated deliveries of the cached report do not release it.
  enteredAltimeters_.removeIf(
      [this](QMap<QString, EnteredAltimeter>::iterator entry) {
        const auto& setting = entry.value();
        if (setting.mode != 'A') return false;
        const auto report = altimeters_.constFind(entry.key());
        return report != altimeters_.cend() && report->hundredths &&
               report->observed > setting.enteredAt;
      });
}
}  // namespace stars
