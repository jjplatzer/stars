#include "itws.h"

#include <QDebug>
#include <QSet>
#include <QtConcurrentRun>

#include "../stars/itws.h"

namespace stars::ui {
QString itwsSite(const QString& facility, const QString& positionFacility) {
  static const auto sites = QStringLiteral(STARS_ITWS_SITES).split(',');
  for (const auto& code : {positionFacility, facility})
    if (sites.contains(code)) return code;
  // Some ITWS partitions carry an airport identifier for the serving TRACON.
  static const QMap<QString, QString> aliases{
      {"A80", "ATL"}, {"D21", "DTW"}, {"L30", "LAS"}, {"F11", "MCO"},
      {"M03", "MEM"}, {"P50", "PHX"}, {"S56", "SLC"}, {"ZSU", "SJU"}};
  const auto site = aliases.value(facility);
  return sites.contains(site) ? site : QString{};
}

struct ItwsArchive::State {
  struct Entry {
    std::optional<ItwsGrid> grid;
    Weather::Frame frame;
  };
  using Key = std::pair<QString, qint64>;
  QMap<Key, Entry> decoded;
  Result picture(const QList<net::Delivery>& deliveries, qint64 now,
                 quint64 revision) {
    Result result{{}, now, now + 60000, revision};
    QMap<QString, QMap<qint64, Key>> bySource;
    QSet<Key> retained, displayed;
    for (const auto& d : deliveries) {
      const auto received = (d.receivedNs + 999999) / 1000000;
      if (received > now) {
        result.until = std::min(result.until, received);
        break;
      }
      Key key{d.writer, d.sequence};
      retained.insert(key);
      if (!decoded.contains(key)) {
        auto& entry = decoded[key];
        try {
          entry.grid = decodeItws(d.body);
        } catch (const std::exception& error) {
          qWarning() << "Invalid ITWS product:" << error.what();
        }
      }
      const auto& grid = decoded.constFind(key)->grid;
      if (!grid) continue;
      if (grid->generated > now) {
        result.until = std::min(result.until, grid->generated);
        continue;
      }
      bySource[grid->source].insert(grid->generated, key);
    }
    decoded.removeIf([&](auto it) { return !retained.contains(it.key()); });
    std::array<QList<Weather::Frame>, 3> layers;
    for (const auto& scans : bySource) {
      auto it = scans.cend();
      for (int slot = 0; slot < 3 && it != scans.cbegin(); ++slot) {
        const auto key = (--it).value();
        auto& entry = decoded[key];
        if (slot == 0) {
          if (entry.grid->expires <= now) continue;
          result.until = std::min(result.until, entry.grid->expires);
        }
        if (!entry.frame) entry.frame = entry.grid->render();
        layers[slot].append(entry.frame);
        displayed.insert(key);
      }
    }
    for (auto it = decoded.begin(); it != decoded.end(); ++it)
      if (!displayed.contains(it.key())) it->frame.reset();
    for (int slot = 0; slot < 3; ++slot) {
      if (layers[slot].isEmpty()) continue;
      if (layers[slot].size() == 1) {
        result.pictures[slot] = layers[slot].front();
        continue;
      }
      auto merged = std::make_shared<NexradFrame>();
      for (int level = 0; level < 6; ++level) {
        for (auto member :
             {&NexradFrame::Level::fill, &NexradFrame::Level::stipple,
              &NexradFrame::Level::faaStipple}) {
          auto cb = std::make_shared<renderer::CmdBuffer>();
          for (const auto& layer : layers[slot]) {
            merged->observed = std::max(merged->observed, layer->observed);
            if (auto part = layer->levels[level].*member) cb->call(part);
          }
          if (!cb->commands().empty()) merged->levels[level].*member = cb;
        }
      }
      result.pictures[slot] = merged;
    }
    return result;
  }
};

ItwsArchive::ItwsArchive(QString site)
    : state_(std::make_shared<State>()),
      archive_(std::move(site),
               {qEnvironmentVariable("STARS_ITWS_DIR",
                                     qEnvironmentVariable("STARS_TAIS_DIR")),
                qEnvironmentVariable("ITWS_BUCKET",
                                     qEnvironmentVariable("TAIS_BUCKET")),
                true}) {}

Weather::Sample ItwsArchive::sample(qint64 ms) {
  if (!archive_.configured()) return {{}, true};
  const auto sec = ms / 1000;
  archive_.prepare(sec, 300);
  if (pending_ && future_.isFinished()) {
    current_ = future_.result();
    pending_ = false;
  }
  const bool valid = current_.from <= ms && ms < current_.until;
  if (valid && current_.revision == archive_.revision())
    return {current_.pictures, true};
  if (!pending_) {
    const auto events = archive_.between(
        std::max<qint64>(0, sec - 300) * 1000000000, (sec + 61) * 1000000000);
    if (events) {
      future_ = QtConcurrent::run([state = state_, events = *events, ms,
                                   revision = archive_.revision()] {
        return state->picture(events, ms, revision);
      });
      pending_ = true;
    }
  }
  return {valid ? current_.pictures : WeatherPictures{},
          valid || !archive_.error().isEmpty()};
}
}  // namespace stars::ui
