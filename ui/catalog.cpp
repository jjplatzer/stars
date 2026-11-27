#include "catalog.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <optional>
#include <stdexcept>

#include "../util/resources.h"

namespace stars::ui {
using util::readCompressedResource;
namespace {

QJsonObject readJson(const QString& path) {
  QJsonParseError error;
  const auto document =
      QJsonDocument::fromJson(readCompressedResource(path), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    throw std::runtime_error(
        (path + QStringLiteral(": Invalid JSON: ") + error.errorString())
            .toStdString());
  }
  return document.object();
}

void checkVersion(const QJsonObject& object) {
  if (object.value("version").toInt() != 1) {
    throw std::runtime_error(
        "Unsupported facility metadata or CRC configuration version");
  }
}

QString displayName(const QString& name) {
  // NASR names are uppercase. Keep aviation acronyms and single-letter
  // initials.
  static const QSet<QString> acronyms = {
      "TRACON", "ARTCC", "ATCT", "RAPCON", "RATCF", "CERAP", "AFB",
      "AAF",    "NAS",   "MCAS", "ANG",    "JRB",   "FAA"};
  QString result;
  QString word;
  const auto appendWord = [&] {
    if (word.isEmpty()) return;
    if (word.size() == 1 || acronyms.contains(word.toUpper())) {
      result += word.toUpper();
    } else {
      result += word.first(1).toUpper() + word.sliced(1).toLower();
    }
    word.clear();
  };
  for (const auto ch : name) {
    if (ch.isLetterOrNumber())
      word += ch;
    else {
      appendWord();
      result += ch;
    }
  }
  appendWord();
  return result;
}

std::optional<System> positionSystem(const QString& callsign) {
  // ColorSet is a palette, not a role: CRC has APP/Tdw and TWR/Tcw entries.
  const auto role = callsign.section('_', -1).toUpper();
  if (role == "APP" || role == "DEP") return System::Tcw;
  if (role == "TWR") return System::Tdw;
  return std::nullopt;
}

QList<SelectionOption> sorted(QList<SelectionOption> options) {
  std::sort(options.begin(), options.end(), [](const auto& a, const auto& b) {
    const auto comparison =
        QString::compare(a.label, b.label, Qt::CaseInsensitive);
    return comparison == 0 ? a.id < b.id : comparison < 0;
  });
  return options;
}

}  // namespace

ProfileCatalog ProfileCatalog::load() {
  QList<QJsonObject> configurations;
  const QDir directory(QStringLiteral(":/resources/config"));
  for (const auto& file :
       directory.entryList({"*.json.zst"}, QDir::Files, QDir::Name)) {
    configurations.append(readJson(directory.filePath(file)));
  }
  if (configurations.isEmpty())
    throw std::runtime_error("No embedded CRC configurations found");
  return ProfileCatalog(
      readJson(QStringLiteral(":/resources/meta/atc.json.zst")),
      configurations);
}

ProfileCatalog::ProfileCatalog(const QJsonObject& metadata,
                               const QList<QJsonObject>& configurations) {
  checkVersion(metadata);
  for (const auto& value : metadata.value("facilities").toArray()) {
    const auto facility = value.toObject();
    names_[facility.value("id").toString()][facility.value("type").toString()] =
        facility.value("name").toString();
  }
  for (const auto& config : configurations) {
    checkVersion(config);
    const auto adaptation = config.value("facility").toString();
    const auto artcc = config.value("artcc").toString();
    configurationNames_[adaptation] = config.value("name").toString();
    QMap<QString, QString> tcpCodes;
    for (const auto& value : config.value("starsConfiguration")
                                 .toObject()
                                 .value("tcps")
                                 .toArray()) {
      const auto tcp = value.toObject();
      tcpCodes[tcp.value("id").toString()] =
          QString::number(tcp.value("subset").toInt()) +
          tcp.value("sectorId").toString();
    }
    for (const auto& value : config.value("positions").toArray()) {
      const auto entry = value.toObject();
      const auto position = entry.value("position").toObject();
      const auto callsign = position.value("callsign").toString();
      const auto system = positionSystem(callsign);
      if (!system) continue;
      const auto tcpId = position.value("starsConfiguration")
                             .toObject()
                             .value("tcpId")
                             .toString();
      // Radio-only/guard entries without an assigned TCP cannot be selected
      // as a STARS controller position. Never fabricate a TCP from a callsign.
      if (tcpId.isEmpty() || !tcpCodes.contains(tcpId)) continue;
      positions_.append({position.value("id").toString(), artcc,
                         entry.value("facility").toString(), adaptation,
                         tcpCodes.value(tcpId),
                         position.value("name").toString(), callsign, *system,
                         position.value("starred").toBool()});
    }
  }
}

QString ProfileCatalog::facilityName(const QString& id,
                                     const QStringList& preferredTypes,
                                     const QString& fallback) const {
  const auto records = names_.value(id);
  for (const auto& type : preferredTypes) {
    if (records.contains(type)) return displayName(records.value(type));
  }
  if (!fallback.isEmpty()) return fallback;
  return id;
}

QList<SelectionOption> ProfileCatalog::artccs(System system) const {
  QSet<QString> ids;
  for (const auto& position : positions_)
    if (position.system == system) ids.insert(position.artcc);
  QList<SelectionOption> result;
  for (const auto& id : ids) {
    const auto name = facilityName(id, {"ARTCC", "CERAP"}, id);
    result.append({id, id + " (" + name + ")", {}, false});
  }
  return sorted(result);
}

QList<SelectionOption> ProfileCatalog::facilities(System system,
                                                  const QString& artcc) const {
  QSet<QString> ids;
  for (const auto& position : positions_) {
    if (position.system == system && position.artcc == artcc)
      ids.insert(position.facility);
  }
  QList<SelectionOption> result;
  for (const auto& id : ids) {
    const QStringList types =
        system == System::Tcw
            ? QStringList{"TRACON",     "ATCT-TRACON", "ATCT-RAPCON",
                          "ATCT-RATCF", "ATCT-A/C",    "ATCT",
                          "CERAP"}
            : QStringList{"ATCT",       "ATCT-TRACON", "ATCT-RAPCON",
                          "ATCT-RATCF", "ATCT-A/C",    "TRACON",
                          "CERAP"};
    const auto name = facilityName(id, types, configurationNames_.value(id));
    result.append({id, id + " (" + name + ")", {}, false});
  }
  return sorted(result);
}

QList<SelectionOption> ProfileCatalog::positions(
    System system, const QString& artcc, const QString& facility) const {
  QList<SelectionOption> result;
  for (const auto& position : positions_) {
    if (position.system != system || position.artcc != artcc ||
        position.facility != facility)
      continue;
    result.append({position.adaptation + "/" + position.id,
                   position.tcp + " (" + position.name + ")", position.callsign,
                   position.starred});
  }
  return sorted(result);
}

}  // namespace stars::ui
