#pragma once
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <optional>

#include "../aviation/altimeter.h"
#include "../radar/latlon.h"
#include "../renderer/font.h"
#include "atpa.h"
#include "colors.h"
#include "crda.h"
#include "datablock.h"
#include "dcb.h"
#include "font.h"
#include "fusion.h"
#include "msaw.h"
#include "net/track.h"
#include "nexrad.h"
#include "restrictions.h"

namespace stars {
// Geographic state is independent of window pixels and survives resize/DPI
// changes.
class STARSPane {
 public:
  static constexpr int initialRangeNM = 50;
  enum class List { None, Geographic, Current };
  STARSPane();
  explicit STARSPane(const QString& position, const QString& tcp = {});
  STARSPane(const STARSPane&) = delete;
  STARSPane& operator=(const STARSPane&) = delete;
  STARSPane(STARSPane&&) = delete;
  STARSPane& operator=(STARSPane&&) = delete;
  STARSPane(const QJsonObject& config, const QString& position,
            const QString& tcp = {});
  static STARSPane load(const QString& position);
  static std::shared_ptr<const renderer::CmdBuffer> geometry(
      const QJsonObject& geojson);
  void loadBundle(const QJsonObject& bundle);
  void toggleMap(int id);
  bool visible(int id) const { return visible_.contains(id); }
  void clearMaps() { visible_.clear(); }
  void toggleList(List list) {
    auto& visible = prefs_.lists["TX"].visible;
    visible = !visible || list_ != list;
    list_ = list;
  }
  List list() const {
    return prefs_.lists.value("TX").visible ? list_ : List::None;
  }
  QString listText() const;
  double range() const { return range_; }
  QPointF center() const {
    return offCenter_ ? userCenter_ : defaultCenter_;
  }  // lon, lat
  bool offCenter() const { return offCenter_; }
  void toggleCenter() { offCenter_ = !offCenter_; }
  bool placingCenter() const { return placing_; }
  void placeCenter() {
    placing_ = !placing_;
    if (placing_) offCenter_ = true;
  }
  void finishCenter() { placing_ = false; }
  QPointF geographic(QPointF pixel, QSizeF size) const;
  QPointF screen(QPointF lonlat, QSizeF size) const;
  renderer::Mat4 projection(QSizeF size) const;
  void pan(QPointF delta, QSizeF size);
  void zoom(double wheel, bool control, QPointF mouse, QSizeF size);
  void draw(renderer::CmdBuffer& cb, const Fonts& fonts, int width, int height,
            int pixelWidth, int pixelHeight, int top);
  const QString& artcc() const { return artcc_; }
  QRectF weatherBounds(QSizeF size) const;
  QStringList altimeterStations() const;
  QStringList facilityAltimeterStations() const;
  QStringList systemListText(const QString& id) const;
  void resetTraffic();
  void previewVolume() { volumePreview_.setRemainingTime(250); }
  bool previewingVolume() const { return !volumePreview_.hasExpired(); }
  bool msawAudioWanted() const;
  bool alertEnabled(const QString& function) const;
  Datablock trackDatablock(const net::RadarTrack& track) const;
  bool atpaEnabled() const { return atpa_.enabled(); }
  void setAtpaDisplay(QChar control, bool on) {
    atpa_.setDisplay(control, on, prefs_);
  }
  bool trafficInitialized() const { return traffic_.initialized(); }
  void setReconstructionMode(ReconstructionMode mode) { fusion_.setMode(mode); }
  ReconstructionMode reconstructionMode() const { return fusion_.mode(); }
  QPointF displayPosition(const net::RadarTrack& target) const;
  void updateTraffic(const net::TaisCache& cache, qint64 ms);
  bool useTaisLeaderDirection() const { return taisLeaderDirection_; }
  void toggleTaisLeaderDirection() {
    taisLeaderDirection_ = !taisLeaderDirection_;
    if (taisLeaderDirection_) dcb_.cancelLeaderAdjustment();
  }
  bool assignedAltitudesHidden() const { return hideAssignedAltitudes_; }
  void toggleAssignedAltitudes() {
    hideAssignedAltitudes_ = !hideAssignedAltitudes_;
  }
  void setBeaconReadout(bool held) { beaconReadout_ = held; }
  void beginPlayback(qint64 second);
  void setAltimeters(aviation::Altimeters readings, qint64 second);
  struct AirportSetting {
    QString text;
    bool missing;
  };
  QList<AirportSetting> airportSettings() const;
  QString systemSetting() const;
  struct SsaText {
    QString text;
    renderer::RGB color;
    bool blink = false;
  };
  using SsaRow = QList<SsaText>;
  QList<SsaRow> ssaRows() const;
  void setWeather(WeatherPictures pictures) { weather_ = std::move(pictures); }
  void resetWeather();
  QString positionFacility() const { return positionFacility_; }
  QStringList positionCodes() const {
    auto codes = localTCPs_.values();
    codes.sort();
    return codes;
  }
  void setSubsetSymbols(QStringList codes) {
    subsetSymbols_ = std::move(codes);
  }
  void setFaaStipple(bool enabled) { faaStipple_ = enabled; }
  bool weatherAvailable(int level) const {
    return weather_[0] && weather_[0]->levels.at(level - 1).fill;
  }
  const colors::Palette& palette() const { return palette_; }
  const colors::Palette& defaultPalette() const { return *defaultPalette_; }
  void setPalette(const colors::Palette& palette) { palette_ = palette; }
  renderer::RGB mapColor(int id) const;
  QJsonObject preferenceSnapshot() const;
  void applyPreferenceSnapshot(const QJsonObject& snapshot);
  void configurePrefSets(QJsonObject sets, SavePrefSets save) {
    dcb_.configurePrefSets(std::move(sets), std::move(save));
  }
  Preferences& preferences() { return prefs_; }
  const Preferences& preferences() const { return prefs_; }
  bool adjusting() const { return dcb_.adjusting(); }
  bool key(const QString& key);
  int dcbHeight() const { return showDcb_ ? Dcb::height : 0; }
  QString preview() const;
  void setRecordingTime(QString utc) { recordingTime_ = std::move(utc); }
  struct Track {
    QPointF location;
    QString acid, assignedBeacon;
    double groundSpeed{};
    bool associated{};
  };
  void setTracks(QMap<QString, Track> tracks);
  // Input is pane-local, excluding the application's title bar. Window
  // borders and platform event normalization belong to the host.
  void hover(QPointF mouse) {
    cursor_ = mouse;
    dcb_.motion(mouse);
  }
  void motion(QPointF mouse, QPointF movement, QSizeF size, bool rightButton,
              bool canPan);
  void wheel(QPointF mouse, float delta, bool control, QSizeF size);
  void press(QPointF mouse, QSizeF size);
  void release(QPointF mouse, QSizeF size = {});
  void cancelPointer() {
    scopePressed_ = false;
    hover({-1, -1});
    dcb_.release();
  }

 private:
  Preferences prefs_;
  QDeadlineTimer volumePreview_{0};
  Dcb dcb_;
  bool showDcb_ = true, scopePressed_ = false;
  QPointF cursor_{-1, -1};
  QString input_, response_;
  enum class Mode { None, Multi, CA, Min, Restriction };
  Mode mode_ = Mode::None;
  struct Acknowledgement {
    QString code, address;
    qint64 firstMrt, at;
    bool applies(const net::RadarTrack& track, qint64 ms) const {
      return code == track.report.beacon && address == track.report.address &&
             at <= ms && at >= track.beaconSinceMs && at >= track.firstMrtMs &&
             track.report.mrtMs >= firstMrt;
    }
  };
  struct LowAltitudeEvent {
    QString address, owner;
    qint64 firstMrt{}, association{};
    std::optional<qint64> since;
    bool acknowledged{}, suppressed{}, disabled{};
    bool matches(const net::RadarTrack& track) const {
      return address == track.report.address && firstMrt == track.firstMrtMs &&
             association == track.associationSinceMs && owner == track.owner;
    }
  };
  std::optional<MsawAirspace> msawAirspace_;
  std::optional<qint64> msawSync_;
  struct ApproachObservation {
    QString procedure, airport, address;
    qint64 firstMrt{}, association{}, start{}, last{};
    QPointF startPosition;
    double startAlong{}, lastAlong{};
  };
  QMap<QString, ApproachObservation> msawApproaches_;
  bool establishedOnFinal(const net::RadarTrack& track);
  bool lowAltitudeCondition(const net::RadarTrack& track);
  bool msawEvaluated_ = false;
  void updateMsaw(bool changed);
  const LowAltitudeEvent* msawAlert(const net::RadarTrack& track) const;
  bool msawCommand(const QString& key);
  struct AlertSite {
    QMap<QString, LowAltitudeEvent> lowAltitude;
    QSet<QString> inhibited;
    QMap<QString, Acknowledgement> acknowledged;
  };
  std::shared_ptr<AlertSite> alertSite_ = std::make_shared<AlertSite>();
  struct SlewSelection {
    qint64 firstMrt{}, associationSince{};
    QString address;
    std::optional<bool> beacon;
    QDeadlineTimer readout{0};
    QString planId, acid;
    std::optional<int> direction;
    std::optional<bool> requestedAltitude;
    bool singlePtl = false, quickLook = false;
    std::optional<Acknowledgement> acknowledged;
  };
  QMap<QString, SlewSelection> selections_;
  void configureAlertSite();
  bool supervisorCommand();
  QString closestTrack(QPointF pixel, QSizeF size) const;
  Crda crda_;
  struct GhostHit {
    int pair = -1;
    QPointF center;
    QList<QRectF> labels;
  };
  QMap<QString, Crda::Selection> ghostSelections_;
  QMap<QString, GhostHit> ghostHits_;
  void drawCrdaDiagrams(renderer::CmdBuffer& cb, QSizeF size, float dpi);
  void drawCrdaGhosts(renderer::CmdBuffer& cb, const Fonts& fonts, QSizeF size,
                      float dpi);
  bool crdaSlew(QPointF mouse, const QString& key);
  bool crdaCommand();
  Restrictions restrictions_;
  void restrictionCommand(std::optional<QPointF> point = {});
  void drawRestrictions(renderer::CmdBuffer& cb, const Fonts& fonts,
                        QSizeF size, float dpi) const;
  bool toolSlew(const QString& key, QPointF point);
  bool automaticPtl(const net::RadarTrack& track) const;
  void updateDwell(QSizeF size);
  void drawMinSep(renderer::CmdBuffer& cb, const Fonts& fonts, QSizeF size,
                  float dpi) const;
  QString minFirst_, dwellTrack_;
  qint64 minPendingGeneration_{};
  std::array<QString, 2> minPair_;
  std::array<qint64, 2> minGeneration_{};
  QPointF dwellCursor_{-1, -1};
  qint64 dwellGeneration_{};
  void selectTrack(const QString& key);
  void pruneSelections();
  void clearBeaconOverrides();
  struct Endpoint {
    QPointF location;
    QString track;
  };
  std::optional<Endpoint> start_;
  std::vector<std::array<Endpoint, 2>> lines_;
  QMap<QString, Track> tracks_;
  void designate(Endpoint endpoint);
  void slew(QPointF mouse, QSizeF size);
  void drawRangeBearingLines(renderer::CmdBuffer& cb, const Fonts& fonts,
                             int width, int height, float dpi) const;
  void drawPreviewArea(renderer::CmdBuffer& cb, const Fonts& fonts, int width,
                       int height) const;
  void drawRangeRings(renderer::CmdBuffer& cb, int width, int height,
                      float dpi) const;
  void drawCompass(renderer::CmdBuffer& cb, const Fonts& fonts, int width,
                   int height, int pixelWidth, int pixelHeight) const;
  QString recordingTime_, primaryAirport_;
  struct AirportStation {
    QString id, label;
  };
  QList<AirportStation> ssaAirports_, facilityStations_;
  struct TowerList {
    QString airport;
    QPointF location;
    double range;
  };
  QList<TowerList> towerLists_;
  QSet<QString> areaAirports_;
  QMap<QString, QStringList> listRows_;
  QMap<QString, int> listNumbers_;
  int nextListNumber_ = 0;
  void updateListRows();
  QString airportKey(const QString& raw) const;
  bool setupList(std::optional<QPointF> position = {});
  void drawSystemLists(renderer::CmdBuffer& cb, const Fonts& fonts,
                       QSizeF size) const;
  aviation::Altimeters altimeters_;
  qint64 altimeterSecond_{};
  struct EnteredAltimeter {
    int hundredths;
    char mode;
    qint64 enteredAt;
  };
  QMap<QString, EnteredAltimeter> enteredAltimeters_;
  struct StationSetting {
    int hundredths;
    char status;
    bool missing;
  };
  std::optional<StationSetting> stationSetting(const QString& id) const;
  QString adaptedAltimeterStation(const QString& code) const;
  struct GeneralInfo {
    QString code, text;
  };
  std::array<GeneralInfo, 10> gi_;
  bool archiveAvailable_ = false;
  QSet<QString> activeConditions_;
  QStringList giLines() const;
  QStringList quickLookLines() const;
  QString modifyGeneralInfo(QString argument);
  bool quickLookCommand(QString argument);
  QString modifyAltimeter(const QString& argument);

  void drawSSA(renderer::CmdBuffer& cb, const Fonts& fonts, int width,
               int height) const;
  void configure(const QJsonObject& config, const QString& position,
                 const QString& tcp);
  void drawVideoMaps(renderer::CmdBuffer& cb, float dpi) const;
  void drawVideoMapsList(renderer::CmdBuffer& cb,
                         const renderer::FontSize& font,
                         renderer::TextureID texture, int width,
                         int height) const;
  struct Map {
    QString id, name, shortName, category;
    std::shared_ptr<const renderer::CmdBuffer> commands;
  };
  QMap<int, Map> maps_;
  std::vector<std::pair<int, Map>> listing_;
  QSet<int> visible_;
  QString positionFacility_, facility_, artcc_, tcp_;
  QStringList subsetSymbols_;
  QSet<QString> localTCPs_;
  bool taisLeaderDirection_ = false, hideAssignedAltitudes_ = false;
  bool vfrSymbol_ = true, beaconReadout_ = false, requestedAltitude_ = false;
  QString altitudeFilters(bool ssa) const;
  Atpa atpa_;
  net::Traffic traffic_;
  Fusion fusion_;
  bool atpaCommand(bool selected = false, const QString& track = {});
  void drawAtpa(renderer::CmdBuffer&, const Fonts&, QSizeF, float) const;
  qint64 trafficMs_{};
  void drawTraffic(renderer::CmdBuffer&, const Fonts&, QSizeF, float) const;
  bool faaStipple_ = false;
  WeatherPictures weather_, historyPictures_;
  QElapsedTimer historyTimer_;
  int historyIndex_ = 0;
  void updateWeatherHistory();
  const colors::Palette* defaultPalette_ = &colors::tcw;
  colors::Palette palette_ = colors::tcw;
  void drawWeather(renderer::CmdBuffer& cb, QSizeF size) const;
  QPointF defaultCenter_, userCenter_;
  double range_ = initialRangeNM, longitudeScale_ = 1;
  bool offCenter_ = false, placing_ = false, bundleLoaded_ = false;
  List list_ = List::Geographic;
  radar::LatLonTransformations transformations(QSizeF size) const;
  void ensureBundle();
};
}  // namespace stars
