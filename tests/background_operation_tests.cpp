#include "background_operation.hpp"
#include "operation_metrics.hpp"
#include "install_activity.hpp"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {

bool waitFor(const std::atomic<bool>& value) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!value.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return value.load();
}

void rendersOnCallerWithoutBlockingWorker() {
    const auto caller = std::this_thread::get_id();
    std::atomic<bool> rendering{false};
    std::atomic<bool> finished{false};
    bool usedWorker = false;
    bool finishedDuringRender = false;
    unsigned frames = 0;
    int lastPercent = -1;
    const unbound::BackgroundProgressCallback render =
        [&](const std::string& title, const std::string& detail, int percent) {
            assert(std::this_thread::get_id() == caller);
            assert(title == "Installing files");
            assert(!detail.empty());
            ++frames;
            lastPercent = percent;
            if (percent == 1) {
                rendering = true;
                // A long UI frame must not retain the worker's progress mutex.
                finishedDuringRender = waitFor(finished);
            }
        };
    const bool result = unbound::runBackgroundOperation("install test", render,
        [&](const unbound::BackgroundProgressCallback& progress) {
            assert(std::this_thread::get_id() != caller);
            progress("Installing files", "First file", 1);
            assert(waitFor(rendering));
            for (int percent = 2; percent <= 100; ++percent)
                progress("Installing files", "File " + std::to_string(percent), percent);
            finished = true;
            return true;
        }, &usedWorker);
    assert(result && usedWorker && finishedDuringRender && finished);
    assert(lastPercent == 100);
    assert(frames < 100); // Progress snapshots coalesce during a slow render.
    assert(fake_sdl::lastRequestedStackBytes == 256 * 1024);
    assert(fake_sdl::noHandles());
}

void joinsBeforeExposingFailureAndCompletionState() {
    std::string error;
    std::string state;
    bool usedWorker = false;
    const unbound::BackgroundProgressCallback progress =
        [](const std::string&, const std::string&, int) {};
    const bool result = unbound::runBackgroundOperation("rollback test", progress,
        [&](const unbound::BackgroundProgressCallback& relay) {
            relay("Installing files", "Rolling back", 75);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            state = "rollback complete";
            error = "write failed";
            return false;
        }, &usedWorker);
    assert(!result && usedWorker);
    assert(state == "rollback complete" && error == "write failed");
    assert(fake_sdl::noHandles());
}

void retainsFinalProgressAfterMissedPublish() {
    const auto caller = std::this_thread::get_id();
    fake_sdl::tryLockFailures = 2;
    int finalPercent = -1;
    const unbound::BackgroundProgressCallback progress =
        [&](const std::string&, const std::string& detail, int percent) {
            assert(std::this_thread::get_id() == caller);
            assert(detail == "Complete");
            finalPercent = percent;
        };
    assert(unbound::runBackgroundOperation("final test", progress,
        [](const unbound::BackgroundProgressCallback& relay) {
            relay("Install", "Preparing", 0);
            relay("Install", "Complete", 100);
            return true;
        }));
    assert(finalPercent == 100 && fake_sdl::tryLockFailures == 0);
    assert(fake_sdl::noHandles());
}

void retainsPreparationBeforeAWaitingFilesystemCall() {
    const auto caller=std::this_thread::get_id();
    fake_sdl::tryLockFailures=1;
    std::atomic<bool> rendered{false};
    assert(unbound::runBackgroundOperation("preflight status",
        [&](const std::string& title,const std::string& detail,int percent){
            assert(std::this_thread::get_id()==caller);
            assert(title=="Preparing destination folders"&&detail=="stat: storage_usb01:/content/english/sound/loaded");
            assert(percent==-1);
            rendered=true;
        },[&](const unbound::BackgroundProgressCallback& relay){
            relay("Preparing destination folders","stat: storage_usb01:/content/english/sound/loaded",-1);
            // No later callback can rescue this report: the simulated storage
            // call waits until the UI has actually received the current path.
            assert(waitFor(rendered));
            return true;
        }));
    assert(rendered&&fake_sdl::tryLockFailures==0&&fake_sdl::noHandles());
}

void keepsWaitingStageVisibleWithoutInventingProgress() {
    const auto caller=std::this_thread::get_id();
    std::uint32_t now=1000;
    unbound::InstallActivity activity(now);
    std::atomic<bool> noticeRendered{false};
    unsigned pulses=0;
    const auto render=[&](const unbound::InstallActivitySnapshot& frame){
        assert(std::this_thread::get_id()==caller);
        assert(frame.percent==-1&&frame.title=="Checking storage");
        assert(frame.detail.find("Checking available space on USB storage")!=std::string::npos);
        assert(frame.detail.find("Elapsed ")!=std::string::npos);
        if(frame.detail.find("This step is taking longer.")!=std::string::npos)noticeRendered=true;
    };
    const bool result=unbound::runBackgroundOperation("quiet preflight",
        [&](const std::string& title,const std::string& detail,int percent){
            activity.observe(title,detail,percent,now);
            render(activity.snapshot(now));
        },[&](const unbound::BackgroundProgressCallback& relay){
            relay("Checking storage","Checking available space on USB storage",-1);
            assert(waitFor(noticeRendered));
            return false; // A completed filesystem failure still joins safely.
        },nullptr,[&]{
            now+=500; // Deterministic presentation time; no long test sleep.
            if(activity.pulseDue(now)){++pulses;render(activity.snapshot(now));}
        });
    assert(!result&&noticeRendered&&pulses>=15&&fake_sdl::noHandles());
}

void fallsBackOnStartupFailure(fake_sdl::Failure failure) {
    fake_sdl::nextFailure = failure;
    const auto caller = std::this_thread::get_id();
    bool usedWorker = true;
    unsigned progressCalls = 0;
    const unbound::BackgroundProgressCallback progress =
        [&](const std::string&, const std::string&, int) {
            assert(std::this_thread::get_id() == caller);
            ++progressCalls;
        };
    assert(unbound::runBackgroundOperation("fallback test", progress,
        [&](const unbound::BackgroundProgressCallback& relay) {
            assert(std::this_thread::get_id() == caller);
            relay("Install", "Complete", 100);
            return true;
        }, &usedWorker));
    assert(!usedWorker && progressCalls == 1);
    assert(fake_sdl::nextFailure == fake_sdl::Failure::None);
    assert(fake_sdl::noHandles());
}

void skipsWorkerWhenNoProgressConsumerExists() {
    const auto caller = std::this_thread::get_id();
    bool usedWorker = true;
    assert(unbound::runBackgroundOperation("headless test", {},
        [&](const unbound::BackgroundProgressCallback& progress) {
            assert(!progress && std::this_thread::get_id() == caller);
            return true;
        }, &usedWorker));
    assert(!usedWorker && fake_sdl::noHandles());
}

void inputTicksDoNotRequireProgressFrames() {
    const auto caller = std::this_thread::get_id();
    std::atomic<unsigned> ticks{0};
    bool usedWorker = false;
    unsigned progressFrames = 0;
    const unbound::BackgroundTickCallback tick = [&] {
        assert(std::this_thread::get_id() == caller);
        ++ticks;
    };
    const unbound::BackgroundProgressCallback progress =
        [&](const std::string&, const std::string&, int) {
            assert(std::this_thread::get_id() == caller);
            ++progressFrames;
        };
    assert(unbound::runBackgroundOperation("input polling test", progress,
        [&](const unbound::BackgroundProgressCallback&) {
            assert(std::this_thread::get_id() != caller);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (ticks.load() < 4 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            assert(ticks.load() >= 4);
            return true;
        }, &usedWorker, tick));
    assert(usedWorker && ticks >= 4 && progressFrames == 0);
    assert(fake_sdl::noHandles());
}

void tickOnlyConsumerStillRunsOnCaller() {
    const auto caller = std::this_thread::get_id();
    unsigned ticks = 0;
    bool usedWorker = false;
    assert(unbound::runBackgroundOperation("input only test", {},
        [&](const unbound::BackgroundProgressCallback& relay) {
            assert(std::this_thread::get_id() != caller);
            relay("Download", "Checking size", 0);
            return true;
        }, &usedWorker, [&] {
            assert(std::this_thread::get_id() == caller);
            ++ticks;
        }));
    assert(usedWorker && ticks >= 2 && fake_sdl::noHandles());
}

void ticksRemainOnCallerDuringFallback(fake_sdl::Failure failure) {
    const auto caller = std::this_thread::get_id();
    fake_sdl::nextFailure = failure;
    unsigned ticks = 0, progressFrames = 0;
    bool usedWorker = true;
    assert(unbound::runBackgroundOperation("input fallback test",
        [&](const std::string&, const std::string&, int) {
            assert(std::this_thread::get_id() == caller);
            ++progressFrames;
        }, [&](const unbound::BackgroundProgressCallback& relay) {
            assert(std::this_thread::get_id() == caller);
            relay("Download", "Checking size", 0);
            relay("Download", "Complete", 100);
            return true;
        }, &usedWorker, [&] {
            assert(std::this_thread::get_id() == caller);
            ++ticks;
        }));
    assert(!usedWorker && ticks == 4 && progressFrames == 2);
    assert(fake_sdl::nextFailure == fake_sdl::Failure::None && fake_sdl::noHandles());
}

void metricsUseNativeTicksAndIgnoreLogFailure() {
    const auto frequency = SDL_GetPerformanceFrequency();
    assert(unbound::operationMetricsMilliseconds(frequency * 4 + frequency / 2) == 4500);
    const auto started = unbound::operationMetricsNow();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(unbound::operationMetricsNow() > started);
    // The Wii U SD path does not exist in this host test. Diagnostics must
    // remain harmless even when fopen/stat fail, including preserving errno.
    errno = EBUSY;
    unbound::appendPerformanceLog("test", false, "bytes=0");
    assert(errno == EBUSY);
}

} // namespace

int main() {
    rendersOnCallerWithoutBlockingWorker();
    joinsBeforeExposingFailureAndCompletionState();
    retainsFinalProgressAfterMissedPublish();
    retainsPreparationBeforeAWaitingFilesystemCall();
    keepsWaitingStageVisibleWithoutInventingProgress();
    fallsBackOnStartupFailure(fake_sdl::Failure::Mutex);
    fallsBackOnStartupFailure(fake_sdl::Failure::Thread);
    skipsWorkerWhenNoProgressConsumerExists();
    inputTicksDoNotRequireProgressFrames();
    tickOnlyConsumerStillRunsOnCaller();
    ticksRemainOnCallerDuringFallback(fake_sdl::Failure::Mutex);
    ticksRemainOnCallerDuringFallback(fake_sdl::Failure::Thread);
    metricsUseNativeTicksAndIgnoreLogFailure();
    std::puts("Background installation relay, completion, fallback and metrics tests passed.");
}
