#include "file_hash_stream.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace unbound;
using namespace unbound::file_hash_detail;

const std::string AbcDigest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
const std::string BinaryDigest = "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880";

struct Reader {
    std::string data = "abc";
    std::size_t position = 0, chunk = 2;
    unsigned reads = 0, closes = 0, sizes = 0;
    unsigned failRead = 0, zeroRead = 0, oversizedRead = 0;
    int closeError = 0, sizeError = 0;
    bool regular = true, delayedEof = false;
    std::uint64_t finalBytes = 3;

    ReadResult read(void* buffer, std::size_t capacity) {
        ++reads;
        if (reads == zeroRead) return {};
        if (reads == oversizedRead) return {capacity + 1, 0, false};
        const auto bytes = std::min({capacity, chunk, data.size() - position});
        std::memcpy(buffer, data.data() + position, bytes);
        position += bytes;
        return {bytes, reads == failRead ? EACCES : 0,
                position == data.size() && (!delayedEof || bytes == 0)};
    }
    SizeResult size() { ++sizes; return {finalBytes, sizeError, regular}; }
    int close() { ++closes; return closeError; }
};

void knownHashesAndProgress() {
    Reader reader;
    unsigned char buffer[7]{};
    std::string output = "unchanged";
    std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;
    FileHashOptions options;
    options.progress = [&](auto bytes, auto total) { progress.emplace_back(bytes, total); };
    auto result = hashStream(reader, buffer, sizeof(buffer), 3, options, output);
    assert(result.ok() && result.bytes == 3 && result.total == 3 && !result.errorNumber);
    assert(output == AbcDigest && reader.closes == 1 && reader.sizes == 1);
    assert((progress == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{0,3},{2,3},{3,3}}));
    reader = {};
    reader.chunk = 3; reader.delayedEof = true;
    unsigned char exactBlock[3]{};
    result = hashStream(reader, exactBlock, sizeof(exactBlock), 3, {}, output);
    assert(result.ok() && reader.reads == 2 && output == AbcDigest); // Exact block needs EOF read.
    reader = {};
    reader.data.clear();
    for (unsigned value = 0; value < 256; ++value) reader.data += static_cast<char>(value);
    reader.finalBytes = reader.data.size();
    reader.chunk = 19;
    options.expectedBytes = 256;
    progress.clear();
    result = hashStream(reader, buffer, sizeof(buffer), 256, options, output);
    assert(result.ok() && output == BinaryDigest && result.bytes == 256);
    assert(reader.reads > 1 && reader.closes == 1);
    assert(progress.front().first == 0 && progress.back().first == 256);
    for (std::size_t i = 1; i < progress.size(); ++i)
        assert(progress[i].first > progress[i-1].first && progress[i].second == 256);
    reader = {};
    reader.data.clear(); reader.finalBytes = 0;
    result = hashStream(reader, buffer, sizeof(buffer), 0, {}, output);
    assert(result.ok() && output == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(reader.reads == 1 && reader.closes == 1);
}

void capturedFailures() {
    unsigned char buffer[7]{};
    const auto rejected = [&](Reader reader, std::uint64_t total, FileHashStatus status,
                               int code, const FileHashOptions& options = {}) {
        std::string output = "keep the previous digest";
        const auto result = hashStream(reader, buffer, sizeof(buffer), total, options, output);
        assert(result.status == status && result.errorNumber == code);
        assert(output == "keep the previous digest" && reader.closes == 1);
        return reader;
    };
    auto reader = Reader{};
    rejected(reader, 4, FileHashStatus::Truncated, EIO);
    rejected(reader, 2, FileHashStatus::SizeChanged, EFBIG);
    reader.failRead = 1;
    reader.closeError = EIO;
    reader = rejected(reader, 3, FileHashStatus::ReadFailed, EACCES);
    assert(reader.reads == 1 && reader.sizes == 0); // Preserve primary error.
    reader = {};
    reader.zeroRead = 1;
    reader = rejected(reader, 3, FileHashStatus::ReadFailed, EIO);
    assert(reader.reads == 1); // Zero progress without EOF cannot spin forever.
    reader = {};
    reader.oversizedRead = 1;
    rejected(reader, 3, FileHashStatus::ReadFailed, EIO);
    reader = {};
    reader.finalBytes = 4;
    rejected(reader, 3, FileHashStatus::SizeChanged, EIO);
    reader = {};
    reader.sizeError = EPERM;
    rejected(reader, 3, FileHashStatus::StatFailed, EPERM);
    reader = {};
    reader.regular = false;
    rejected(reader, 3, FileHashStatus::NotRegular, EINVAL);
    reader = {};
    reader.closeError = ENOSPC;
    rejected(reader, 3, FileHashStatus::CloseFailed, ENOSPC);
    reader = {};
    FileHashOptions mismatched;
    mismatched.expectedBytes = 4;
    reader = rejected(reader, 3, FileHashStatus::SizeMismatch, EINVAL, mismatched);
    assert(reader.reads == 0);
    reader = {};
    std::string output = "unchanged";
    const auto noBuffer = hashStream(reader, nullptr, 0, 3, {}, output);
    assert(noBuffer.status == FileHashStatus::BufferUnavailable && noBuffer.errorNumber == ENOMEM);
    assert(reader.closes == 1 && reader.reads == 0 && output == "unchanged");
}

void cancellation() {
    unsigned char buffer[7]{};
    for (const unsigned afterBytes : {0U, 2U, 3U}) {
        Reader reader;
        bool cancelled = afterBytes == 0;
        FileHashOptions options;
        options.cancelled = [&] { return cancelled; };
        options.progress = [&](auto bytes, auto) { if (bytes >= afterBytes) cancelled = true; };
        std::string output = "unchanged";
        const auto result = hashStream(reader, buffer, sizeof(buffer), 3, options, output);
        assert(result.status == FileHashStatus::Cancelled && result.errorNumber == ECANCELED);
        assert(result.bytes == afterBytes && output == "unchanged" && reader.closes == 1);
        if (afterBytes == 0) assert(reader.reads == 0);
    }
}

void realFileAdapter() {
    char path[] = "/tmp/unbound-file-hash-XXXXXX";
    const int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE* file = fdopen(descriptor, "wb");
    assert(file && std::fwrite("abc", 1, 3, file) == 3 && std::fclose(file) == 0);
    std::string output = "unchanged";
    FileHashOptions options;
    options.expectedBytes = 3;
    std::vector<std::uint64_t> progress;
    options.progress = [&](auto bytes, auto) { progress.push_back(bytes); };
    const auto result = sha256FileDetailed(path, output, options);
    assert(result.ok() && result.bytes == 3 && result.total == 3 && output == AbcDigest);
    assert(progress.front() == 0 && progress.back() == 3);
    assert(sha256File(path, output) && output == AbcDigest); // Legacy entry point.
    bool cancel = false;
    options.cancelled = [&] { return cancel; };
    options.progress = [&](auto bytes, auto) { if (bytes) cancel = true; };
    output = "unchanged";
    const auto cancelled = sha256FileDetailed(path, output, options);
    assert(cancelled.status == FileHashStatus::Cancelled && output == "unchanged");
    std::string preserved;
    assert(sha256File(path, preserved) && preserved == AbcDigest); // No cancelled-file removal.
    options.cancelled = {};
    options.progress = {};
    options.expectedBytes = 4;
    output = "unchanged";
    const auto mismatch = sha256FileDetailed(path, output, options);
    assert(mismatch.status == FileHashStatus::SizeMismatch && output == "unchanged");
    const std::string link = std::string(path) + ".link";
    assert(symlink(path, link.c_str()) == 0);
    const auto linked = sha256FileDetailed(link, output);
    assert(linked.status == FileHashStatus::NotRegular && output == "unchanged");
    assert(std::remove(link.c_str()) == 0 && std::remove(path) == 0);
    const auto missing = sha256FileDetailed(path, output);
    assert(missing.status == FileHashStatus::StatFailed && missing.errorNumber == ENOENT);
    assert(output == "unchanged");
}
} // namespace

int main() {
    knownHashesAndProgress();
    capturedFailures();
    cancellation();
    realFileAdapter();
    std::cout << "Detailed file hashing: known binary SHA, progress, cancellation, exact length, I/O and close failures passed\n";
}
