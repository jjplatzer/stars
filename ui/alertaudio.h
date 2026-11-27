#pragma once
#include <SDL3/SDL.h>

#include <QByteArray>

class AlertAudio {
 public:
  AlertAudio() = default;
  AlertAudio(const AlertAudio&) = delete;
  AlertAudio& operator=(const AlertAudio&) = delete;
  ~AlertAudio();
  void update(bool wanted, int volume);
  bool playing() const { return alarmPlaying; }

 private:
  bool open();
  SDL_AudioStream* alarmStream{};
  QByteArray alarmSample;
  bool alarmInitialized = false, alarmFailed = false, alarmPlaying = false;
};
