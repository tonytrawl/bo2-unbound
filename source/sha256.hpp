#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace unbound {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t length);
    std::string finish();
private:
    void transform(const std::uint8_t block[64]);
    std::uint32_t state_[8];
    std::uint8_t buffer_[64]{};
    std::uint64_t total_ = 0;
    std::size_t used_ = 0;
    bool finished_ = false;
};

bool sha256File(const std::string& path, std::string& output);

enum class FileHashStatus {
    Ok, Cancelled, StatFailed, NotRegular, OpenFailed, SizeMismatch,
    BufferUnavailable, BufferSetupFailed, ReadFailed, Truncated,
    SizeChanged, CloseFailed
};

struct FileHashOptions {
    // Zero accepts the file's inspected length, including an empty file.
    std::uint64_t expectedBytes = 0;
    // Called once before reading and after each bounded block. Callers may
    // throttle UI relay; this layer performs no worker or UI operations.
    std::function<void(std::uint64_t bytes, std::uint64_t total)> progress;
    // May read an atomic cancellation flag. Cancellation never removes files.
    std::function<bool()> cancelled;
};

struct FileHashResult {
    FileHashStatus status = FileHashStatus::Ok;
    int errorNumber = 0;
    std::uint64_t bytes = 0;
    std::uint64_t total = 0;
    bool ok() const noexcept { return status == FileHashStatus::Ok; }
};

// A digest is committed to output only after exact EOF, a final unchanged-size
// check and successful close. All failures leave the caller's output intact.
FileHashResult sha256FileDetailed(const std::string& path, std::string& output,
                                  const FileHashOptions& options = {});

} // namespace unbound
