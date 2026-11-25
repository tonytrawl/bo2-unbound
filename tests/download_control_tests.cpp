#include "download_control.hpp"
#include "download_pipe.hpp"
#include "download_size.hpp"
#include "sha256.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>

namespace {

struct SinkState {
    std::string bytes;
    unbound::Sha256 hash;
    std::atomic<bool> firstWrite{false};
};

struct InputPollState {
    std::thread::id owner;
    unbound::DownloadControl* control;
    unsigned calls = 0;
};

void requestFromInput(void* opaque) {
    auto& state = *static_cast<InputPollState*>(opaque);
    assert(std::this_thread::get_id() == state.owner);
    ++state.calls;
    state.control->requestCancel();
}

bool slowSink(void* opaque, const char* data, std::size_t bytes) {
    auto& sink = *static_cast<SinkState*>(opaque);
    sink.firstWrite.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    sink.bytes.append(data, bytes);
    sink.hash.update(data, bytes);
    return true;
}

std::string digest(const std::string& bytes) {
    unbound::Sha256 hash;
    hash.update(bytes.data(), bytes.size());
    return hash.finish();
}

} // namespace

int main() {
    using namespace unbound;
    // A trusted manifest size bypasses the network probe completely. Invalid
    // declarations fail closed rather than allowing an unbounded fallback.
    unsigned probeCalls = 0;
    const auto probe = [&](unsigned long long& bytes) { ++probeCalls; bytes = 1234; return true; };
    const auto declared = resolveDownloadSize(1234, 2048, true, probe);
    assert(declared.valid && declared.known && !declared.probed && declared.bytes == 1234);
    assert(probeCalls == 0);
    assert(!resolveDownloadSize(2049, 2048, true, probe).valid && probeCalls == 0);
    assert(!resolveDownloadSize(1234, 2048, false, probe).valid && probeCalls == 0);
    const auto detected = resolveDownloadSize(0, 2048, false, probe);
    assert(detected.valid && detected.known && detected.probed && detected.bytes == 1234);
    assert(probeCalls == 1);
    const auto unknown = resolveDownloadSize(0, 2048, true,
        [&](unsigned long long& bytes) { ++probeCalls; bytes = 999; return false; });
    assert(unknown.valid && !unknown.known && unknown.probed && unknown.bytes == 0);
    assert(probeCalls == 2);

    static_assert(!std::is_copy_constructible_v<DownloadControl>);
    DownloadControl control;
    assert(!control.cancelRequested());
    assert(!control.cancelled());
    assert(!downloadCancellationRequested(nullptr));
    assert(!control.hasInputPoll());
    control.pollInput();
    assert(!control.cancelRequested());

    // Binding the input hook does not poll or cancel by itself. Only explicit
    // caller-thread ticks do so; workers still use the atomic request.
    DownloadControl polled;
    InputPollState input{std::this_thread::get_id(), &polled};
    polled.setInputPoll(requestFromInput, &input);
    assert(polled.hasInputPoll() && input.calls == 0 && !polled.cancelRequested());
    polled.pollInput();
    assert(input.calls == 1 && polled.cancelRequested() && !polled.cancelled());
    polled.setInputPoll(nullptr, nullptr);
    polled.pollInput();
    assert(input.calls == 1 && !polled.hasInputPoll());

    // The UI request and completion acknowledgement are different events.
    std::thread requester([&] { control.requestCancel(); });
    requester.join();
    assert(control.cancelRequested());
    assert(!control.cancelled());
    control.markCancelled();
    assert(control.cancelled());
    control.requestCancel(); // Repeated input is harmless.
    assert(control.cancelRequested());

    // Drain already accepted bytes on user cancellation, including a partial
    // last queue block. The prefix/hash are inspected only after the join.
    SinkState sink;
    DownloadPipe pipe(4096);
    assert(pipe.start(slowSink, &sink));
    const std::string payload(4096 * 3 + 137, 'Q');
    assert(pipe.push(payload.data(), payload.size()));
    DownloadControl cancellation;
    cancellation.requestCancel();
    assert(pipe.finish(downloadCancellationRequested(&cancellation)));
    cancellation.markCancelled();
    assert(sink.bytes == payload);
    assert(sink.hash.finish() == digest(payload));
    assert(cancellation.cancelled());

    const auto retain = [](bool known = true, bool closed = true, bool writeFailed = false,
                           bool limit = false, bool resetFailed = false,
                           bool interruption = true, unsigned long long bytes = 100,
                           bool validBody = true, unsigned long long offset = 0) {
        return retainInterruptedDownload(known, closed, writeFailed, limit, resetFailed,
                                         interruption, bytes, validBody, offset);
    };
    assert(retain());
    assert(!retain(false));
    assert(!retain(true, false));
    assert(!retain(true, true, true));
    assert(!retain(true, true, false, true));
    assert(!retain(true, true, false, false, true));
    assert(!retain(true, true, false, false, false, false));
    assert(!retain(true, true, false, false, false, true, 0));
    assert(!retain(true, true, false, false, false, true, 100, false));
    assert(retain(true, true, false, false, false, true, 100, false, 100));
    assert(!retain(true, true, false, false, false, true, 101, false, 100));

    assert(measuredDownloadRate(1024.0 * 1024.0 * 1.6) == "1.60 MiB/s");
    assert(measuredDownloadRate(300.0 * 1024.0) == "0.29 MiB/s");
    assert(measuredDownloadRate(0).empty());
    assert(measuredDownloadRate(-1).empty());
    assert(measuredDownloadRate(std::numeric_limits<double>::infinity()).empty());
    assert(measuredDownloadRate(std::numeric_limits<double>::quiet_NaN()).empty());
    std::puts("Download cancellation and retention policy tests passed");
}
