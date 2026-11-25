#include "pcm_loop.hpp"

#include <array>
#include <cassert>
#include <cstdio>

int main() {
    using unbound::renderLoopingPcm16;
    // Two stereo frames: (+32767, -32768), (+128, -128).
    const std::array<unsigned char, 8> pcm{255,127,0,128,128,0,128,255};
    std::size_t offset = 0;
    std::array<unsigned char, 4> frame{};
    renderLoopingPcm16(frame.data(), frame.size(), pcm.data(), pcm.size(), offset, 128);
    assert((frame == std::array<unsigned char, 4>{255,127,0,128}));
    assert(offset == 4);
    renderLoopingPcm16(frame.data(), frame.size(), pcm.data(), pcm.size(), offset, 128);
    assert((frame == std::array<unsigned char, 4>{128,0,128,255}));
    assert(offset == 0);

    // Output can wrap more than once in one callback.
    std::array<unsigned char, 28> repeated{};
    offset = 4;
    renderLoopingPcm16(repeated.data(), repeated.size(), pcm.data(), pcm.size(), offset, 128);
    for (std::size_t index = 0; index < repeated.size(); ++index)
        assert(repeated[index] == pcm[(4 + index) % pcm.size()]);
    assert(offset == 0);

    // Scale with signed math, independent of host endianness.
    offset = 0;
    renderLoopingPcm16(frame.data(), frame.size(), pcm.data(), pcm.size(), offset, 64);
    assert((frame == std::array<unsigned char, 4>{255,63,0,192}));
    offset = 0;
    renderLoopingPcm16(frame.data(), frame.size(), pcm.data(), pcm.size(), offset, 512);
    assert((frame == std::array<unsigned char, 4>{255,127,0,128}));
    offset = 0;
    renderLoopingPcm16(frame.data(), frame.size(), pcm.data(), pcm.size(), offset, 0);
    assert((frame == std::array<unsigned char, 4>{0,0,0,0}));
    assert(offset == 4);

    // Invalid/empty input remains silent, with no out-of-bounds read.
    frame.fill(123);
    offset = 100;
    renderLoopingPcm16(frame.data(), frame.size(), nullptr, 0, offset);
    assert((frame == std::array<unsigned char, 4>{0,0,0,0}) && offset == 0);
    frame.fill(123);
    renderLoopingPcm16(frame.data(), frame.size(), pcm.data(), 7, offset);
    assert((frame == std::array<unsigned char, 4>{0,0,0,0}) && offset == 0);

    // Odd callback tails are silent and cannot split a stereo frame.
    std::array<unsigned char, 7> odd{};
    offset = 3;
    renderLoopingPcm16(odd.data(), odd.size(), pcm.data(), pcm.size(), offset, 128);
    assert((odd == std::array<unsigned char, 7>{255,127,0,128,0,0,0}));
    assert(offset == 4);
    renderLoopingPcm16(nullptr, 100, pcm.data(), pcm.size(), offset);
    assert(offset == 4);
    renderLoopingPcm16(frame.data(), 0, pcm.data(), pcm.size(), offset);
    assert(offset == 4);
    std::puts("Background PCM loop wrapping, signed volume, bounds and silence tests passed");
}
