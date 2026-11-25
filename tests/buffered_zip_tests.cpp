#include "buffered_zip.hpp"
#include "archive_limits.hpp"
#include "tester_archive.hpp"
#include "install_stream.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class Scratch {
public:
    Scratch() {
        path = std::filesystem::temp_directory_path() /
            ("unbound-buffered-zip-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        assert(std::filesystem::create_directory(path));
    }
    ~Scratch() { std::filesystem::remove_all(path); }
    std::filesystem::path path;
};

std::vector<char> payload(std::size_t bytes) {
    std::vector<char> result(bytes);
    std::uint32_t state = 0x6c8e9cf5;
    for (auto& byte : result) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = static_cast<char>(state);
    }
    return result;
}

void makeArchive(const std::filesystem::path& path,
                 const std::vector<char>& bytes, bool twoEntries) {
    int error = 0;
    zip_t* archive = zip_open(path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    assert(archive);
    zip_source_t* source = zip_source_buffer(archive, bytes.data(), bytes.size(), 0);
    assert(source);
    const auto stored = zip_file_add(archive, "update/content/stored.ff", source, 0);
    assert(stored >= 0);
    assert(zip_set_file_compression(archive, stored, ZIP_CM_STORE, 0) == 0);
    if (twoEntries) {
        source = zip_source_buffer(archive, bytes.data(), bytes.size(), 0);
        assert(source);
        const auto deflated = zip_file_add(archive, "update/code/deflated.rpl", source, 0);
        assert(deflated >= 0);
        assert(zip_set_file_compression(archive, deflated, ZIP_CM_DEFLATE, 6) == 0);
    }
    assert(zip_close(archive) == 0);
}

void checkEntry(zip_t* archive, zip_uint64_t index, const std::vector<char>& expected) {
    zip_stat_t info;
    assert(zip_stat_index(archive, index, ZIP_FL_UNCHANGED, &info) == 0);
    assert(info.size == expected.size());
    zip_file_t* entry = zip_fopen_index(archive, index, ZIP_FL_UNCHANGED);
    assert(entry);
    // Vary consumer read sizes independently of FILE's buffer boundaries.
    constexpr std::size_t ChunkBytes = 65537;
    std::vector<char> chunk(ChunkBytes);
    std::size_t completed = 0;
    unbound::Sha256 actualHash;
    for (;;) {
        const auto count = zip_fread(entry, chunk.data(), chunk.size());
        assert(count >= 0);
        if (!count) break;
        assert(static_cast<std::size_t>(count) <= expected.size() - completed);
        actualHash.update(chunk.data(), static_cast<std::size_t>(count));
        completed += static_cast<std::size_t>(count);
    }
    assert(zip_fclose(entry) == 0);
    assert(completed == expected.size());
    unbound::Sha256 expectedHash;
    expectedHash.update(expected.data(), expected.size());
    assert(actualHash.finish() == expectedHash.finish());
}

void checkPipelinedEntry(zip_t* archive, zip_uint64_t index, const std::vector<char>& expected) {
    zip_file_t* entry = zip_fopen_index(archive, index, ZIP_FL_UNCHANGED);
    assert(entry);
    unbound::AlignedIoBuffer transfer, outputBuffer;
    FILE* output = std::tmpfile();
    assert(output && transfer && outputBuffer);
    assert(std::setvbuf(output, outputBuffer.data(), _IOFBF, outputBuffer.size()) == 0);
    unbound::OperationMetrics metrics;
    std::size_t progressBytes = 0;
    const auto result = unbound::streamInstallEntry(expected.size(), transfer.data(), transfer.size(),
        [entry](char* data, std::size_t size) { return zip_fread(entry, data, size); },
        [output](const char* data, std::size_t size) { return std::fwrite(data, 1, size, output); },
        [&](std::size_t bytes) { progressBytes += bytes; }, metrics);
    assert(result == unbound::InstallStreamResult::Success);
    // EOF, CRC and all queued writes completed before any output reuse/commit.
    assert(zip_fclose(entry) == 0 && fake_sdl::noHandles());
    assert(metrics.pipelinedFiles == 1 && metrics.bytesWritten == expected.size());
    assert(metrics.bytesRead == expected.size() && progressBytes == expected.size());
    assert(std::fflush(output) == 0 && std::fseek(output, 0, SEEK_SET) == 0);
    unbound::Sha256 actualHash, expectedHash;
    for (;;) {
        const auto bytes = std::fread(transfer.data(), 1, transfer.size(), output);
        if (!bytes) break;
        actualHash.update(transfer.data(), bytes);
    }
    assert(!std::ferror(output) && std::fclose(output) == 0);
    expectedHash.update(expected.data(), expected.size());
    assert(actualHash.finish() == expectedHash.finish());
}

std::size_t openDescriptors() {
    const std::filesystem::path path("/proc/self/fd");
    if (!std::filesystem::exists(path)) return 0;
    return static_cast<std::size_t>(std::distance(
        std::filesystem::directory_iterator(path), std::filesystem::directory_iterator{}));
}

void storedDeflatedReadAheadAndFallback(const std::filesystem::path& path,
                                      const std::vector<char>& bytes) {
    const auto before = openDescriptors();
    for (const std::size_t bufferBytes : {0U, 16384U, 262144U, 1048576U}) {
        unbound::BufferedZipReader reader(bufferBytes);
        assert(reader.open(path.c_str()));
        assert(zip_get_num_entries(reader.get(), ZIP_FL_UNCHANGED) == 2);
        checkEntry(reader.get(), 1, bytes);
        checkPipelinedEntry(reader.get(), 0, bytes);
        checkPipelinedEntry(reader.get(), 1, bytes);
        checkEntry(reader.get(), 0, bytes);
        checkEntry(reader.get(), 1, bytes);
        assert(reader.close());
        assert(reader.get() == nullptr);
        assert(reader.close());
        // Reopening an active reader must first release its previous FILE.
        for (int count = 0; count < 20; ++count) assert(reader.open(path.c_str()));
        // The destructor closes the last archive/source before freeing buffer.
    }
    assert(openDescriptors() == before);
}

void invalidArchivesReleaseSources(const std::filesystem::path& root,
                                  const std::filesystem::path& valid) {
    const auto invalid = root / "invalid.zip";
    {
        std::ofstream output(invalid, std::ios::binary);
        output << "Not a ZIP archive";
    }
    const auto before = openDescriptors();
    unbound::BufferedZipReader reader;
    for (int count = 0; count < 100; ++count) {
        assert(reader.open(valid.c_str()));
        assert(!reader.open(invalid.c_str()));
        assert(reader.get() == nullptr);
        assert(!reader.open((root / "missing.zip").c_str()));
        assert(reader.close());
    }
    assert(openDescriptors() == before);
}

unsigned little16(const unsigned char* bytes) {
    return bytes[0] | (static_cast<unsigned>(bytes[1]) << 8);
}

void storedEntryCrcStillRejectsCorruption(const std::filesystem::path& path,
                                         const std::vector<char>& bytes) {
    makeArchive(path, bytes, false);
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        unsigned char header[30]{};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        assert(file.gcount() == static_cast<std::streamsize>(sizeof(header)));
        assert(header[0] == 'P' && header[1] == 'K' && header[2] == 3 && header[3] == 4);
        const auto offset = static_cast<std::streamoff>(
            sizeof(header) + little16(header + 26) + little16(header + 28));
        file.seekg(offset);
        char value = 0;
        file.get(value);
        value ^= 0x5a;
        file.seekp(offset);
        file.put(value);
        assert(file.good());
    }
    unbound::BufferedZipReader reader;
    assert(reader.open(path.c_str()));
    zip_file_t* entry = zip_fopen_index(reader.get(), 0, ZIP_FL_UNCHANGED);
    assert(entry);
    std::vector<char> chunk(256 * 1024);
    bool rejected = false;
    for (;;) {
        const auto count = zip_fread(entry, chunk.data(), chunk.size());
        if (count < 0) { rejected = true; break; }
        if (!count) break;
    }
    if (zip_fclose(entry) != 0) rejected = true;
    assert(rejected);
    assert(reader.close());
    assert(reader.open(path.c_str()));
    entry = zip_fopen_index(reader.get(), 0, ZIP_FL_UNCHANGED);
    assert(entry);
    unbound::AlignedIoBuffer transfer;
    unbound::OperationMetrics metrics;
    const auto streamed = unbound::streamInstallEntry(bytes.size(), transfer.data(), transfer.size(),
        [entry](char* data, std::size_t size) { return zip_fread(entry, data, size); },
        [](const char*, std::size_t size) { return size; }, [](std::size_t) {}, metrics);
    const bool crcClosed = zip_fclose(entry) == 0;
    // A queued writer must not turn a bad CRC into a successful replacement.
    assert(streamed != unbound::InstallStreamResult::Success || !crcClosed);
    assert(fake_sdl::noHandles() && reader.close());
}

void combinedTesterArchive(const std::filesystem::path& path) {
    const auto compressed = std::filesystem::file_size(path);
    assert(compressed > 2ULL * 1024 * 1024 * 1024);
    assert(compressed <= unbound::config::MaxArchiveBytes);
    unbound::BufferedZipReader reader;
    assert(reader.open(path.c_str()));
    const auto count = zip_get_num_entries(reader.get(), ZIP_FL_UNCHANGED);
    assert(count > 0 && count <= unbound::config::MaxArchiveFiles);
    std::uint64_t expanded = 0;
    unsigned payloads = 0;
    unsigned readmes = 0;
    unbound::AlignedIoBuffer transfer;
    assert(transfer);
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index) {
        zip_stat_t info{};
        assert(zip_stat_index(reader.get(), index, ZIP_FL_UNCHANGED, &info) == 0);
        const std::string name(info.name);
        const bool directory = !name.empty() && name.back() == '/';
        assert(unbound::archiveSizeLimit(expanded, info.size) == unbound::ArchiveSizeLimit::None);
        expanded += info.size;
        if (unbound::testerReadmeEntry(name, directory, info.size)) {
            ++readmes;
            continue;
        }
        if (directory) continue;
        assert(name.rfind("update/code/", 0) == 0 || name.rfind("update/content/", 0) == 0);
        zip_file_t* entry = zip_fopen_index(reader.get(), index, ZIP_FL_UNCHANGED);
        assert(entry);
        unbound::Sha256 output;
        unbound::OperationMetrics metrics;
        const auto result = unbound::streamInstallEntry(info.size, transfer.data(), transfer.size(),
            [entry](char* bytes, std::size_t size) { return zip_fread(entry, bytes, size); },
            [&output](const char* bytes, std::size_t size) { output.update(bytes, size); return size; },
            [](std::size_t) {}, metrics);
        assert(result == unbound::InstallStreamResult::Success);
        assert(zip_fclose(entry) == 0 && fake_sdl::noHandles());
        assert(metrics.bytesRead == info.size && metrics.bytesWritten == info.size);
        ++payloads;
    }
    assert(payloads == 101 && readmes == 1 && expanded == 4755944782ULL);
    assert(reader.close());
    std::printf("Combined tester passed: %u payloads, root readme skipped, %llu expanded bytes streamed with CRC checks.\n",
                payloads, static_cast<unsigned long long>(expanded));
}

} // namespace

int main(int argc, char** argv) {
    Scratch scratch;
    const auto bytes = payload(3 * 1024 * 1024 + 37);
    const auto archive = scratch.path / "valid.zip";
    makeArchive(archive, bytes, true);
    storedDeflatedReadAheadAndFallback(archive, bytes);
    invalidArchivesReleaseSources(scratch.path, archive);
    storedEntryCrcStillRejectsCorruption(scratch.path / "corrupted.zip", bytes);
    if (argc == 2) combinedTesterArchive(argv[1]);
    std::puts("Buffered real-libzip read-ahead, pipeline FILE/hash roundtrips, ownership, fallback and CRC tests passed.");
}
