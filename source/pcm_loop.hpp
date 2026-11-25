#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace unbound {

// Signed 16-bit little-endian stereo PCM. This runs on SDL's audio callback:
// bounded copies/math only, with no allocation, filesystem I/O, or locks.
inline void renderLoopingPcm16(unsigned char* output, std::size_t outputBytes,
                               const unsigned char* pcm, std::size_t pcmBytes,
                               std::size_t& offset, unsigned volume = 51) noexcept {
    constexpr std::size_t FrameBytes = 4;
    constexpr unsigned MaximumVolume = 128;
    if (!output || !outputBytes) return;
    std::memset(output, 0, outputBytes);
    if (!pcm || pcmBytes < FrameBytes || pcmBytes % FrameBytes) {
        offset = 0;
        return;
    }
    offset = offset < pcmBytes && offset % FrameBytes == 0 ? offset : 0;
    const auto playableBytes = outputBytes - outputBytes % FrameBytes;
    volume = std::min(volume, MaximumVolume);
    std::size_t written = 0;
    while (written < playableBytes) {
        const auto chunk = std::min(pcmBytes - offset, playableBytes - written);
        if (volume == MaximumVolume) {
            std::memcpy(output + written, pcm + offset, chunk);
        } else if (volume) {
            for (std::size_t index = 0; index < chunk; index += 2) {
                const unsigned packed = static_cast<unsigned>(pcm[offset + index]) |
                    (static_cast<unsigned>(pcm[offset + index + 1]) << 8);
                const std::int32_t sample = packed >= 32768 ?
                    static_cast<std::int32_t>(packed) - 65536 : static_cast<std::int32_t>(packed);
                const std::int32_t scaled = sample * static_cast<std::int32_t>(volume) /
                    static_cast<std::int32_t>(MaximumVolume);
                const auto encoded = static_cast<std::uint16_t>(scaled);
                output[written + index] = static_cast<unsigned char>(encoded & 255);
                output[written + index + 1] = static_cast<unsigned char>(encoded >> 8);
            }
        }
        written += chunk;
        offset += chunk;
        if (offset == pcmBytes) offset = 0;
    }
}

} // namespace unbound
