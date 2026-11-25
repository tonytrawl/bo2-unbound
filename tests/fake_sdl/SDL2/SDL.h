#pragma once

// A deliberately small host SDL threading implementation. Tests exercise the
// actual pipeline header with real concurrent threads and injected SDL startup
// failures; no Wii U libraries or display/network initialization are needed.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <thread>

using Uint32 = std::uint32_t;
using Uint64 = std::uint64_t;
enum SDL_bool { SDL_FALSE, SDL_TRUE };
constexpr int SDL_MUTEX_TIMEDOUT = 1;
struct SDL_atomic_t { std::atomic<int> value{0}; };
inline int SDL_AtomicGet(SDL_atomic_t* value) { return value->value.load(); }
inline int SDL_AtomicSet(SDL_atomic_t* value, int desired) {
    return value->value.exchange(desired);
}
inline int SDL_AtomicAdd(SDL_atomic_t* value, int delta) {
    return value->value.fetch_add(delta);
}
inline SDL_bool SDL_AtomicCAS(SDL_atomic_t* value, int oldValue, int desired) {
    return value->value.compare_exchange_strong(oldValue, desired) ? SDL_TRUE : SDL_FALSE;
}

namespace fake_sdl {
enum class Failure { None, Mutex, Condition, Thread };
inline std::atomic<Failure> nextFailure{Failure::None};
inline std::atomic<unsigned> mutexes{0};
inline std::atomic<unsigned> conditions{0};
inline std::atomic<unsigned> threads{0};
inline std::atomic<unsigned> waits{0};
inline std::atomic<unsigned> currentWaits{0};
inline std::atomic<unsigned> joiningThreads{0};
inline std::atomic<unsigned> tryLockFailures{0};
inline std::atomic<std::size_t> lastRequestedStackBytes{0};

inline bool shouldFail(Failure operation) {
    Failure expected = operation;
    return nextFailure.compare_exchange_strong(expected, Failure::None);
}
inline bool noHandles() {
    return mutexes == 0 && conditions == 0 && threads == 0;
}
} // namespace fake_sdl

struct SDL_mutex { std::mutex mutex; };
struct SDL_cond { std::condition_variable condition; };
struct SDL_Thread {
    std::thread thread;
    int status = 0;
};

inline SDL_mutex* SDL_CreateMutex() {
    if (fake_sdl::shouldFail(fake_sdl::Failure::Mutex)) return nullptr;
    auto* result = new (std::nothrow) SDL_mutex;
    if (result) ++fake_sdl::mutexes;
    return result;
}
inline void SDL_DestroyMutex(SDL_mutex* value) {
    if (value) { delete value; --fake_sdl::mutexes; }
}
inline int SDL_LockMutex(SDL_mutex* value) { value->mutex.lock(); return 0; }
inline int SDL_TryLockMutex(SDL_mutex* value) {
    unsigned remaining = fake_sdl::tryLockFailures;
    while (remaining && !fake_sdl::tryLockFailures.compare_exchange_weak(remaining, remaining - 1)) {}
    if (remaining) return SDL_MUTEX_TIMEDOUT;
    return value->mutex.try_lock() ? 0 : SDL_MUTEX_TIMEDOUT;
}
inline int SDL_UnlockMutex(SDL_mutex* value) { value->mutex.unlock(); return 0; }

inline SDL_cond* SDL_CreateCond() {
    if (fake_sdl::shouldFail(fake_sdl::Failure::Condition)) return nullptr;
    auto* result = new (std::nothrow) SDL_cond;
    if (result) ++fake_sdl::conditions;
    return result;
}
inline void SDL_DestroyCond(SDL_cond* value) {
    if (value) { delete value; --fake_sdl::conditions; }
}
inline int SDL_CondWait(SDL_cond* condition, SDL_mutex* mutex) {
    std::unique_lock<std::mutex> lock(mutex->mutex, std::adopt_lock);
    ++fake_sdl::waits;
    ++fake_sdl::currentWaits;
    condition->condition.wait(lock);
    --fake_sdl::currentWaits;
    lock.release();
    return 0;
}
inline int SDL_CondSignal(SDL_cond* value) { value->condition.notify_one(); return 0; }
inline int SDL_CondBroadcast(SDL_cond* value) { value->condition.notify_all(); return 0; }

inline SDL_Thread* SDL_CreateThread(int (*entry)(void*), const char*, void* context) {
    if (fake_sdl::shouldFail(fake_sdl::Failure::Thread)) return nullptr;
    auto* result = new (std::nothrow) SDL_Thread;
    if (!result) return nullptr;
    ++fake_sdl::threads;
    result->thread = std::thread([result, entry, context] { result->status = entry(context); });
    return result;
}
inline SDL_Thread* SDL_CreateThreadWithStackSize(
    int (*entry)(void*), const char* name, std::size_t stackBytes, void* context) {
    fake_sdl::lastRequestedStackBytes = stackBytes;
    return SDL_CreateThread(entry, name, context);
}
inline void SDL_WaitThread(SDL_Thread* value, int* status) {
    if (!value) return;
    ++fake_sdl::joiningThreads;
    value->thread.join();
    --fake_sdl::joiningThreads;
    if (status) *status = value->status;
    delete value;
    --fake_sdl::threads;
}
inline Uint64 SDL_GetTicks64() {
    return static_cast<Uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline Uint32 SDL_GetTicks() { return static_cast<Uint32>(SDL_GetTicks64()); }
inline Uint64 SDL_GetPerformanceCounter() {
    return static_cast<Uint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline Uint64 SDL_GetPerformanceFrequency() { return 1000000000ULL; }
inline void SDL_Delay(Uint32 milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
