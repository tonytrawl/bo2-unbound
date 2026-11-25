#pragma once

#include "download_pipe.hpp"
#include "operation_metrics.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace unbound {

enum class InstallStreamResult { Success, ReadFailed, SizeMismatch, WriteFailed };

namespace install_stream_detail {

template <typename Writer>
struct Sink {
    Writer& write;
    OperationMetrics& metrics;

    static bool consume(void* opaque, const char* bytes, std::size_t count) {
        auto& sink = *static_cast<Sink*>(opaque);
        const auto began = operationMetricsNow();
        const auto written = sink.write(bytes, count);
        sink.metrics.writeTicks += operationMetricsNow() - began;
        sink.metrics.bytesWritten += written;
        return written == count;
    }
};

} // namespace install_stream_detail

// Only the producer touches the archive and progress callback; only the sink
// touches the destination FILE. Every path joins the sink before returning, so
// callers can safely close/CRC-check/delete/swap the temporary file afterwards.
// Small files and allocation/thread failures retain the direct write path.
template <typename Reader, typename Writer, typename Observer>
InstallStreamResult streamInstallEntry(unsigned long long expectedBytes,
                                        char* buffer, std::size_t bufferBytes,
                                        Reader&& read, Writer&& write,
                                        Observer&& observe, OperationMetrics& metrics) {
    if (!buffer || !bufferBytes) return InstallStreamResult::ReadFailed;
    install_stream_detail::Sink<std::remove_reference_t<Writer>> sink{write, metrics};
    constexpr unsigned long long PipelineThreshold = 1024 * 1024;
    DownloadPipe pipe(expectedBytes >= PipelineThreshold ? 256 * 1024 : 0);
    const bool pipelined = expectedBytes >= PipelineThreshold &&
        pipe.start(decltype(sink)::consume, &sink);
    if (pipelined) ++metrics.pipelinedFiles;
    else ++metrics.synchronousFiles;

    unsigned long long unpacked = 0;
    auto result = InstallStreamResult::Success;
    for (;;) {
        const auto began = operationMetricsNow();
        const std::int64_t count = read(buffer, bufferBytes);
        metrics.archiveReadTicks += operationMetricsNow() - began;
        if (count < 0) { result = InstallStreamResult::ReadFailed; break; }
        if (!count) break;
        const auto bytes = static_cast<std::uint64_t>(count);
        if (bytes > bufferBytes || unpacked > expectedBytes || bytes > expectedBytes - unpacked) {
            result = InstallStreamResult::SizeMismatch;
            break;
        }
        metrics.bytesRead += bytes;
        const auto chunk = static_cast<std::size_t>(bytes);
        const bool accepted = pipelined ? pipe.push(buffer, chunk)
                                       : decltype(sink)::consume(&sink, buffer, chunk);
        if (!accepted) { result = InstallStreamResult::WriteFailed; break; }
        unpacked += bytes;
        observe(chunk);
    }
    if (result == InstallStreamResult::Success && unpacked != expectedBytes)
        result = InstallStreamResult::SizeMismatch;
    if (pipelined) {
        const bool drained = pipe.finish(result == InstallStreamResult::Success);
        metrics.queueWaitTicks += pipe.waitTicks();
        if (!drained && result == InstallStreamResult::Success)
            result = InstallStreamResult::WriteFailed;
    }
    return result;
}

} // namespace unbound
