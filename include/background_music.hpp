#pragma once

#include <SDL2/SDL.h>
#include <cstddef>

namespace unbound {

class BackgroundMusic {
public:
    BackgroundMusic() = default;
    ~BackgroundMusic() { stop(); }
    BackgroundMusic(const BackgroundMusic&) = delete;
    BackgroundMusic& operator=(const BackgroundMusic&) = delete;

    // Non-fatal: unavailable audio or an invalid asset leaves HQ silent.
    // The complete bounded PCM asset is read once and its file is closed before
    // returning, so playback never holds the WUHB mount open during self-update.
    bool start(const char* path);
    void stop() noexcept;

private:
    static void callback(void* context, Uint8* output, int bytes);
    SDL_AudioDeviceID device_ = 0;
    Uint8* pcm_ = nullptr;
    Uint32 pcmBytes_ = 0;
    std::size_t offset_ = 0;
    bool audioInitialized_ = false;
};

} // namespace unbound
