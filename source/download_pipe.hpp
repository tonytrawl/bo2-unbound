#pragma once

#include "io_buffer.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace unbound {

// Check the network callback's multiplication and byte budget before copying
// anything into the queue. Accepted bytes include data still waiting for SD.
inline bool checkedDownloadChunk(std::size_t size, std::size_t count,
                                 unsigned long long accepted,
                                 unsigned long long maximum,
                                 std::size_t& bytes) {
    bytes = 0;
    if (size && count > std::numeric_limits<std::size_t>::max() / size)
        return false;
    const auto chunk = size * count;
    if (accepted > maximum || chunk > maximum - accepted) return false;
    bytes = chunk;
    return true;
}

// One producer, one consumer. A slot remains reserved while the sink uses it.
// Memory is bounded; a slow SD card applies backpressure instead of growing RAM.
class DownloadPipe {
public:
    using Sink = bool (*)(void*, const char*, std::size_t);
    explicit DownloadPipe(std::size_t requestedBlockBytes = 256 * 1024)
        : storage_(allocationSize(requestedBlockBytes)) {
        if (storage_) blockSize_ = storage_.size() / Slots;
    }

    ~DownloadPipe() {
        if (thread_) (void)finish(false);
        releaseSynchronization();
    }
    DownloadPipe(const DownloadPipe&) = delete;
    DownloadPipe& operator=(const DownloadPipe&) = delete;

    bool start(Sink sink, void* context) {
        if (!blockSize_ || !sink || started_) return false;
        mutex_ = SDL_CreateMutex();
        readable_ = SDL_CreateCond();
        writable_ = SDL_CreateCond();
        if (!mutex_ || !readable_ || !writable_) {
            releaseSynchronization();
            return false;
        }
        sink_ = sink;
        context_ = context;
        thread_ = SDL_CreateThreadWithStackSize(consume, "HQ SD writer", 256 * 1024, this);
        if (!thread_) {
            releaseSynchronization();
            return false;
        }
        started_ = true;
        return true;
    }

    bool push(const char* data, std::size_t bytes) {
        if (!thread_ || (!data && bytes)) return false;
        SDL_LockMutex(mutex_);
        while (bytes && !failed_ && !closing_) {
            while (ready_ == Slots && !failed_ && !closing_) {
                const auto began = SDL_GetPerformanceCounter();
                const int result = SDL_CondWait(writable_, mutex_);
                waitTicks_ += SDL_GetPerformanceCounter() - began;
                if (result != 0) { failLocked(); break; }
            }
            if (failed_ || closing_) break;
            const auto take = std::min(bytes, blockSize_ - partial_);
            std::memcpy(storage_.data() + writeSlot_ * blockSize_ + partial_, data, take);
            partial_ += take;
            data += take;
            bytes -= take;
            if (partial_ == blockSize_) sealLocked();
        }
        const bool ok = !failed_ && !closing_ && bytes == 0;
        SDL_UnlockMutex(mutex_);
        return ok;
    }

    // Must be called by the producer only after its final push. Always joins
    // the writer, including cancellation/failure, before FILE/hash reuse.
    bool finish(bool producerSucceeded) {
        if (!started_) return false;
        if (thread_) {
            SDL_LockMutex(mutex_);
            if (!producerSucceeded) {
                failed_ = true;
            } else if (!failed_ && partial_) {
                sealLocked();
            }
            closing_ = true;
            SDL_CondBroadcast(readable_);
            SDL_CondBroadcast(writable_);
            SDL_UnlockMutex(mutex_);
            SDL_WaitThread(thread_, nullptr);
            thread_ = nullptr;
        }
        return !failed_;
    }

    // Result/timing access is only valid after finish() has joined the writer.
    bool failed() const { return failed_; }
    std::uint64_t waitTicks() const { return waitTicks_; }
    std::size_t blockSize() const { return blockSize_; }
    std::size_t capacity() const { return blockSize_ * Slots; }

private:
    static constexpr std::size_t Slots = 4;
    static std::size_t allocationSize(std::size_t bytes) {
        if (!bytes || bytes > (std::numeric_limits<std::size_t>::max() - 63) / Slots)
            return 0;
        return bytes * Slots;
    }
    void sealLocked() {
        lengths_[writeSlot_] = partial_;
        partial_ = 0;
        writeSlot_ = (writeSlot_ + 1) % Slots;
        ++ready_;
        SDL_CondSignal(readable_);
    }
    void failLocked() {
        failed_ = true;
        closing_ = true;
        SDL_CondBroadcast(readable_);
        SDL_CondBroadcast(writable_);
    }
    static int consume(void* opaque) {
        auto& pipe = *static_cast<DownloadPipe*>(opaque);
        SDL_LockMutex(pipe.mutex_);
        for (;;) {
            while (!pipe.ready_ && !pipe.closing_ && !pipe.failed_) {
                if (SDL_CondWait(pipe.readable_, pipe.mutex_) != 0) {
                    pipe.failLocked();
                    break;
                }
            }
            if (pipe.failed_ || (!pipe.ready_ && pipe.closing_)) break;
            const auto slot = pipe.readSlot_;
            const auto bytes = pipe.lengths_[slot];
            SDL_UnlockMutex(pipe.mutex_);
            const bool ok = pipe.sink_(pipe.context_,
                                       pipe.storage_.data() + slot * pipe.blockSize_, bytes);
            SDL_LockMutex(pipe.mutex_);
            if (!ok) { pipe.failLocked(); break; }
            --pipe.ready_;
            pipe.readSlot_ = (pipe.readSlot_ + 1) % Slots;
            SDL_CondSignal(pipe.writable_);
        }
        SDL_UnlockMutex(pipe.mutex_);
        return 0;
    }
    void releaseSynchronization() {
        if (readable_) SDL_DestroyCond(readable_);
        if (writable_) SDL_DestroyCond(writable_);
        if (mutex_) SDL_DestroyMutex(mutex_);
        readable_ = nullptr;
        writable_ = nullptr;
        mutex_ = nullptr;
    }

    AlignedIoBuffer storage_;
    std::size_t blockSize_ = 0;
    SDL_mutex* mutex_ = nullptr;
    SDL_cond* readable_ = nullptr;
    SDL_cond* writable_ = nullptr;
    SDL_Thread* thread_ = nullptr;
    Sink sink_ = nullptr;
    void* context_ = nullptr;
    std::size_t lengths_[Slots]{};
    std::size_t writeSlot_ = 0, readSlot_ = 0, ready_ = 0, partial_ = 0;
    bool started_ = false, closing_ = false, failed_ = false;
    std::uint64_t waitTicks_ = 0;
};

} // namespace unbound
