#pragma once

#include "config.hpp"

#include <SDL2/SDL.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace unbound {

inline std::uint64_t operationMetricsNow() {
    return SDL_GetPerformanceCounter();
}

inline std::uint64_t operationMetricsMilliseconds(std::uint64_t ticks) {
    const auto frequency = SDL_GetPerformanceFrequency();
    return frequency ? (ticks / frequency) * 1000 +
        ((ticks % frequency) * 1000) / frequency : 0;
}

// Collect native timer ticks inside the worker; convert only after its join.
struct OperationMetrics {
    std::uint64_t totalTicks = 0;
    std::uint64_t archiveReadTicks = 0;
    std::uint64_t writeTicks = 0;
    std::uint64_t closeTicks = 0;
    std::uint64_t queueWaitTicks = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t bytesWritten = 0;
    std::uint64_t pipelinedFiles = 0;
    std::uint64_t synchronousFiles = 0;
    std::uint64_t inputBufferBytes = 0;
};

inline void appendPerformanceLog(const char* operation, bool success,
                                 const std::string& fields) {
    constexpr std::size_t MaximumLogBytes = 64 * 1024;
    constexpr std::size_t MaximumFieldsBytes = 1024;
    const int previousErrno = errno;
    const std::string path = std::string(config::SdRoot) + "/performance.log";
    const std::string line = std::string("r") + std::to_string(config::AppReleaseRevision) +
        "|" + operation + "|ok=" +
        (success ? "1|" : "0|") + fields.substr(0, MaximumFieldsBytes) + "\n";
    struct stat info{};
    // Restart a full diagnostic log so the latest run remains available.
    const bool full = stat(path.c_str(), &info) == 0 && info.st_size >= 0 &&
        static_cast<std::uint64_t>(info.st_size) + line.size() > MaximumLogBytes;
    if (FILE* output = std::fopen(path.c_str(), full ? "wb" : "ab")) {
        (void)std::fwrite(line.data(), 1, line.size(), output);
        (void)std::fclose(output);
    }
    // Diagnostics are best-effort and never change the operation's outcome.
    errno = previousErrno;
}

} // namespace unbound
