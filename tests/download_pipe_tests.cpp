#include "download_pipe.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t BlockBytes = 64;

std::vector<char> payload(std::size_t count) {
    std::vector<char> result(count);
    for (std::size_t index = 0; index < count; ++index)
        result[index] = static_cast<char>((index * 131U + index / 257U) & 255U);
    return result;
}

template <typename Predicate> void waitFor(Predicate condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!condition()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

struct Sink {
    std::vector<char> bytes;
    unbound::Sha256 hash;
    std::vector<std::size_t> chunks;
    std::mutex gateMutex;
    std::condition_variable gateCondition;
    bool holdFirst = false;
    bool enteredFirst = false;
    bool released = false;
    bool failFirst = false;
    unsigned calls = 0;

    static bool consume(void* context, const char* bytes, std::size_t count) {
        auto& sink = *static_cast<Sink*>(context);
        ++sink.calls;
        if (sink.holdFirst && sink.calls == 1) {
            std::unique_lock<std::mutex> lock(sink.gateMutex);
            sink.enteredFirst = true;
            sink.gateCondition.notify_all();
            sink.gateCondition.wait(lock, [&] { return sink.released; });
        }
        if (sink.failFirst && sink.calls == 1) return false;
        sink.bytes.insert(sink.bytes.end(), bytes, bytes + count);
        sink.hash.update(bytes, count);
        sink.chunks.push_back(count);
        return true;
    }

    void waitUntilEntered() {
        std::unique_lock<std::mutex> lock(gateMutex);
        assert(gateCondition.wait_for(lock, std::chrono::seconds(5),
                                      [&] { return enteredFirst; }));
    }
    void release() {
        std::lock_guard<std::mutex> lock(gateMutex);
        released = true;
        gateCondition.notify_all();
    }
};

void verify(Sink& sink, const std::vector<char>& expected) {
    assert(sink.bytes == expected);
    unbound::Sha256 expectedHash;
    expectedHash.update(expected.data(), expected.size());
    assert(sink.hash.finish() == expectedHash.finish());
}

void orderedPartialChunksAndWraparound() {
    Sink sink;
    const auto input = payload(2 * 1024 * 1024 + 37);
    {
        unbound::DownloadPipe pipe(BlockBytes);
        assert(pipe.start(Sink::consume, &sink));
        assert(pipe.blockSize() == BlockBytes);
        assert(pipe.capacity() == 4 * BlockBytes);
        // Callback boundaries deliberately differ from block boundaries, and
        // several callbacks are larger than the entire queue.
        constexpr std::size_t Chunks[] = {1, 7, 63, 64, 65, 3, 1025, 17, 511};
        std::size_t offset = 0;
        std::size_t nextChunk = 0;
        while (offset < input.size()) {
            const auto count = std::min(Chunks[nextChunk++ % 9], input.size() - offset);
            assert(pipe.push(input.data() + offset, count));
            offset += count;
        }
        assert(pipe.finish(true));
        assert(!pipe.failed());
        verify(sink, input);
        assert(sink.chunks.size() == (input.size() + BlockBytes - 1) / BlockBytes);
        for (std::size_t index = 0; index + 1 < sink.chunks.size(); ++index)
            assert(sink.chunks[index] == BlockBytes);
        assert(sink.chunks.back() == input.size() % BlockBytes);
    }
    assert(fake_sdl::noHandles());
}

void emptyAndFinalPartialDrain() {
    for (const std::size_t count : {0U, 1U, 63U, 64U, 65U, 256U, 257U}) {
        Sink sink;
        const auto input = payload(count);
        {
            unbound::DownloadPipe pipe(BlockBytes);
            assert(pipe.start(Sink::consume, &sink));
            if (count) assert(pipe.push(input.data(), count));
            assert(pipe.finish(true));
            assert(!pipe.failed());
            verify(sink, input);
            assert(sink.calls == (count + BlockBytes - 1) / BlockBytes);
        }
        assert(fake_sdl::noHandles());
    }
}

void boundedBackpressureAndDrain() {
    Sink sink;
    sink.holdFirst = true;
    const auto input = payload(8 * BlockBytes + 13);
    {
        unbound::DownloadPipe pipe(BlockBytes);
        assert(pipe.start(Sink::consume, &sink));
        assert(pipe.push(input.data(), BlockBytes));
        sink.waitUntilEntered();
        assert(pipe.push(input.data() + BlockBytes, 3 * BlockBytes));
        std::atomic<bool> producerReturned{false};
        bool producerResult = false;
        std::thread producer([&] {
            producerResult = pipe.push(input.data() + 4 * BlockBytes,
                                       input.size() - 4 * BlockBytes);
            producerReturned = true;
        });
        waitFor([&] { return fake_sdl::currentWaits > 0; });
        assert(!producerReturned);
        // The in-flight slot remains reserved. A fifth block cannot overwrite
        // its bytes before the sink's write and hash have finished.
        sink.release();
        producer.join();
        assert(producerResult);
        assert(pipe.finish(true));
        assert(!pipe.failed());
        assert(pipe.waitTicks() > 0);
        verify(sink, input);
    }
    assert(fake_sdl::noHandles());
}

void failedProducerDiscardsPendingData() {
    Sink sink;
    sink.holdFirst = true;
    const auto input = payload(3 * BlockBytes + 7);
    {
        unbound::DownloadPipe pipe(BlockBytes);
        assert(pipe.start(Sink::consume, &sink));
        assert(pipe.push(input.data(), BlockBytes));
        sink.waitUntilEntered();
        assert(pipe.push(input.data() + BlockBytes, input.size() - BlockBytes));
        bool finishResult = true;
        std::thread finisher([&] { finishResult = pipe.finish(false); });
        waitFor([&] { return fake_sdl::joiningThreads > 0; });
        sink.release();
        finisher.join();
        assert(!finishResult);
        assert(sink.calls == 1);
        verify(sink, std::vector<char>(input.begin(), input.begin() + BlockBytes));
    }
    assert(fake_sdl::noHandles());
}

void failedSinkWakesBlockedProducer() {
    Sink sink;
    sink.holdFirst = true;
    sink.failFirst = true;
    const auto input = payload(6 * BlockBytes);
    {
        unbound::DownloadPipe pipe(BlockBytes);
        assert(pipe.start(Sink::consume, &sink));
        assert(pipe.push(input.data(), BlockBytes));
        sink.waitUntilEntered();
        assert(pipe.push(input.data() + BlockBytes, 3 * BlockBytes));
        std::atomic<bool> producerReturned{false};
        bool producerResult = true;
        std::thread producer([&] {
            producerResult = pipe.push(input.data() + 4 * BlockBytes, 2 * BlockBytes);
            producerReturned = true;
        });
        waitFor([&] { return fake_sdl::currentWaits > 0; });
        assert(!producerReturned);
        sink.release();
        waitFor([&] { return producerReturned.load(); });
        producer.join();
        assert(!producerResult);
        assert(!pipe.finish(true));
        assert(pipe.failed());
        assert(sink.calls == 1);
        assert(sink.bytes.empty());
    }
    assert(fake_sdl::noHandles());
}

void failedStartupSupportsSynchronousFallback() {
    for (const auto failure : {fake_sdl::Failure::Mutex, fake_sdl::Failure::Condition,
                               fake_sdl::Failure::Thread}) {
        Sink sink;
        const auto input = payload(4 * BlockBytes + 29);
        {
            unbound::DownloadPipe pipe(BlockBytes);
            fake_sdl::nextFailure = failure;
            assert(!pipe.start(Sink::consume, &sink));
            assert(sink.calls == 0);
            // The integration can use its existing direct write/hash callback;
            // an unsuccessful start must not have consumed any bytes already.
            assert(Sink::consume(&sink, input.data(), input.size()));
            verify(sink, input);
        }
        assert(fake_sdl::nextFailure == fake_sdl::Failure::None);
        assert(fake_sdl::noHandles());
    }
    for (const auto requested : {std::size_t{0}, std::numeric_limits<std::size_t>::max()}) {
        Sink sink;
        {
            unbound::DownloadPipe pipe(requested);
            assert(!pipe.start(Sink::consume, &sink));
            assert(sink.calls == 0);
        }
        assert(fake_sdl::noHandles());
    }
}

void destructionCancelsAndJoins() {
    Sink sink;
    sink.holdFirst = true;
    const auto input = payload(3 * BlockBytes + 11);
    auto* pipe = new unbound::DownloadPipe(BlockBytes);
    assert(pipe->start(Sink::consume, &sink));
    assert(pipe->push(input.data(), BlockBytes));
    sink.waitUntilEntered();
    assert(pipe->push(input.data() + BlockBytes, input.size() - BlockBytes));
    std::thread destroyer([&] { delete pipe; });
    waitFor([&] { return fake_sdl::joiningThreads > 0; });
    sink.release();
    destroyer.join();
    assert(sink.calls == 1);
    verify(sink, std::vector<char>(input.begin(), input.begin() + BlockBytes));
    assert(fake_sdl::noHandles());
}

void exactByteLimitAndOverflow() {
    using unbound::checkedDownloadChunk;
    std::size_t bytes = 99;
    assert(checkedDownloadChunk(1, 257, 0, 257, bytes) && bytes == 257);
    assert(!checkedDownloadChunk(1, 1, 257, 257, bytes) && bytes == 0);
    assert(checkedDownloadChunk(1, 37, 220, 257, bytes) && bytes == 37);
    assert(!checkedDownloadChunk(1, 38, 220, 257, bytes) && bytes == 0);
    assert(!checkedDownloadChunk(1, 0, 258, 257, bytes) && bytes == 0);
    assert(checkedDownloadChunk(1, 0, 257, 257, bytes) && bytes == 0);
    assert(checkedDownloadChunk(0, std::numeric_limits<std::size_t>::max(),
                                0, 0, bytes) && bytes == 0);
    assert(!checkedDownloadChunk(1, 1, 0, 0, bytes) && bytes == 0);

    const auto sizeMaximum = std::numeric_limits<std::size_t>::max();
    const auto totalMaximum = std::numeric_limits<unsigned long long>::max();
    assert(!checkedDownloadChunk(2, sizeMaximum, 0, totalMaximum, bytes) && bytes == 0);
    assert(!checkedDownloadChunk(sizeMaximum, 2, 0, totalMaximum, bytes) && bytes == 0);
    assert(checkedDownloadChunk(sizeMaximum, 1, 0, sizeMaximum, bytes) && bytes == sizeMaximum);
    assert(checkedDownloadChunk(1, 63, totalMaximum - 63, totalMaximum, bytes) && bytes == 63);
    assert(!checkedDownloadChunk(1, 64, totalMaximum - 63, totalMaximum, bytes) && bytes == 0);
    assert(!checkedDownloadChunk(1, 0, totalMaximum, totalMaximum - 1, bytes) && bytes == 0);

    // The cap applies to queued network bytes even when the writer is blocked.
    Sink sink;
    sink.holdFirst = true;
    const auto input = payload(4 * BlockBytes);
    {
        unbound::DownloadPipe pipe(BlockBytes);
        assert(pipe.start(Sink::consume, &sink));
        unsigned long long accepted = 0;
        assert(checkedDownloadChunk(1, BlockBytes, accepted, input.size(), bytes));
        assert(pipe.push(input.data(), bytes));
        accepted += bytes;
        sink.waitUntilEntered();
        assert(checkedDownloadChunk(1, 3 * BlockBytes, accepted, input.size(), bytes));
        assert(pipe.push(input.data() + BlockBytes, bytes));
        accepted += bytes;
        assert(accepted == input.size());
        assert(!checkedDownloadChunk(1, 1, accepted, input.size(), bytes));
        sink.release();
        assert(pipe.finish(true));
        verify(sink, input);
    }
    assert(fake_sdl::noHandles());
}
} // namespace

int main() {
    orderedPartialChunksAndWraparound();
    emptyAndFinalPartialDrain();
    boundedBackpressureAndDrain();
    failedProducerDiscardsPendingData();
    failedSinkWakesBlockedProducer();
    failedStartupSupportsSynchronousFallback();
    destructionCancelsAndJoins();
    exactByteLimitAndOverflow();
    assert(fake_sdl::currentWaits == 0 && fake_sdl::joiningThreads == 0);
    std::puts("Download pipeline byte/hash, backpressure, drain, cancellation, and fallback tests passed.");
}
