#include "background_music.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace fake_audio;
    for (const auto failure : {Failure::Init, Failure::OpenFile, Failure::OversizedFile,
                              Failure::Decode, Failure::WrongFormat, Failure::OpenDevice}) {
        reset(failure);
        {
            unbound::BackgroundMusic music;
            assert(!music.start("theme.wav"));
            music.stop();
            music.stop();
        }
        assert(files == 0 && buffers == 0 && devices == 0);
        assert(initializations == quits);
    }
    reset();
    {
        unbound::BackgroundMusic music;
        assert(!music.start(nullptr));
        assert(!music.start(""));
        assert(initializations == 0);
        assert(music.start("theme.wav"));
        assert(devices == 1 && buffers == 1 && files == 0 && starts == 1);
        render();
        assert(music.start("theme.wav"));
        assert(closes == 1 && devices == 1 && buffers == 1 && files == 0 && starts == 2);
        music.stop();
        assert(closes == 2 && devices == 0 && buffers == 0 && files == 0);
        assert(initializations == quits);
        music.stop();
    }
    reset();
    {
        unbound::BackgroundMusic music;
        assert(music.start("theme.wav"));
    }
    assert(closes == 1 && buffers == 0 && devices == 0 && files == 0);
    assert(initializations == quits);
    std::puts("Background music nonfatal failures, closed files, restart and callback-before-free lifecycle passed");
}
