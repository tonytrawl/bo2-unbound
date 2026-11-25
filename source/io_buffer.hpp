#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace unbound {

// Heap-backed transfer memory avoids large stack frames and lets the Wii U
// filesystem use aligned blocks. If memory is tight, retain a smaller buffer.
class AlignedIoBuffer {
public:
    explicit AlignedIoBuffer(std::size_t requestedBytes = 256 * 1024) noexcept {
        constexpr std::size_t Alignment = 64;
        constexpr std::size_t MinimumFallback = 16 * 1024;
        if (!requestedBytes ||
            requestedBytes > std::numeric_limits<std::size_t>::max() - (Alignment - 1))
            return;
        for (std::size_t bytes = requestedBytes;;
             bytes = std::max(bytes / 2, MinimumFallback)) {
            allocation_ = std::malloc(bytes + Alignment - 1);
            if (allocation_) {
                const auto address = reinterpret_cast<std::uintptr_t>(allocation_);
                data_ = reinterpret_cast<char*>((address + Alignment - 1) & ~(Alignment - 1));
                size_ = bytes;
                return;
            }
            if (bytes <= MinimumFallback) return;
        }
    }

    ~AlignedIoBuffer() { std::free(allocation_); }
    AlignedIoBuffer(const AlignedIoBuffer&) = delete;
    AlignedIoBuffer& operator=(const AlignedIoBuffer&) = delete;

    explicit operator bool() const noexcept { return data_ != nullptr; }
    char* data() noexcept { return data_; }
    const char* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }

private:
    void* allocation_ = nullptr;
    char* data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace unbound
