#pragma once

// Narrow deterministic audio fake. The production PCM renderer is tested
// separately; this exercises initialization/ownership/failure ordering.
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using Uint8 = std::uint8_t;
using Uint16 = std::uint16_t;
using Uint32 = std::uint32_t;
using Sint64 = std::int64_t;
using SDL_AudioDeviceID = Uint32;
constexpr Uint32 SDL_INIT_AUDIO = 0x10;
constexpr Uint16 AUDIO_S16LSB = 0x8010;

struct SDL_AudioSpec {
    int freq = 0;
    Uint16 format = 0;
    Uint8 channels = 0;
    Uint8 silence = 0;
    Uint16 samples = 0;
    Uint16 padding = 0;
    Uint32 size = 0;
    void (*callback)(void*, Uint8*, int) = nullptr;
    void* userdata = nullptr;
};
struct SDL_RWops {};

namespace fake_audio {
enum class Failure { None, Init, OpenFile, OversizedFile, Decode, WrongFormat, OpenDevice };
inline Failure failure = Failure::None;
inline unsigned initializations = 0, quits = 0, files = 0, buffers = 0, devices = 0;
inline unsigned starts = 0, closes = 0, callbacks = 0;
inline SDL_AudioSpec desired;

inline void reset(Failure next = Failure::None) {
    assert(files == 0 && buffers == 0 && devices == 0);
    failure = next;
    initializations = quits = starts = closes = callbacks = 0;
    desired = {};
}
inline void render() {
    assert(devices && buffers && files == 0);
    std::array<Uint8, 36> output{};
    desired.callback(desired.userdata, output.data(), static_cast<int>(output.size()));
    ++callbacks;
}
} // namespace fake_audio

inline const char* SDL_GetError() { return "injected audio test failure"; }
inline int SDL_InitSubSystem(Uint32 flags) {
    assert(flags == SDL_INIT_AUDIO);
    if (fake_audio::failure == fake_audio::Failure::Init) return -1;
    ++fake_audio::initializations;
    return 0;
}
inline void SDL_QuitSubSystem(Uint32 flags) {
    assert(flags == SDL_INIT_AUDIO);
    assert(fake_audio::files == 0 && fake_audio::buffers == 0 && fake_audio::devices == 0);
    ++fake_audio::quits;
}
inline SDL_RWops* SDL_RWFromFile(const char*, const char*) {
    if (fake_audio::failure == fake_audio::Failure::OpenFile) return nullptr;
    ++fake_audio::files;
    return new SDL_RWops;
}
inline Sint64 SDL_RWsize(SDL_RWops*) {
    return fake_audio::failure == fake_audio::Failure::OversizedFile ? 33 * 1024 * 1024 : 100;
}
inline int SDL_RWclose(SDL_RWops* file) {
    assert(file && fake_audio::files > 0);
    --fake_audio::files;
    delete file;
    return 0;
}
inline SDL_AudioSpec* SDL_LoadWAV_RW(SDL_RWops* file, int freeSource,
                                    SDL_AudioSpec* spec, Uint8** pcm, Uint32* bytes) {
    assert(freeSource == 1 && file);
    SDL_RWclose(file);
    if (fake_audio::failure == fake_audio::Failure::Decode) return nullptr;
    *pcm = static_cast<Uint8*>(std::malloc(8));
    assert(*pcm);
    std::memset(*pcm, 10, 8);
    ++fake_audio::buffers;
    *bytes = 8;
    spec->freq = 44100;
    spec->channels = 2;
    spec->format = fake_audio::failure == fake_audio::Failure::WrongFormat ? 0 : AUDIO_S16LSB;
    return spec;
}
inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int capture,
                                             const SDL_AudioSpec* desired,
                                             SDL_AudioSpec*, int allowedChanges) {
    assert(capture == 0 && allowedChanges == 0);
    assert(fake_audio::files == 0 && fake_audio::buffers == 1);
    assert(desired->freq == 44100 && desired->channels == 2 && desired->format == AUDIO_S16LSB);
    assert(desired->samples == 2048 && desired->callback && desired->userdata);
    if (fake_audio::failure == fake_audio::Failure::OpenDevice) return 0;
    fake_audio::desired = *desired;
    ++fake_audio::devices;
    return 1;
}
inline void SDL_PauseAudioDevice(SDL_AudioDeviceID device, int paused) {
    assert(device == 1 && paused == 0 && fake_audio::devices == 1);
    ++fake_audio::starts;
    fake_audio::render();
}
inline void SDL_CloseAudioDevice(SDL_AudioDeviceID device) {
    assert(device == 1 && fake_audio::devices == 1);
    // Model an in-flight callback that must finish before backing PCM is freed.
    fake_audio::render();
    --fake_audio::devices;
    ++fake_audio::closes;
}
inline void SDL_FreeWAV(Uint8* data) {
    assert(data && fake_audio::devices == 0 && fake_audio::buffers == 1);
    --fake_audio::buffers;
    std::free(data);
}
