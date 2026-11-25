#pragma once

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>

namespace unbound {

// One object per download. UI input only requests cancellation; the download
// owner acknowledges it after its network and file workers have safely joined.
class DownloadControl {
public:
    // Bind before starting any download workers. Only the owning UI/caller
    // thread invokes this hook; network and writer workers read the atomic
    // cancellation flag only. Polling does not imply a progress redraw.
    void setInputPoll(void (*poll)(void*), void* context) noexcept {
        inputPoll_ = poll;
        inputContext_ = context;
    }
    bool hasInputPoll() const noexcept { return inputPoll_ != nullptr; }
    void pollInput() const {
        if (inputPoll_) inputPoll_(inputContext_);
    }

    void requestCancel() noexcept {
        requested_.store(true, std::memory_order_release);
    }
    bool cancelRequested() const noexcept {
        return requested_.load(std::memory_order_acquire);
    }
    bool cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }
    void markCancelled() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

private:
    void (*inputPoll_)(void*) = nullptr;
    void* inputContext_ = nullptr;
    std::atomic<bool> requested_{false};
    std::atomic<bool> cancelled_{false};
};

inline bool downloadCancellationRequested(const DownloadControl* control) noexcept {
    return control && control->cancelRequested();
}

// A user cancellation can retain a known release's clean prefix, just like a
// network interruption. Disk failures, unsafe responses and unknown hashes cannot.
inline bool retainInterruptedDownload(bool knownHash, bool closedCleanly,
                                      bool writeFailed, bool limitExceeded,
                                      bool retryResetFailed, bool recoverableInterruption,
                                      unsigned long long received,
                                      bool validBody, unsigned long long resumeOffset) noexcept {
    return knownHash && closedCleanly && !writeFailed && !limitExceeded &&
           !retryResetFailed && recoverableInterruption && received &&
           (validBody || (resumeOffset && received == resumeOffset));
}

inline std::string measuredDownloadRate(double bytesPerSecond) {
    if (!std::isfinite(bytesPerSecond) || bytesPerSecond <= 0.0) return {};
    char value[48]{};
    std::snprintf(value, sizeof(value), "%.2f MiB/s", bytesPerSecond / (1024.0 * 1024.0));
    return value;
}

} // namespace unbound
