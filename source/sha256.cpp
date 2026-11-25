#include "sha256.hpp"
#include "file_hash_stream.hpp"
#include "io_buffer.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace unbound {
namespace {
constexpr std::uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
inline std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
}

Sha256::Sha256() {
    const std::uint32_t initial[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::memcpy(state_, initial, sizeof(state_));
}

void Sha256::transform(const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (std::uint32_t(block[i*4]) << 24) | (std::uint32_t(block[i*4+1]) << 16) |
               (std::uint32_t(block[i*4+2]) << 8) | block[i*4+3];
    for (unsigned i = 16; i < 64; ++i) {
        auto s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        auto s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    auto a=state_[0], b=state_[1], c=state_[2], d=state_[3];
    auto e=state_[4], f=state_[5], g=state_[6], h=state_[7];
    for (unsigned i = 0; i < 64; ++i) {
        auto s1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        auto ch = (e & f) ^ (~e & g);
        auto t1 = h + s1 + ch + K[i] + w[i];
        auto s0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        auto maj = (a & b) ^ (a & c) ^ (b & c);
        auto t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
}

void Sha256::update(const void* data, std::size_t length) {
    if (finished_) return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_ += length;
    if (used_ && length) {
        const auto take = (length < 64-used_) ? length : 64-used_;
        std::memcpy(buffer_+used_, bytes, take);
        used_ += take; bytes += take; length -= take;
        if (used_ < 64) return;
        transform(buffer_); used_ = 0;
    }
    // Transform complete input blocks in place; only partial blocks need a copy.
    while (length >= 64) {
        transform(bytes);
        bytes += 64; length -= 64;
    }
    if (length) {
        std::memcpy(buffer_, bytes, length);
        used_ = length;
    }
}

std::string Sha256::finish() {
    if (!finished_) {
        const auto bitLength = total_ * 8;
        buffer_[used_++] = 0x80;
        if (used_ > 56) { while (used_ < 64) buffer_[used_++] = 0; transform(buffer_); used_ = 0; }
        while (used_ < 56) buffer_[used_++] = 0;
        for (int i = 7; i >= 0; --i) buffer_[used_++] = std::uint8_t(bitLength >> (i*8));
        transform(buffer_); finished_ = true;
    }
    static constexpr char Hex[] = "0123456789abcdef";
    std::string out(64, '0');
    for (unsigned i = 0; i < 8; ++i) for (unsigned j = 0; j < 4; ++j) {
        auto byte = std::uint8_t(state_[i] >> (24-j*8));
        out[(i*4+j)*2] = Hex[byte >> 4]; out[(i*4+j)*2+1] = Hex[byte & 15];
    }
    return out;
}

namespace {
struct StdioHashReader {
    FILE* file;
    file_hash_detail::ReadResult read(void* buffer, std::size_t bytes) {
        errno = 0;
        const auto count = std::fread(buffer, 1, bytes, file);
        const int error = std::ferror(file) ? (errno ? errno : EIO) : 0;
        return {count, error, std::feof(file) != 0};
    }
    file_hash_detail::SizeResult size() {
        struct stat info{};
        errno = 0;
        if (fstat(fileno(file), &info) != 0) return {0, errno ? errno : EIO, false};
        if (info.st_size < 0) return {0, EOVERFLOW, false};
        return {static_cast<std::uint64_t>(info.st_size), 0, S_ISREG(info.st_mode) != 0};
    }
    int close() {
        errno = 0;
        const int result = std::fclose(file);
        file = nullptr;
        return result == 0 ? 0 : (errno ? errno : EIO);
    }
};
}

FileHashResult sha256FileDetailed(const std::string& path, std::string& output,
                                  const FileHashOptions& options) {
    if (options.cancelled && options.cancelled())
        return {FileHashStatus::Cancelled, ECANCELED, 0, options.expectedBytes};
    struct stat initial{};
    errno = 0;
    if (lstat(path.c_str(), &initial) != 0)
        return {FileHashStatus::StatFailed, errno ? errno : EIO, 0, options.expectedBytes};
    if (!S_ISREG(initial.st_mode))
        return {FileHashStatus::NotRegular, EINVAL, 0, options.expectedBytes};
    if (initial.st_size < 0)
        return {FileHashStatus::SizeMismatch, EOVERFLOW, 0, options.expectedBytes};
    const auto total = static_cast<std::uint64_t>(initial.st_size);
    errno = 0;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return {FileHashStatus::OpenFailed, errno ? errno : EIO, 0, total};
    StdioHashReader reader{file};
    const auto fail = [&](FileHashStatus status, int error) {
        reader.close();
        return FileHashResult{status, error, 0, total};
    };
    const auto opened = reader.size();
    if (opened.error) return fail(FileHashStatus::StatFailed, opened.error);
    if (!opened.regular) return fail(FileHashStatus::NotRegular, EINVAL);
    if (opened.bytes != total) return fail(FileHashStatus::SizeChanged, EIO);
    AlignedIoBuffer transfer;
    AlignedIoBuffer input;
    if (!transfer || !input) return fail(FileHashStatus::BufferUnavailable, ENOMEM);
    errno = 0;
    if (std::setvbuf(file, input.data(), _IOFBF, input.size()) != 0)
        return fail(FileHashStatus::BufferSetupFailed, errno ? errno : EIO);
    return file_hash_detail::hashStream(reader, transfer.data(), transfer.size(),
                                        total, options, output);
}

bool sha256File(const std::string& path, std::string& output) {
    return sha256FileDetailed(path, output).ok();
}

} // namespace unbound
