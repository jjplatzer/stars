#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

#include "stars.h"

namespace stars {
namespace {
template <class T>
struct Field {
  const char* key;
  int T::* member;
  int minimum, maximum;
};

constexpr Field<BrightnessPreferences> brightnessFields[]{
    {"dcb", &BrightnessPreferences::dcb, 25, 100},
    {"backgroundContrast", &BrightnessPreferences::backgroundContrast, 0, 100},
    {"videoGroupA", &BrightnessPreferences::videoGroupA, 5, 100},
    {"videoGroupB", &BrightnessPreferences::videoGroupB, 5, 100},
    {"fullDatablocks", &BrightnessPreferences::fullDatablocks, 0, 100},
    {"lists", &BrightnessPreferences::lists, 25, 100},
    {"positions", &BrightnessPreferences::positions, 0, 100},
    {"limitedDatablocks", &BrightnessPreferences::limitedDatablocks, 0, 100},
    {"otherTracks", &BrightnessPreferences::otherTracks, 0, 100},
    {"lines", &BrightnessPreferences::lines, 0, 100},
    {"rangeRings", &BrightnessPreferences::rangeRings, 0, 100},
    {"compass", &BrightnessPreferences::compass, 0, 100},
    {"beaconSymbols", &BrightnessPreferences::beaconSymbols, 0, 100},
    {"primarySymbols", &BrightnessPreferences::primarySymbols, 0, 100},
    {"history", &BrightnessPreferences::history, 0, 100},
    {"weather", &BrightnessPreferences::weather, 5, 100},
    {"wxContrast", &BrightnessPreferences::wxContrast, 5, 100},
};
constexpr Field<CharacterSizePreferences> sizeFields[]{
    {"datablocks", &CharacterSizePreferences::datablocks, 0, 5},
    {"lists", &CharacterSizePreferences::lists, 0, 5},
    {"dcb", &CharacterSizePreferences::dcb, 0, 2},
    {"tools", &CharacterSizePreferences::tools, 0, 5},
    {"positionSymbols", &CharacterSizePreferences::positionSymbols, 0, 5},
};
template <class T, size_t N>
QJsonObject encode(const T& value, const Field<T> (&fields)[N]) {
  QJsonObject object;
  for (const auto& f : fields) object.insert(f.key, value.*f.member);
  return object;
}
template <class T, size_t N>
void decode(const QJsonObject& object, T& value, const Field<T> (&fields)[N]) {
  for (const auto& f : fields) {
    const int n = object.value(f.key).toInt(value.*f.member);
    if (n >= f.minimum && n <= f.maximum) value.*f.member = n;
  }
}
std::optional<QPointF> point(const QJsonValue& value) {
  const auto xy = value.toArray();
  if (xy.size() != 2 || !xy[0].isDouble() || !xy[1].isDouble()) return {};
  return QPointF(xy[0].toDouble(), xy[1].toDouble());
}
}  // namespace
QJsonObject STARSPane::preferenceSnapshot() const {
  QJsonArray wx, maps;
  for (bool shown : prefs_.displayWeatherLevel) wx.append(shown);
  auto ids = visible_.values();
  std::sort(ids.begin(), ids.end());
  for (int id : ids) maps.append(id);
  auto fields = prefs_.ssaFields.values();
  fields.sort();
  QJsonObject snapshot{
      {"leaderDirection", prefs_.leaderDirection},
      {"leaderLength", prefs_.leaderLength},
      {"tpaMileage", prefs_.tpaMileage},
      {"atpaDistance", prefs_.atpaDistance},
      {"atpaAlertCones", prefs_.atpaAlertCones},
      {"atpaMonitorCones", prefs_.atpaMonitorCones},
      {"selectedBeacons", QJsonArray::fromStringList(prefs_.selectedBeacons)},
      {"historyCount", prefs_.historyCount},
      {"audioVolume", prefs_.audioVolume},
      {"ldbBeaconCodes", prefs_.ldbBeaconCodes},
      {"associatedAltitude",
       QJsonArray{prefs_.associatedAltitude[0], prefs_.associatedAltitude[1]}},
      {"unassociatedAltitude", QJsonArray{prefs_.unassociatedAltitude[0],
                                          prefs_.unassociatedAltitude[1]}},
      {"range", range_},
      {"center", QJsonArray{userCenter_.x(), userCenter_.y()}},
      {"offCenter", offCenter_},
      {"ringSpacing", prefs_.ringSpacing},
      {"useRingCenter", prefs_.useUserRingCenter},
      {"maps", maps},
      {"mapList", int(list_)},
      {"brightness", encode(prefs_.brightness, brightnessFields)},
      {"charSize", encode(prefs_.charSize, sizeFields)},
      {"weatherLevels", wx},
      {"allSsaFields", prefs_.allSsaFields},
      {"ssaFields", QJsonArray::fromStringList(fields)}};
  if (auto p = prefs_.userRingCenter)
    snapshot.insert("ringCenter", QJsonArray{p->x(), p->y()});
  QJsonArray gi;
  for (bool enabled : prefs_.giFilter) gi.append(enabled);
  snapshot.insert("giFilter", gi);
  QJsonObject owners;
  for (auto it = prefs_.quickLook.cbegin(); it != prefs_.quickLook.cend(); ++it)
    owners.insert(it.key(), it.value());
  snapshot.insert("quickLook", owners);
  QJsonObject lists;
  for (auto it = prefs_.lists.cbegin(); it != prefs_.lists.cend(); ++it)
    lists[it.key()] = QJsonObject{
        {"position", QJsonArray{it->position.x(), it->position.y()}},
        {"lines", it->lines},
        {"visible", it->visible}};
  snapshot.insert("lists", lists);
  QJsonObject restrictions;
  for (auto it = prefs_.restrictionDisplay.cbegin();
       it != prefs_.restrictionDisplay.cend(); ++it)
    restrictions[it.key()] = QJsonObject{{"hidden", it->hidden},
                                         {"hideText", it->hideText},
                                         {"forceBlink", it->forceBlink},
                                         {"stopBlink", it->stopBlink}};
  snapshot.insert("restrictionDisplay", restrictions);
  snapshot.insert("dwell", prefs_.dwell);
  snapshot.insert("ptlOwn", prefs_.ptlOwn);
  snapshot.insert("ptlAll", prefs_.ptlAll);
  snapshot.insert("ptlHalfMinutes", prefs_.ptlHalfMinutes);
  return snapshot;
}
void STARSPane::applyPreferenceSnapshot(const QJsonObject& s) {
  Preferences next;
  const auto restrictions = s.value("restrictionDisplay").toObject();
  for (const auto& area : restrictions_.areas()) {
    const auto value = restrictions.value(area.serial).toObject();
    if (!value.isEmpty())
      next.restrictionDisplay[area.serial] = {
          value["hidden"].toBool(), value["hideText"].toBool(),
          value["forceBlink"].toBool(), value["stopBlink"].toBool()};
  }
  const auto lists = s.value("lists").toObject();
  for (auto it = next.lists.begin(); it != next.lists.end(); ++it) {
    const auto saved = lists.value(it.key()).toObject();
    if (auto xy = point(saved["position"]);
        xy && QRectF(0, 0, 1, 1).contains(*xy))
      it->position = *xy;
    it->lines = std::clamp(saved["lines"].toInt(it->lines), 1, 100);
    it->visible = saved["visible"].toBool(it->visible);
  }
  next.leaderDirection = std::clamp(s.value("leaderDirection").toInt(0), 0, 7);
  next.leaderLength = std::clamp(s.value("leaderLength").toInt(1), 0, 7);
  next.audioVolume = std::clamp(s.value("audioVolume").toInt(10), 1, 10);
  next.historyCount = std::clamp(s.value("historyCount").toInt(5), 1, 10);
  for (const auto& item : s.value("selectedBeacons").toArray()) {
    const auto code = item.toString();
    if (Preferences::validBeacon(code) && !next.selectedBeacons.contains(code))
      next.selectedBeacons.append(code);
    if (next.selectedBeacons.size() == 10) break;
  }
  const auto gi = s.value("giFilter").toArray();
  for (int i = 0; i < std::min(10, int(gi.size())); ++i)
    next.giFilter[i] = gi[i].toBool(i == 0);
  const auto owners = s.value("quickLook").toObject();
  for (auto it = owners.begin(); it != owners.end(); ++it)
    if (it.value().isBool() &&
        (it.key() == "ALL" ||
         (localTCPs_.contains(it.key()) && it.key() != tcp_)))
      next.quickLook.insert(it.key(), it.value().toBool());
  next.ptlAll = s.value("ptlAll").toBool();
  next.dwell = std::clamp(s.value("dwell").toInt(1), 0, 2);
  next.ptlOwn = !next.ptlAll && s.value("ptlOwn").toBool();
  next.ptlHalfMinutes = std::clamp(s.value("ptlHalfMinutes").toInt(2), 0, 10);
  next.tpaMileage = s.value("tpaMileage").toBool(true);
  next.atpaDistance = s.value("atpaDistance").toBool(true);
  next.atpaAlertCones = s.value("atpaAlertCones").toBool(true);
  next.atpaMonitorCones = s.value("atpaMonitorCones").toBool(false);
  next.ldbBeaconCodes = s.value("ldbBeaconCodes").toBool(true);
  const auto filter = [&](QString key, std::array<int, 2>& range) {
    const auto a = s.value(key).toArray();
    if (a.size() != 2) return;
    const int low = a[0].toInt(-10000), high = a[1].toInt(-10000);
    if (low >= -9900 && low <= high && high <= 99900) range = {low, high};
  };
  filter("associatedAltitude", next.associatedAltitude);
  filter("unassociatedAltitude", next.unassociatedAltitude);
  decode(s.value("brightness").toObject(), next.brightness, brightnessFields);
  decode(s.value("charSize").toObject(), next.charSize, sizeFields);
  const int spacing = s.value("ringSpacing").toInt(5);
  if (spacing == 2 || spacing == 5 || spacing == 10 || spacing == 20)
    next.ringSpacing = spacing;
  next.userRingCenter = point(s.value("ringCenter"));
  next.useUserRingCenter = s.value("useRingCenter").toBool();
  const auto wx = s.value("weatherLevels").toArray();
  for (int i = 0; i < std::min(6, int(wx.size())); ++i)
    next.displayWeatherLevel[i] = wx[i].toBool(true);
  next.allSsaFields = s.value("allSsaFields").toBool();
  if (s.value("ssaFields").isArray()) {
    next.ssaFields.clear();
    for (const auto& field : s.value("ssaFields").toArray())
      next.ssaFields.insert(field.toString());
  }
  QSet<int> maps;
  for (const auto& value : s.value("maps").toArray()) {
    const int id = value.toInt();
    if (!maps_.contains(id)) continue;  // Removed in a later adaptation.
    ensureBundle();
    if (maps_[id].commands && maps.size() < 50) maps.insert(id);
  }
  visible_ = std::move(maps);
  prefs_ = std::move(next);
  range_ = std::clamp(s.value("range").toDouble(initialRangeNM), 6., 512.);
  userCenter_ = point(s.value("center")).value_or(defaultCenter_);
  offCenter_ = s.value("offCenter").toBool();
  placing_ = false;
  const int category = s.value("mapList").toInt();
  list_ = category == int(List::Current) ? List::Current : List::Geographic;
  if (!lists.contains("TX"))
    prefs_.lists["TX"].visible =
        category == int(List::Current) || category == int(List::Geographic);
}

QString Dcb::setName(int slot) const {
  return sets_.value(QString::number(slot)).toObject().value("name").toString();
}
void Dcb::openPref() {
  finishAdjustment();
  if (radar_) entrySnapshot_ = radar_->preferenceSnapshot();
  entrySlot_ = selectedSlot_;
  page_ = Page::Pref;
}
void Dcb::commitSet(int slot, const QJsonObject& set) {
  auto updated = sets_;
  const auto key = QString::number(slot);
  if (set.isEmpty())
    updated.remove(key);
  else
    updated.insert(key, set);
  if (saveSets_) saveSets_(sets_, updated);
  sets_ = std::move(updated);
}
void Dcb::defaultPref() {
  radar_->applyPreferenceSnapshot({});
  selectedSlot_ = 0;
}
void Dcb::prefAction(const QString& action) {
  finishAdjustment();
  try {
    if (action == "SAVE AS") {
      saveAs_ = true;
      return;
    }
    if (action == "DEFAULT") {
      defaultPref();
    } else if (action == "RESTORE") {
      radar_->applyPreferenceSnapshot(entrySnapshot_);
      selectedSlot_ = entrySlot_;
    } else if (action == "SAVE") {
      commitSet(selectedSlot_, {{"name", setName(selectedSlot_)},
                                {"display", radar_->preferenceSnapshot()}});
    } else if (action == "DELETE") {
      commitSet(selectedSlot_, {});
      if (entrySlot_ == selectedSlot_) entrySlot_ = 0;
      selectedSlot_ = 0;
    } else {
      const int slot = action.section('\n', 0, 0).toInt();
      if (slot < 1 || slot > 32 || setName(slot).isEmpty()) {
        error_ = "ILL PREFSET";
        return;
      }
      radar_->applyPreferenceSnapshot(sets_.value(QString::number(slot))
                                          .toObject()
                                          .value("display")
                                          .toObject());
      selectedSlot_ = slot;
    }
  } catch (const std::exception& e) {
    error_ = QString::fromUtf8(e.what());
  }
}
bool Dcb::prefKey(const QString& key) {
  if (key == "Backspace") {
    input_.chop(1);
    error_.clear();
    return true;
  }
  if (key != "Enter") {
    if (key.size() == 1 && key.front().isPrint()) {
      if (!error_.isEmpty()) input_.clear();
      error_.clear();
      input_ += key.toUpper();
    }
    return true;
  }
  input_ = input_.trimmed();
  static const QRegularExpression namePattern("^[A-Z0-9]{1,7}$");
  if (!namePattern.match(input_).hasMatch()) {
    error_ = "FORMAT";
    return true;
  }
  int freeSlot = 0, matching = 0;
  for (int slot = 1; slot <= 32; ++slot) {
    const auto name = setName(slot);
    if (!freeSlot && name.isEmpty()) freeSlot = slot;
    if (name == input_) matching = slot;
  }
  bool numeric = false;
  const int number = input_.toInt(&numeric);
  if (!saveAs_) {
    if (numeric && (number < 1 || number > 32))
      error_ = "FORMAT";
    else if (numeric)
      prefAction(QString::number(number));
    else if (matching)
      prefAction(QString::number(matching));
    else
      error_ = "ILL PREFSET";
  } else if (matching || (number >= 1 && number <= 32))
    error_ = "ILL PREFSET";
  else if (!freeSlot)
    error_ = "CAPACITY";
  else {
    try {
      commitSet(freeSlot,
                {{"name", input_}, {"display", radar_->preferenceSnapshot()}});
      selectedSlot_ = freeSlot;
      finishAdjustment();
    } catch (const std::exception& e) {
      error_ = QString::fromUtf8(e.what());
    }
  }
  return true;
}
}  // namespace stars
