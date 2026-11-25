#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

namespace unbound {

struct ResumeRecord {
    // SHA-256 of the request identity, not a URL or account identifier.
    std::string requestKey;
    std::string prefixSha256;
    unsigned long long bytes = 0;
    unsigned long long totalBytes = 0;
};

namespace resume_detail {
inline bool uint64(std::string_view value, unsigned long long& result) {
    if (value.empty()) return false;
    unsigned long long parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return false;
        const auto digit = static_cast<unsigned>(ch - '0');
        if (parsed > (std::numeric_limits<unsigned long long>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    result = parsed;
    return true;
}
inline bool digest(std::string_view value) {
    if (value.size() != 64) return false;
    for (const char ch : value)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) return false;
    return true;
}
inline char lower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}
inline bool equalCaseInsensitive(std::string_view first, std::string_view second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index)
        if (lower(first[index]) != lower(second[index])) return false;
    return true;
}
inline std::string_view trimOws(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    return value;
}
} // namespace resume_detail

inline bool validResumeRecord(const ResumeRecord& record, unsigned long long maximum) {
    return resume_detail::digest(record.requestKey) &&
           resume_detail::digest(record.prefixSha256) && record.bytes > 0 &&
           record.bytes <= maximum && record.totalBytes <= maximum &&
           (!record.totalBytes || record.bytes <= record.totalBytes);
}

// Exactly five lines, with an optional final newline. No comments, additional
// fields, URLs, or permissive numeric conversions are accepted.
inline bool parseResumeRecord(std::string_view text, unsigned long long maximum,
                              ResumeRecord& output) {
    if (text.empty() || text.size() > 1024) return false;
    std::array<std::string_view, 5> lines{};
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto newline = text.find('\n');
        if (index + 1 < lines.size() && newline == std::string_view::npos) return false;
        lines[index] = text.substr(0, newline);
        if (!lines[index].empty() && lines[index].back() == '\r')
            lines[index].remove_suffix(1);
        if (newline == std::string_view::npos) text = {};
        else text.remove_prefix(newline + 1);
    }
    if (!text.empty() || lines[0] != "HQRESUME1") return false;
    ResumeRecord parsed;
    parsed.requestKey = std::string(lines[1]);
    parsed.prefixSha256 = std::string(lines[2]);
    if (!resume_detail::uint64(lines[3], parsed.bytes) ||
        !resume_detail::uint64(lines[4], parsed.totalBytes) ||
        !validResumeRecord(parsed, maximum)) return false;
    output = parsed;
    return true;
}

inline std::string serializeResumeRecord(
    const ResumeRecord& record,
    unsigned long long maximum = std::numeric_limits<unsigned long long>::max()) {
    if (!validResumeRecord(record, maximum)) return {};
    return "HQRESUME1\n" + record.requestKey + "\n" + record.prefixSha256 + "\n" +
           std::to_string(record.bytes) + "\n" + std::to_string(record.totalBytes) + "\n";
}

// A range request must never append a normal 200 response to an existing file.
// Validate the final response's headers before the write callback is permitted.
// The integration must additionally check its final byte count and SHA-256.
class ResumeResponse {
public:
    ResumeResponse(unsigned long long offset, unsigned long long maximum,
                   unsigned long long expectedTotal = 0)
        : offset_(offset), maximum_(maximum), expectedTotal_(expectedTotal) {
        reset();
    }

    bool consume(std::string_view fullHeaderLine) {
        const bool statusLine = fullHeaderLine.size() >= 5 &&
                                fullHeaderLine.substr(0, 5) == "HTTP/";
        // libcurl can report trailing headers after a valid body. They cannot
        // revise the byte range that was checked before any data was written.
        if (finished_ && !statusLine) return !failed_;
        if (failed_) return false;
        if (fullHeaderLine.size() > MaxLineBytes ||
            fullHeaderLine.size() > MaxHeaderBytes - headerBytes_) return fail();
        headerBytes_ += fullHeaderLine.size();
        if (!fullHeaderLine.empty() && fullHeaderLine.back() == '\n') {
            fullHeaderLine.remove_suffix(1);
            if (!fullHeaderLine.empty() && fullHeaderLine.back() == '\r')
                fullHeaderLine.remove_suffix(1);
        }
        for (const char ch : fullHeaderLine)
            if ((static_cast<unsigned char>(ch) < 32 && ch != '\t') || ch == 127)
                return fail();
        if (statusLine) return beginResponse(fullHeaderLine);
        if (!inHeaders_) return fail();
        if (fullHeaderLine.empty()) return completeHeaders();
        // Redirect and informational entities are not the file being resumed.
        if (statusCode_ != 206) return true;
        const auto colon = fullHeaderLine.find(':');
        if (colon == std::string_view::npos || colon == 0 ||
            fullHeaderLine.front() == ' ' || fullHeaderLine.front() == '\t')
            return fail();
        const auto name = fullHeaderLine.substr(0, colon);
        for (const char ch : name) {
            const bool token = (ch >= '0' && ch <= '9') ||
                               (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                               std::string_view("!#$%&'*+-.^_`|~").find(ch) != std::string_view::npos;
            if (!token) return fail();
        }
        const auto value = resume_detail::trimOws(fullHeaderLine.substr(colon + 1));
        if (resume_detail::equalCaseInsensitive(name, "Content-Range")) {
            if (rangeSeen_ || !parseRange(value)) return fail();
            rangeSeen_ = true;
        } else if (resume_detail::equalCaseInsensitive(name, "Content-Length")) {
            if (lengthSeen_ || !resume_detail::uint64(value, contentLength_)) return fail();
            lengthSeen_ = true;
        } else if (resume_detail::equalCaseInsensitive(name, "Content-Encoding")) {
            if (encodingSeen_ || !resume_detail::equalCaseInsensitive(value, "identity"))
                return fail();
            encodingSeen_ = true;
        }
        return true;
    }

    bool bodyAllowed() const { return bodyAllowed_ && !failed_; }
    bool restartRequired() const { return restartRequired_; }
    unsigned long long totalBytes() const { return totalBytes_; }
    unsigned statusCode() const { return statusCode_; }

    // Reset response state only; the offset and request limits remain unchanged.
    void reset() {
        statusCode_ = 0;
        headerBytes_ = 0;
        totalBytes_ = 0;
        spanBytes_ = 0;
        contentLength_ = 0;
        inHeaders_ = finished_ = rangeSeen_ = lengthSeen_ = encodingSeen_ = false;
        bodyAllowed_ = restartRequired_ = false;
        failed_ = !maximum_ || offset_ >= maximum_ ||
                  (expectedTotal_ && (expectedTotal_ <= offset_ || expectedTotal_ > maximum_));
    }

private:
    static constexpr std::size_t MaxLineBytes = 8 * 1024;
    static constexpr std::size_t MaxHeaderBytes = 64 * 1024;

    bool fail() {
        failed_ = true;
        bodyAllowed_ = false;
        return false;
    }
    bool beginResponse(std::string_view line) {
        const auto space = line.find(' ');
        if (space == std::string_view::npos || space <= 5) return fail();
        const auto version = line.substr(5, space - 5);
        bool dotSeen = false;
        for (std::size_t index = 0; index < version.size(); ++index) {
            const char ch = version[index];
            if (ch == '.' && !dotSeen && index > 0 && index + 1 < version.size())
                dotSeen = true;
            else if (ch < '0' || ch > '9') return fail();
        }
        line.remove_prefix(space + 1);
        if (line.size() < 3 || (line.size() > 3 && line[3] != ' ')) return fail();
        unsigned long long status = 0;
        if (!resume_detail::uint64(line.substr(0, 3), status) || status < 100 || status > 599)
            return fail();
        statusCode_ = static_cast<unsigned>(status);
        totalBytes_ = spanBytes_ = contentLength_ = 0;
        rangeSeen_ = lengthSeen_ = encodingSeen_ = finished_ = bodyAllowed_ = false;
        inHeaders_ = true;
        if (statusCode_ == 200 || statusCode_ == 416) {
            restartRequired_ = true;
            return fail();
        }
        return true;
    }
    bool parseRange(std::string_view value) {
        if (value.size() < 6 || value.substr(0, 6) != "bytes ") return false;
        value.remove_prefix(6);
        const auto dash = value.find('-');
        const auto slash = value.find('/');
        if (dash == std::string_view::npos || slash == std::string_view::npos || dash >= slash)
            return false;
        unsigned long long start = 0, end = 0, total = 0;
        if (!resume_detail::uint64(value.substr(0, dash), start) ||
            !resume_detail::uint64(value.substr(dash + 1, slash - dash - 1), end) ||
            !resume_detail::uint64(value.substr(slash + 1), total) || start != offset_ ||
            end < start || !total || end >= total || total > maximum_ ||
            (expectedTotal_ && total != expectedTotal_)) return false;
        totalBytes_ = total;
        spanBytes_ = end - start + 1;
        return true;
    }
    bool completeHeaders() {
        inHeaders_ = false;
        finished_ = true;
        if (statusCode_ == 206) {
            if (!rangeSeen_ || (lengthSeen_ && contentLength_ != spanBytes_)) return fail();
            bodyAllowed_ = true;
        }
        return true;
    }

    const unsigned long long offset_, maximum_, expectedTotal_;
    std::size_t headerBytes_ = 0;
    unsigned statusCode_ = 0;
    unsigned long long totalBytes_ = 0, spanBytes_ = 0, contentLength_ = 0;
    bool inHeaders_ = false, finished_ = false;
    bool rangeSeen_ = false, lengthSeen_ = false, encodingSeen_ = false;
    bool bodyAllowed_ = false, restartRequired_ = false, failed_ = false;
};

} // namespace unbound
