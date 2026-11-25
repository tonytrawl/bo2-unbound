#include "background_music.hpp"
#include "pcm_loop.hpp"

#include <coreinit/debug.h>

namespace unbound {

bool BackgroundMusic::start(const char* path) {
    stop();
    constexpr Sint64 MaximumAssetBytes = 32 * 1024 * 1024;
    constexpr Uint32 MaximumPcmBytes = 32 * 1024 * 1024;
    const auto fail = [this](const char* reason) {
        OSReport("Unbound HQ background audio unavailable: %s (%s)\n", reason, SDL_GetError());
        stop();
        return false;
    };
    if (!path || !*path) return false;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return fail("could not initialize audio");
    audioInitialized_ = true;
    SDL_RWops* file = SDL_RWFromFile(path, "rb");
    if (!file) return fail("could not open the theme");
    const Sint64 bytes = SDL_RWsize(file);
    if (bytes <= 0 || bytes > MaximumAssetBytes) {
        SDL_RWclose(file);
        return fail("theme file exceeds its memory budget");
    }
    SDL_AudioSpec wav{};
    Uint8* loadedPcm = nullptr;
    Uint32 loadedBytes = 0;
    // freesrc=1 closes the file on success AND failure; no persistent WUHB I/O.
    // Transfer buffer ownership only on success. SDL owns all decode-failure
    // cleanup, including any buffer it allocated before detecting a bad WAV.
    if (!SDL_LoadWAV_RW(file, 1, &wav, &loadedPcm, &loadedBytes))
        return fail("could not decode the theme");
    pcm_ = loadedPcm;
    pcmBytes_ = loadedBytes;
    if (wav.freq != 44100 || wav.channels != 2 || wav.format != AUDIO_S16LSB ||
        !pcmBytes_ || pcmBytes_ > MaximumPcmBytes || pcmBytes_ % 4) {
        return fail("theme is not bounded 44.1 kHz stereo signed 16-bit PCM");
    }
    SDL_AudioSpec desired{};
    desired.freq = wav.freq;
    desired.format = AUDIO_S16LSB;
    desired.channels = 2;
    desired.samples = 2048;
    desired.callback = &BackgroundMusic::callback;
    desired.userdata = this;
    // No allowed changes: SDL may convert to the hardware format internally,
    // while our callback always receives this exact, tested little-endian PCM.
    device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (!device_) return fail("could not open playback");
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void BackgroundMusic::callback(void* context, Uint8* output, int bytes) {
    if (bytes <= 0 || !output) return;
    auto& music = *static_cast<BackgroundMusic*>(context);
    renderLoopingPcm16(output, static_cast<std::size_t>(bytes), music.pcm_,
                       music.pcmBytes_, music.offset_, 51); // Approximately 40%.
}

void BackgroundMusic::stop() noexcept {
    // SDL_CloseAudioDevice waits for the callback to finish. Only then may its
    // backing PCM be freed; start/stop never race callback-visible members.
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    if (pcm_) {
        SDL_FreeWAV(pcm_);
        pcm_ = nullptr;
    }
    pcmBytes_ = 0;
    offset_ = 0;
    if (audioInitialized_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audioInitialized_ = false;
    }
}

} // namespace unbound
