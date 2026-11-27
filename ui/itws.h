#pragma once
#include "../net/tais.h"
#include "weather.h"

namespace stars::ui {
QString itwsSite(const QString& facility, const QString& positionFacility = {});
class ItwsArchive {
 public:
  explicit ItwsArchive(QString site);
  Weather::Sample sample(qint64 playbackMs);

 private:
  struct State;
  struct Result {
    WeatherPictures pictures{};
    qint64 from = -1, until = -1;
    quint64 revision{};
  };
  std::shared_ptr<State> state_;
  net::TaisCache archive_;
  QFuture<Result> future_;
  bool pending_ = false;
  Result current_;
};
}  // namespace stars::ui
