#pragma once

#include "io_buffer.hpp"

#include <cstdio>
#include <zip.h>

namespace unbound {

// libzip's path-based source otherwise uses the stdio default (32 KiB on
// Wii U). Keep a larger aligned read-ahead buffer alive for the complete
// archive lifetime; compressed entries often request much smaller blocks.
class BufferedZipReader {
public:
    explicit BufferedZipReader(std::size_t bufferBytes = 1024 * 1024) noexcept
        : input_(bufferBytes) {}

    ~BufferedZipReader() { discard(); }
    BufferedZipReader(const BufferedZipReader&) = delete;
    BufferedZipReader& operator=(const BufferedZipReader&) = delete;

    bool open(const char* path) noexcept {
        discard();
        if (!input_) return openDefault(path);

        FILE* stream = std::fopen(path, "rb");
        if (!stream) return false;
        if (std::setvbuf(stream, input_.data(), _IOFBF, input_.size()) != 0) {
            std::fclose(stream);
            // Buffer configuration is an optimization, not an install
            // prerequisite. Preserve the proven source on low-memory systems.
            return openDefault(path);
        }

        zip_error_t error;
        zip_error_init(&error);
        zip_source_t* source = zip_source_filep_create(stream, 0, -1, &error);
        if (!source) {
            // Only a successfully created source assumes FILE ownership.
            std::fclose(stream);
            zip_error_fini(&error);
            return false;
        }
        archive_ = zip_open_from_source(source, ZIP_RDONLY, &error);
        if (!archive_) {
            // open_from_source assumes source ownership only on success.
            // Freeing the source also closes stream, before input_ can die.
            zip_source_free(source);
        }
        zip_error_fini(&error);
        if (archive_) bufferedBytes_ = input_.size();
        return archive_ != nullptr;
    }

    zip_t* get() const noexcept { return archive_; }
    std::size_t bufferedBytes() const noexcept { return bufferedBytes_; }

    bool close() noexcept {
        if (!archive_) return true;
        if (zip_close(archive_) == 0) {
            archive_ = nullptr;
            return true;
        }
        // libzip retains its archive/source after a failed close. No writes
        // are permitted, so discard it to close FILE and release every buffer.
        discard();
        return false;
    }

private:
    bool openDefault(const char* path) noexcept {
        int error = 0;
        archive_ = zip_open(path, ZIP_RDONLY, &error);
        return archive_ != nullptr;
    }

    void discard() noexcept {
        if (archive_) zip_discard(archive_);
        archive_ = nullptr;
        bufferedBytes_ = 0;
    }

    AlignedIoBuffer input_;
    zip_t* archive_ = nullptr;
    std::size_t bufferedBytes_ = 0;
};

} // namespace unbound
