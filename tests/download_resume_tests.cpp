#include "download_resume.hpp"

#include <cassert>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>

namespace {
using unbound::ResumeRecord;
using unbound::ResumeResponse;
constexpr unsigned long long Maximum = 1000000;
const std::string Key(64, 'a');
const std::string Prefix(64, 'b');

void records() {
    const ResumeRecord record{Key, Prefix, 100, 1000};
    const auto encoded = unbound::serializeResumeRecord(record, Maximum);
    assert(encoded == "HQRESUME1\n" + Key + "\n" + Prefix + "\n100\n1000\n");
    ResumeRecord parsed;
    assert(unbound::parseResumeRecord(encoded, Maximum, parsed));
    assert(parsed.requestKey == Key && parsed.prefixSha256 == Prefix);
    assert(parsed.bytes == 100 && parsed.totalBytes == 1000);
    assert(unbound::parseResumeRecord(encoded.substr(0, encoded.size() - 1), Maximum, parsed));
    const auto unchanged = parsed;
    auto invalid = [&](std::string text, unsigned long long maximum = Maximum) {
        assert(!unbound::parseResumeRecord(text, maximum, parsed));
        assert(parsed.requestKey == unchanged.requestKey);
        assert(parsed.prefixSha256 == unchanged.prefixSha256);
        assert(parsed.bytes == unchanged.bytes && parsed.totalBytes == unchanged.totalBytes);
    };
    invalid("");
    invalid(std::string(1025, 'a'));
    invalid("HQRESUME2\n" + Key + "\n" + Prefix + "\n100\n1000\n");
    invalid(encoded + "\n");
    invalid(encoded + "extra\n");
    invalid("HQRESUME1\n" + Key + "\n" + Prefix + "\n100\n");
    invalid("HQRESUME1\n" + Key + "\n" + Prefix + "\n100\n1000\n1000\n");
    invalid("HQRESUME1\nhttps://example.com\n" + Prefix + "\n100\n1000\n");
    invalid("HQRESUME1\n" + std::string(64, 'g') + "\n" + Prefix + "\n100\n1000\n");
    invalid("HQRESUME1\n" + Key + "\n" + std::string(63, 'b') + "\n100\n1000\n");
    for (const std::string number : {"", "-1", "+1", " 100", "100 ", "1e3", "100x",
                                    "18446744073709551616"}) {
        invalid("HQRESUME1\n" + Key + "\n" + Prefix + "\n" + number + "\n1000\n");
        invalid("HQRESUME1\n" + Key + "\n" + Prefix + "\n100\n" + number + "\n");
    }
    invalid("HQRESUME1\n" + Key + "\n" + Prefix + "\n0\n1000\n");
    invalid("HQRESUME1\n" + Key + "\n" + Prefix + "\n1001\n1000\n");
    invalid(encoded, 99);
    invalid(encoded, 999);
    assert(unbound::serializeResumeRecord({Key, Prefix, 0, 1000}).empty());
    assert(unbound::serializeResumeRecord(record, 999).empty());
    assert(unbound::parseResumeRecord("HQRESUME1\n" + Key + "\n" + Prefix + "\n100\n0\n",
                                     Maximum, parsed));
    assert(parsed.totalBytes == 0);
    assert(unbound::parseResumeRecord("HQRESUME1\r\n" + Key + "\r\n" + Prefix +
                                     "\r\n100\r\n1000\r\n", Maximum, parsed));
    assert(unbound::parseResumeRecord("HQRESUME1\n" + std::string(64, 'A') + "\n" + Prefix +
                                     "\n100\n1000\n", Maximum, parsed));
    // Request and prefix mismatch are intentionally checked by the integration:
    // a structurally valid record must retain, not silently replace, its identity.
    assert(parsed.requestKey != Key);
    assert(parsed.prefixSha256 != std::string(64, 'c'));
}

void statusAndRange(ResumeResponse& response, std::string_view range = "bytes 100-999/1000") {
    assert(response.consume("HTTP/1.1 206 Partial Content\r\n"));
    assert(response.statusCode() == 206 && !response.bodyAllowed());
    assert(response.consume("Content-Range: " + std::string(range) + "\r\n"));
    assert(!response.bodyAllowed());
}

void validResponses() {
    ResumeResponse response(100, Maximum, 1000);
    statusAndRange(response);
    assert(response.consume("content-length:\t900 \r\n"));
    assert(response.consume("CONTENT-ENCODING: Identity\r\n"));
    assert(response.consume("X-Other: yes\r\n"));
    assert(response.consume("\r\n"));
    assert(response.bodyAllowed() && !response.restartRequired());
    assert(response.totalBytes() == 1000);
    // A trailer cannot invalidate headers after the body has been accepted.
    assert(response.consume("Content-Range: deliberately-invalid-trailer\r\n"));
    assert(response.consume("Content-Encoding: gzip\r\n"));
    assert(response.consume(std::string(9000, 'x')));
    assert(response.consume("\r\n"));
    assert(response.bodyAllowed());
    // A new status line, unlike a trailer, must revalidate a new response.
    assert(response.consume("HTTP/2 206\r\n"));
    assert(!response.bodyAllowed());
    assert(response.consume("Content-Range: bytes 100-999/1000\n"));
    assert(response.consume("\n"));
    assert(response.bodyAllowed());
    response.reset();
    assert(!response.bodyAllowed() && response.statusCode() == 0 && response.totalBytes() == 0);
    statusAndRange(response);
    assert(response.consume("\r\n"));
    assert(response.bodyAllowed());
    ResumeResponse unknownTotal(100, Maximum);
    statusAndRange(unknownTotal);
    assert(unknownTotal.consume("\r\n"));
    assert(unknownTotal.bodyAllowed() && unknownTotal.totalBytes() == 1000);
    // Some servers legally return a shorter range. Root integration requires
    // the entire file's final length before committing a successful download.
    ResumeResponse shortRange(100, Maximum);
    statusAndRange(shortRange, "bytes 100-199/1000");
    assert(shortRange.consume("Content-Length: 100\r\n"));
    assert(shortRange.consume("\r\n"));
    assert(shortRange.bodyAllowed());
}

void redirectAndInformationalResponses() {
    ResumeResponse response(100, Maximum, 1000);
    assert(response.consume("HTTP/1.1 302 Found\r\n"));
    assert(response.consume("Location: https://example.com/file\r\n"));
    assert(response.consume("Content-Length: 20\r\n"));
    assert(response.consume("\r\n"));
    assert(!response.bodyAllowed() && !response.restartRequired());
    assert(response.consume("HTTP/1.1 100 Continue\r\n"));
    assert(response.consume("\r\n"));
    assert(!response.bodyAllowed());
    assert(response.consume("HTTP/1.1 103 Early Hints\r\n"));
    assert(response.consume("Link: <thing>; rel=preload\r\n"));
    assert(response.consume("\r\n"));
    assert(!response.bodyAllowed());
    statusAndRange(response);
    assert(response.consume("Content-Length: 900\r\n"));
    assert(response.consume("\r\n"));
    assert(response.bodyAllowed());
    for (const unsigned code : {400U, 401U, 403U, 404U, 429U, 500U, 503U}) {
        response.reset();
        assert(response.consume("HTTP/1.1 " + std::to_string(code) + " Error\r\n"));
        assert(response.consume("Content-Length: 90\r\n"));
        assert(response.consume("\r\n"));
        assert(response.statusCode() == code && !response.bodyAllowed() && !response.restartRequired());
    }
    for (const std::string code : {"200 OK", "416 Range Not Satisfiable"}) {
        response.reset();
        assert(!response.consume("HTTP/1.1 " + code + "\r\n"));
        assert(response.restartRequired() && !response.bodyAllowed());
    }
}

void malformedResponses() {
    for (const std::string range : {
             "", "bytes */1000", "items 100-999/1000", "bytes 99-999/1000", "bytes 101-999/1000",
             "bytes 100-99/1000", "bytes 100-1000/1000", "bytes 100-999/0", "bytes 100-999/*",
             "bytes 100-999/1000001", "bytes 100-999/999", "bytes 100-999/1001",
             "bytes 100-999/18446744073709551616", "bytes 18446744073709551616-999/1000",
             "bytes 100-18446744073709551616/1000", "bytes 100-999/1000, 100-999/1000",
             "bytes 100 -999/1000", "bytes +100-999/1000", "bytes -100-999/1000", "bytes 100-999//1000"}) {
        ResumeResponse response(100, Maximum, 1000);
        assert(response.consume("HTTP/1.1 206 Partial Content\r\n"));
        assert(!response.consume("Content-Range: " + range + "\r\n"));
        assert(!response.bodyAllowed() && !response.restartRequired());
        assert(!response.consume("\r\n"));
    }
    {
        ResumeResponse response(100, Maximum);
        assert(response.consume("HTTP/1.1 206 Partial Content\r\n"));
        assert(!response.consume("\r\n"));
        assert(!response.bodyAllowed());
    }
    for (const std::string value : {"0", "899", "901", "1000"}) {
        ResumeResponse response(100, Maximum);
        statusAndRange(response);
        assert(response.consume("Content-Length: " + value + "\r\n"));
        assert(!response.consume("\r\n"));
    }
    for (const std::string value : {"", "-900", "+900", "90x", "18446744073709551616"}) {
        ResumeResponse response(100, Maximum);
        statusAndRange(response);
        assert(!response.consume("Content-Length: " + value + "\r\n"));
    }
    for (const std::string encoding : {"", "gzip", "deflate", "br", "identity, gzip"}) {
        ResumeResponse response(100, Maximum);
        statusAndRange(response);
        assert(!response.consume("Content-Encoding: " + encoding + "\r\n"));
    }
    for (const std::string duplicate : {"Content-Range: bytes 100-999/1000\r\n",
                                       "Content-Length: 900\r\n", "Content-Encoding: identity\r\n"}) {
        ResumeResponse response(100, Maximum);
        statusAndRange(response);
        if (duplicate.find("Content-Range") != 0) assert(response.consume(duplicate));
        assert(!response.consume(duplicate));
        assert(!response.bodyAllowed());
    }
    for (const std::string invalid : {"HTTP/1.1 20 OK\r\n", "HTTP/1.1 206x\r\n", "HTTP/1.1 abc OK\r\n",
                                      "HTTP/1.1 099\r\n", "HTTP/1.1 600\r\n", "HTTP/x 206\r\n",
                                      "HTTP/.1 206\r\n", "HTTP/1. 206\r\n", "HTTP/1..1 206\r\n"}) {
        ResumeResponse response(100, Maximum);
        assert(!response.consume(invalid));
    }
    for (const std::string invalid : {"missing-colon\r\n", ": value\r\n",
                                      " Content-Range: bytes 100-999/1000\r\n",
                                      "Content-Range : bytes 100-999/1000\r\n",
                                      "Content[Range]: bytes 100-999/1000\r\n",
                                      "Content-Range: bytes 100-999/1000\r\nInjected: x\r\n"}) {
        ResumeResponse response(100, Maximum);
        assert(response.consume("HTTP/1.1 206\r\n"));
        assert(!response.consume(invalid));
    }
    {
        ResumeResponse response(100, Maximum);
        assert(!response.consume("\r\n"));
        response.reset();
        assert(response.consume("HTTP/1.1 206\r\n"));
        assert(!response.consume(std::string(8193, 'x')));
        response.reset();
        assert(response.consume("HTTP/1.1 206\r\n"));
        const std::string largeHeader = "X-Large: " + std::string(7990, 'x') + "\r\n";
        for (unsigned index = 0; index < 8; ++index) assert(response.consume(largeHeader));
        assert(!response.consume(largeHeader));
    }
    {
        const auto limit = std::numeric_limits<unsigned long long>::max();
        ResumeResponse response(limit - 1, limit);
        statusAndRange(response, "bytes 18446744073709551614-18446744073709551614/18446744073709551615");
        assert(response.consume("Content-Length: 1\r\n"));
        assert(response.consume("\r\n"));
        assert(response.bodyAllowed());
    }
    for (const auto& params : {ResumeRecord{"", "", 100, 100}, ResumeRecord{"", "", 100, Maximum + 1}}) {
        ResumeResponse response(params.bytes, Maximum, params.totalBytes);
        assert(!response.consume("HTTP/1.1 206\r\n"));
    }
    ResumeResponse invalidOffset(Maximum, Maximum);
    assert(!invalidOffset.consume("HTTP/1.1 206\r\n"));
    ResumeResponse zeroMaximum(0, 0);
    assert(!zeroMaximum.consume("HTTP/1.1 206\r\n"));
}
} // namespace

int main() {
    records();
    validResponses();
    redirectAndInformationalResponses();
    malformedResponses();
    std::puts("Download resume policy tests passed");
}
