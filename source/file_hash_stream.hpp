#pragma once

#include "sha256.hpp"

#include <cerrno>
#include <limits>

namespace unbound::file_hash_detail {

struct ReadResult {
    std::size_t bytes = 0;
    int error = 0;
    bool eof = false;
};

struct SizeResult {
    std::uint64_t bytes = 0;
    int error = 0;
    bool regular = true;
};

// Reader supplies captured-error read/size/close methods. It is already open;
// every exit closes it once. Tests inject short reads, I/O failures and changing
// lengths without a real filesystem or thread. No retry can spin on zero data.
template<class Reader>
FileHashResult hashStream(Reader& reader, void* buffer, std::size_t bufferBytes,
                          std::uint64_t total, const FileHashOptions& options,
                          std::string& output) {
    FileHashResult result{FileHashStatus::Ok, 0, 0, total};
    const auto closeWith = [&](FileHashStatus status, int error) {
        result.status = status;
        result.errorNumber = error;
        const int closeError = reader.close();
        // The original read/cancel failure is more useful than a later close
        // failure. A successful hash requires successful close too.
        if (status == FileHashStatus::Ok && closeError) {
            result.status = FileHashStatus::CloseFailed;
            result.errorNumber = closeError;
        }
        return result;
    };
    const auto cancelled = [&]() { return options.cancelled && options.cancelled(); };
    if (cancelled()) return closeWith(FileHashStatus::Cancelled, ECANCELED);
    if (!buffer || !bufferBytes) return closeWith(FileHashStatus::BufferUnavailable, ENOMEM);
    if (options.expectedBytes && options.expectedBytes != total)
        return closeWith(FileHashStatus::SizeMismatch, EINVAL);
    if (options.progress) options.progress(0, total);
    Sha256 hash;
    for (;;) {
        if (cancelled()) return closeWith(FileHashStatus::Cancelled, ECANCELED);
        const auto read = reader.read(buffer, bufferBytes);
        if (read.bytes > bufferBytes ||
            read.bytes > std::numeric_limits<std::uint64_t>::max() - result.bytes)
            return closeWith(FileHashStatus::ReadFailed, EIO);
        result.bytes += read.bytes;
        if (read.error) return closeWith(FileHashStatus::ReadFailed, read.error);
        if (result.bytes > total) return closeWith(FileHashStatus::SizeChanged, EFBIG);
        if (read.bytes) {
            hash.update(buffer, read.bytes);
            if (options.progress) options.progress(result.bytes, total);
        }
        if (read.eof) {
            if (result.bytes != total) return closeWith(FileHashStatus::Truncated, EIO);
            break;
        }
        if (!read.bytes) return closeWith(FileHashStatus::ReadFailed, EIO);
    }
    if (cancelled()) return closeWith(FileHashStatus::Cancelled, ECANCELED);
    const auto finalSize = reader.size();
    if (finalSize.error) return closeWith(FileHashStatus::StatFailed, finalSize.error);
    if (!finalSize.regular) return closeWith(FileHashStatus::NotRegular, EINVAL);
    if (finalSize.bytes != total) return closeWith(FileHashStatus::SizeChanged, EIO);
    const auto completed = closeWith(FileHashStatus::Ok, 0);
    if (completed.ok()) output = hash.finish();
    return completed;
}

} // namespace unbound::file_hash_detail
