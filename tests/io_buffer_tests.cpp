#include "io_buffer.hpp"
#include "sha256.hpp"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <limits>

int main() {
    using unbound::AlignedIoBuffer;
    for (const std::size_t requested : {1U, 8192U, 16384U, 65536U, 262144U}) {
        AlignedIoBuffer buffer(requested);
        assert(buffer);
        assert(buffer.size() <= requested);
        assert(buffer.size() > 0);
        assert(reinterpret_cast<std::uintptr_t>(buffer.data()) % 64 == 0);
        buffer.data()[0] = 'a';
        buffer.data()[buffer.size() - 1] = 'z';
    }
    AlignedIoBuffer empty(0);
    assert(!empty && empty.size() == 0);
    AlignedIoBuffer overflow(std::numeric_limits<std::size_t>::max());
    assert(!overflow && overflow.size() == 0);

    AlignedIoBuffer transfer;
    AlignedIoBuffer fileBuffer;
    assert(transfer && fileBuffer);
    FILE* file = std::tmpfile();
    assert(file);
    assert(std::setvbuf(file, fileBuffer.data(), _IOFBF, fileBuffer.size()) == 0);

    // Exercise repeated full aligned blocks and a final partial block, then
    // verify all bytes through the same FILE buffering used by the installer.
    constexpr std::size_t TotalBytes = 3 * 1024 * 1024 + 37;
    unbound::Sha256 writtenHash;
    for (std::size_t completed = 0; completed < TotalBytes;) {
        const auto bytes = std::min(transfer.size(), TotalBytes - completed);
        for (std::size_t index = 0; index < bytes; ++index)
            transfer.data()[index] = static_cast<char>((completed + index) * 131U);
        assert(std::fwrite(transfer.data(), 1, bytes, file) == bytes);
        writtenHash.update(transfer.data(), bytes);
        completed += bytes;
    }
    assert(std::fflush(file) == 0);
    assert(std::fseek(file, 0, SEEK_SET) == 0);
    unbound::Sha256 readHash;
    std::size_t readBytes = 0;
    while (const auto bytes = std::fread(transfer.data(), 1, transfer.size(), file)) {
        readHash.update(transfer.data(), bytes);
        readBytes += bytes;
    }
    assert(!std::ferror(file));
    assert(std::fclose(file) == 0);
    assert(readBytes == TotalBytes);
    assert(readHash.finish() == writtenHash.finish());
    std::puts("Aligned IO buffer and binary buffered IO tests passed.");
}
