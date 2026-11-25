#pragma once

#include <SDL2/SDL.h>

#include <functional>
#include <string>
#include <type_traits>

namespace unbound {

using BackgroundProgressCallback =
    std::function<void(const std::string&, const std::string&, int)>;
using BackgroundTickCallback = std::function<void()>;

namespace background_detail {

struct ProgressSnapshot {
    std::string title;
    std::string detail;
    int percent = -1;
};

template <typename Work>
struct OperationState {
    Work& work;
    SDL_mutex* mutex = nullptr;
    SDL_atomic_t finished{};
    ProgressSnapshot latest{};
    // Only the worker touches finalProgress until SDL_WaitThread completes.
    // Keeping it separately guarantees delivery even if the last try-lock fails.
    ProgressSnapshot finalProgress{};
    bool hasFinalProgress = false;
    bool dirty = false;
    bool result = false;

    void publish(const std::string& title, const std::string& detail, int percent) {
        finalProgress = {title, detail, percent};
        hasFinalProgress = true;
        // Rendering must never hold up filesystem/decompression work.
        bool locked=SDL_TryLockMutex(mutex)==0;
        // Preparation reports identify the call that may wait on storage.
        // Do not drop the last such report immediately before that call. The
        // caller holds this lock only for a snapshot copy, never for rendering
        // or I/O; normal percentage/byte updates remain nonblocking/coalesced.
        if(!locked&&percent<0)locked=SDL_LockMutex(mutex)==0;
        if (locked) {
            latest = finalProgress;
            dirty = true;
            SDL_UnlockMutex(mutex);
        }
    }

    static int run(void* opaque) {
        auto& state = *static_cast<OperationState*>(opaque);
        const BackgroundProgressCallback relay = [&state](const std::string& title,
                                                          const std::string& detail,
                                                          int percent) {
            state.publish(title, detail, percent);
        };
        state.result = state.work(relay);
        SDL_AtomicSet(&state.finished, 1);
        return 0;
    }
};

} // namespace background_detail

// Work receives a progress callback and returns bool. Its referenced inputs and
// result/error state remain alive until the mandatory join below. The caller's
// progress callback is invoked exclusively on the calling (UI) thread.
// An optional light tick lets that thread poll input between coalesced progress
// frames. It never runs on the worker and never requires a display redraw.
template <typename Work>
bool runBackgroundOperation(const char* name,
                            const BackgroundProgressCallback& progress,
                            Work&& work, bool* usedWorker = nullptr,
                            const BackgroundTickCallback& tick = {}) {
    if (usedWorker) *usedWorker = false;
    if (!progress && !tick) return work(progress);
    if (tick) tick();

    background_detail::OperationState<std::remove_reference_t<Work>> state{work};
    state.mutex = SDL_CreateMutex();
    SDL_Thread* worker = nullptr;
    if (state.mutex) {
        SDL_AtomicSet(&state.finished, 0);
        // Wii U SDL's default 32 KiB stack is too small for ZIP/install work.
        worker = SDL_CreateThreadWithStackSize(decltype(state)::run, name,
                                               256 * 1024, &state);
    }
    if (!worker) {
        if (state.mutex) SDL_DestroyMutex(state.mutex);
        if (!tick) return work(progress);
        const BackgroundProgressCallback relay = [&](const std::string& title,
                                                      const std::string& detail,
                                                      int percent) {
            tick();
            if (progress) progress(title, detail, percent);
        };
        const bool result = work(relay);
        tick();
        return result;
    }
    if (usedWorker) *usedWorker = true;

    while (!SDL_AtomicGet(&state.finished)) {
        if (tick) tick();
        background_detail::ProgressSnapshot snapshot;
        SDL_LockMutex(state.mutex);
        const bool dirty = state.dirty;
        if (dirty) {
            snapshot = state.latest;
            state.dirty = false;
        }
        SDL_UnlockMutex(state.mutex);
        if (dirty && progress) progress(snapshot.title, snapshot.detail, snapshot.percent);
        SDL_Delay(25);
    }
    SDL_WaitThread(worker, nullptr);
    if (tick) tick();
    if (state.hasFinalProgress && progress)
        progress(state.finalProgress.title, state.finalProgress.detail,
                 state.finalProgress.percent);
    SDL_DestroyMutex(state.mutex);
    return state.result;
}

} // namespace unbound
