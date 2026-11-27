#include "alertaudio.h"

#include <QDebug>
#include <QFile>

AlertAudio::~AlertAudio() {
  if (alarmStream) SDL_DestroyAudioStream(alarmStream);
  if (alarmInitialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool AlertAudio::open() {
  QFile resource(":/resources/audio/msaw.wav");
  if (!resource.open(QIODevice::ReadOnly)) return false;
  const auto bytes = resource.readAll();
  SDL_AudioSpec spec{};
  Uint8* pcm = nullptr;
  Uint32 length = 0;
  if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes.constData(), bytes.size()), true,
                      &spec, &pcm, &length))
    return false;
  alarmSample = QByteArray(reinterpret_cast<const char*>(pcm), length);
  SDL_free(pcm);
  alarmInitialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
  if (!alarmInitialized) return false;
  alarmStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                          &spec, nullptr, nullptr);
  return alarmStream && SDL_ResumeAudioStreamDevice(alarmStream);
}

void AlertAudio::update(bool wanted, int volume) {
  if (!wanted) {
    if (alarmPlaying) SDL_ClearAudioStream(alarmStream);
    alarmPlaying = false;
    return;
  }
  if (alarmFailed) return;
  if (!alarmStream && !open()) {
    qWarning() << "MSAW audio unavailable:" << SDL_GetError();
    alarmFailed = true;
    return;
  }
  SDL_SetAudioStreamGain(alarmStream, volume / 10.f);
  if (SDL_GetAudioStreamQueued(alarmStream) < alarmSample.size() &&
      !SDL_PutAudioStreamData(alarmStream, alarmSample.constData(),
                              alarmSample.size())) {
    qWarning() << "Cannot queue MSAW audio:" << SDL_GetError();
    alarmPlaying = false;
    alarmFailed = true;
    return;
  }
  alarmPlaying = true;
}
