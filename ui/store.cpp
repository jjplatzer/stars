#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <stdexcept>

namespace stars::ui {
namespace {
void validate(const Profile& profile) {
  if (profile.id.isEmpty() || profile.name.trimmed().isEmpty() ||
      (profile.system != "TCW" && profile.system != "TDW") ||
      profile.artcc.isEmpty() || profile.facility.isEmpty() ||
      profile.position.section('/', 0, 0).isEmpty() ||
      profile.position.section('/', 1, 1).isEmpty()) {
    throw std::runtime_error("Invalid or incomplete profile");
  }
}
}  // namespace

ProfileStore::ProfileStore(const QString& path)
    : path_(path.isEmpty() ? QStandardPaths::writableLocation(
                                 QStandardPaths::AppDataLocation) +
                                 "/profiles.json"
                           : path) {}

QList<Profile> ProfileStore::load() const {
  QFile file(path_);
  if (!file.exists()) return {};
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Cannot read saved profiles");
  const auto document = QJsonDocument::fromJson(file.readAll());
  const auto root = document.object();
  if (root.value("version").toInt() != 1 || !root.value("profiles").isArray()) {
    throw std::runtime_error("Invalid or unsupported profiles file");
  }
  QList<Profile> profiles;
  for (const auto& value : root.value("profiles").toArray()) {
    const auto object = value.toObject();
    Profile profile{
        object.value("id").toString(),
        object.value("name").toString(),
        object.value("system").toString(),
        object.value("artcc").toString(),
        object.value("facility").toString(),
        object.value("position").toString(),
        QDate::fromString(object.value("created_date").toString(), Qt::ISODate),
        object.value("tcp").toString()};
    const auto stored = object.value("colors");
    if (!stored.isUndefined() && !stored.isObject())
      throw std::runtime_error("Invalid profile palette");
    auto colors = stored.toObject();
    if (object.contains("bkc_ceiling") && !colors.contains("BackgroundCeiling"))
      colors.insert("BackgroundCeiling", object.value("bkc_ceiling"));
    for (auto it = colors.begin(); it != colors.end(); ++it) {
      QColor color(it.value().toString());
      if (!color.isValid()) throw std::runtime_error("Invalid profile color");
      profile.colors.insert(it.key(), color);
    }
    const auto font = object.value("font_set").toString("B");
    if (font != "A" && font != "B")
      throw std::runtime_error("Invalid profile font set");
    profile.fontSetB = font == "B";
    const auto prefSets = object.value("pref_sets");
    if (!prefSets.isUndefined() && !prefSets.isObject())
      throw std::runtime_error("Invalid profile preference sets");
    profile.prefSets = prefSets.toObject();
    profile.faaStipple = object.value("faa_wx_stipple").toBool(false);
    for (const auto& code : object.value("subset_symbols").toArray())
      if (code.isString()) profile.subsetSymbols.append(code.toString());
    validate(profile);
    profiles.append(profile);
  }
  return profiles;
}

QList<Profile> ProfileStore::create(Profile profile) const {
  profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  profile.name = profile.name.trimmed();
  profile.createdDate = QDate::currentDate();
  validate(profile);
  return update([&](QList<Profile>& profiles) { profiles.append(profile); });
}

QList<Profile> ProfileStore::rename(const QString& id,
                                    const QString& name) const {
  if (name.trimmed().isEmpty() || name.trimmed().size() > 120)
    throw std::runtime_error(
        "Enter a profile name between 1 and 120 characters");
  return update([&](QList<Profile>& profiles) {
    for (auto& profile : profiles)
      if (profile.id == id) {
        profile.name = name.trimmed();
        return;
      }
    throw std::runtime_error("Profile no longer exists");
  });
}

void ProfileStore::setPrefSets(const QString& id, const QJsonObject& previous,
                               const QJsonObject& updated) const {
  update([&](QList<Profile>& profiles) {
    for (auto& profile : profiles)
      if (profile.id == id) {
        if (profile.prefSets != previous)
          throw std::runtime_error("PREF SETS CHANGED - REOPEN PROFILE");
        profile.prefSets = updated;
        return;
      }
    throw std::runtime_error("Profile no longer exists");
  });
}

void ProfileStore::setSubsetSymbols(const QString& id,
                                    const QStringList& codes) const {
  update([&](QList<Profile>& profiles) {
    for (auto& profile : profiles)
      if (profile.id == id) {
        profile.subsetSymbols = codes;
        return;
      }
    throw std::runtime_error("Profile no longer exists");
  });
}

void ProfileStore::setFaaStipple(const QString& id, bool enabled) const {
  update([&](QList<Profile>& profiles) {
    for (auto& profile : profiles)
      if (profile.id == id) {
        profile.faaStipple = enabled;
        return;
      }
    throw std::runtime_error("Profile no longer exists");
  });
}

void ProfileStore::setFontSetB(const QString& id, bool setB) const {
  update([&](QList<Profile>& profiles) {
    for (auto& profile : profiles)
      if (profile.id == id) {
        profile.fontSetB = setB;
        return;
      }
    throw std::runtime_error("Profile no longer exists");
  });
}

void ProfileStore::setColors(const QString& id,
                             const ColorOverrides& changes) const {
  update([&](QList<Profile>& profiles) {
    for (auto& profile : profiles)
      if (profile.id == id) {
        applyColorChanges(profile.colors, changes);
        return;
      }
    throw std::runtime_error("Profile no longer exists");
  });
}

QList<Profile> ProfileStore::remove(const QString& id) const {
  return update([&](QList<Profile>& profiles) {
    for (qsizetype i = 0; i < profiles.size(); ++i)
      if (profiles[i].id == id) {
        profiles.removeAt(i);
        return;
      }
    throw std::runtime_error("Profile no longer exists");
  });
}

QList<Profile> ProfileStore::update(
    const std::function<void(QList<Profile>&)>& mutate) const {
  if (!QDir().mkpath(QFileInfo(path_).absolutePath()))
    throw std::runtime_error("Cannot create profile directory");
  QLockFile lock(path_ + ".lock");
  if (!lock.tryLock())
    throw std::runtime_error(
        "Profiles are being updated by another STARS window. Please try "
        "again.");
  // Reload under the lock so two application windows cannot overwrite each
  // other.
  auto profiles = load();
  mutate(profiles);
  QJsonArray array;
  for (const auto& entry : profiles) {
    QJsonObject colors;
    for (auto it = entry.colors.cbegin(); it != entry.colors.cend(); ++it)
      colors.insert(it.key(), it->name());
    array.append(QJsonObject{
        {"id", entry.id},
        {"name", entry.name},
        {"system", entry.system},
        {"artcc", entry.artcc},
        {"facility", entry.facility},
        {"position", entry.position},
        {"created_date",
         entry.createdDate.isValid()
             ? QJsonValue(entry.createdDate.toString(Qt::ISODate))
             : QJsonValue()},
        {"subset_symbols", QJsonArray::fromStringList(entry.subsetSymbols)},
        {"tcp", entry.tcp},
        {"font_set", entry.fontSetB ? "B" : "A"},
        {"colors", colors},
        {"faa_wx_stipple", entry.faaStipple},
        {"pref_sets", entry.prefSets}});
  }
  const auto data =
      QJsonDocument(QJsonObject{{"version", 1}, {"profiles", array}}).toJson();
  QSaveFile file(path_);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
      !file.commit()) {
    throw std::runtime_error("Cannot save profiles");
  }
  return profiles;
}

}  // namespace stars::ui
