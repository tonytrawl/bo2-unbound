#include "install_stream.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t MiB = 1024 * 1024;

std::vector<char> payload(std::size_t size) {
    std::vector<char> data(size);
    for (std::size_t i = 0; i < size; ++i) data[i] = static_cast<char>((i * 131 + i / 257) & 255);
    return data;
}

struct Reader {
    const std::vector<char>& bytes;
    std::size_t offset = 0;
    bool failAtEnd = false;
    std::atomic<std::size_t> observed{0};
    std::int64_t operator()(char* destination, std::size_t maximum) {
        if (offset == bytes.size()) return failAtEnd ? -1 : 0;
        const auto count = std::min({maximum, bytes.size() - offset, std::size_t{200017}});
        std::memcpy(destination, bytes.data() + offset, count);
        offset += count;
        observed = offset;
        return static_cast<std::int64_t>(count);
    }
};

struct Writer {
    std::vector<char> bytes;
    std::thread::id thread;
    bool shortWrite = false;
    bool hold = false;
    bool entered = false;
    bool released = false;
    std::mutex mutex;
    std::condition_variable condition;

    std::size_t operator()(const char* data, std::size_t size) {
        thread = std::this_thread::get_id();
        if (hold && !entered) {
            std::unique_lock lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return released; });
        }
        const auto written = shortWrite ? size - 1 : size;
        bytes.insert(bytes.end(), data, data + written);
        return written;
    }
    void waitUntilEntered() {
        std::unique_lock lock(mutex);
        assert(condition.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        condition.notify_all();
    }
};

void roundTrips() {
    for (const auto count : {std::size_t{0}, std::size_t{1}, MiB - 1, MiB, 3 * MiB + 37}) {
        const auto data = payload(count);
        Reader read{data};
        Writer write;
        unbound::AlignedIoBuffer buffer;
        unbound::OperationMetrics metrics;
        std::size_t observed = 0;
        const auto producer = std::this_thread::get_id();
        const auto result = unbound::streamInstallEntry(count, buffer.data(), buffer.size(), read, write,
            [&](std::size_t bytes) { assert(std::this_thread::get_id() == producer); observed += bytes; }, metrics);
        assert(result == unbound::InstallStreamResult::Success);
        assert(write.bytes == data && observed == count);
        assert(metrics.bytesRead == count && metrics.bytesWritten == count);
        assert(metrics.pipelinedFiles == (count >= MiB ? 1U : 0U));
        assert(metrics.synchronousFiles == (count < MiB ? 1U : 0U));
        if (count) assert((write.thread != producer) == (count >= MiB));
        assert(fake_sdl::noHandles());
    }
}

void boundedOverlapAndMandatoryJoin() {
    const auto data = payload(4 * MiB + 71);
    Reader read{data};
    Writer write;
    write.hold = true;
    unbound::AlignedIoBuffer buffer;
    unbound::OperationMetrics metrics;
    std::atomic<bool> returned{false};
    auto result = unbound::InstallStreamResult::ReadFailed;
    std::thread producer([&] {
        result = unbound::streamInstallEntry(data.size(), buffer.data(), buffer.size(), read, write,
                                             [](std::size_t) {}, metrics);
        returned = true;
    });
    write.waitUntilEntered();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!fake_sdl::currentWaits) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    // Decode advanced while the destination was held. A bounded full queue now
    // applies backpressure; no success or caller-side close is possible yet.
    assert(read.observed >= MiB && read.observed < data.size());
    assert(!returned);
    write.release();
    producer.join();
    assert(result == unbound::InstallStreamResult::Success);
    assert(write.bytes == data && metrics.queueWaitTicks > 0);
    assert(fake_sdl::noHandles());
}

void failedReadAndSizeMismatchNeverSucceed() {
    const auto data = payload(3 * MiB + 37);
    for (const auto expected : {data.size(), data.size() - 1, data.size() + 1}) {
        Reader read{data};
        read.failAtEnd = expected == data.size();
        Writer write;
        unbound::AlignedIoBuffer buffer;
        unbound::OperationMetrics metrics;
        const auto result = unbound::streamInstallEntry(expected, buffer.data(), buffer.size(), read, write,
                                                         [](std::size_t) {}, metrics);
        assert(result == (read.failAtEnd ? unbound::InstallStreamResult::ReadFailed
                                       : unbound::InstallStreamResult::SizeMismatch));
        assert(write.bytes.size() <= expected);
        assert(std::equal(write.bytes.begin(), write.bytes.end(), data.begin()));
        assert(fake_sdl::noHandles());
    }
}

void partialWriterFailureJoinsBeforeReturn() {
    for (const auto size : {std::size_t{101}, 4 * MiB + 37}) {
        const auto data = payload(size);
        Reader read{data};
        Writer write;
        write.shortWrite = true;
        unbound::AlignedIoBuffer buffer;
        unbound::OperationMetrics metrics;
        assert(unbound::streamInstallEntry(size, buffer.data(), buffer.size(), read, write,
                                           [](std::size_t) {}, metrics) == unbound::InstallStreamResult::WriteFailed);
        assert(metrics.bytesWritten == write.bytes.size() && metrics.bytesWritten < size);
        assert(fake_sdl::noHandles());
    }
}

void startupFailureUsesDirectWrites() {
    const auto data = payload(2 * MiB + 19);
    for (const auto failure : {fake_sdl::Failure::Mutex, fake_sdl::Failure::Condition, fake_sdl::Failure::Thread}) {
        Reader read{data};
        Writer write;
        unbound::AlignedIoBuffer buffer;
        unbound::OperationMetrics metrics;
        fake_sdl::nextFailure = failure;
        assert(unbound::streamInstallEntry(data.size(), buffer.data(), buffer.size(), read, write,
                                           [](std::size_t) {}, metrics) == unbound::InstallStreamResult::Success);
        assert(write.bytes == data && metrics.synchronousFiles == 1 && metrics.pipelinedFiles == 0);
        assert(write.thread == std::this_thread::get_id());
        assert(fake_sdl::nextFailure == fake_sdl::Failure::None && fake_sdl::noHandles());
    }
}

void invalidReaderAndBufferAreRejected() {
    unbound::AlignedIoBuffer buffer;
    for (const auto returned : {std::int64_t{-1}, std::numeric_limits<std::int64_t>::max()}) {
        Writer write;
        unbound::OperationMetrics metrics;
        const auto result = unbound::streamInstallEntry(MiB, buffer.data(), buffer.size(),
            [returned](char*, std::size_t) { return returned; }, write, [](std::size_t) {}, metrics);
        assert(result != unbound::InstallStreamResult::Success && write.bytes.empty());
        assert(fake_sdl::noHandles());
    }
    Writer write;
    unbound::OperationMetrics metrics;
    assert(unbound::streamInstallEntry(MiB, nullptr, 0,
        [](char*, std::size_t) { return std::int64_t{0}; }, write, [](std::size_t) {}, metrics)
        == unbound::InstallStreamResult::ReadFailed);
    assert(fake_sdl::noHandles());
}
} // namespace

int main() {
    roundTrips();
    boundedOverlapAndMandatoryJoin();
    failedReadAndSizeMismatchNeverSucceed();
    partialWriterFailureJoinsBeforeReturn();
    startupFailureUsesDirectWrites();
    invalidReaderAndBufferAreRejected();
    std::puts("Install pipeline ordering, bounded overlap, exact-size/read/write failures, join and fallback passed.");
}
