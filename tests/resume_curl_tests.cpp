#include "download_pipe.hpp"
#include "download_resume.hpp"
#include "download_control.hpp"
#include "download_size.hpp"
#include "sha256.hpp"

#include <curl/curl.h>

// Linux host-only integration test. Example for the existing build container:
// c++ -std=c++20 -Wall -Wextra -Werror -pthread -Itests/fake_sdl -Iinclude
//     -Isource -I/opt/devkitpro/portlibs/wiiu/include tests/resume_curl_tests.cpp
//     source/sha256.cpp -l:libcurl.so.4 -o /tmp/resume_curl_tests
// timeout 15 /tmp/resume_curl_tests
// For sanitizers add -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie
// and use ASAN_OPTIONS=handle_segv=0:abort_on_error=1:halt_on_error=1 with timeout.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t BlockBytes = 256 * 1024;

std::string makePayload() {
    std::string result(2 * 1024 * 1024 + 37, '\0');
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<char>((index * 131U + index / 257U) & 255U);
    return result;
}

std::string digest(std::string_view value) {
    unbound::Sha256 hash;
    hash.update(value.data(), value.size());
    return hash.finish();
}

bool sendAll(int connection, std::string_view value, bool pace = false) {
    while (!value.empty()) {
        const auto count = send(connection, value.data(), std::min<std::size_t>(65536, value.size()), MSG_NOSIGNAL);
        if (count <= 0) return false;
        value.remove_prefix(static_cast<std::size_t>(count));
        if (pace) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// Loopback only: no external server, credentials, Wii U filesystem, or internet.
class Server {
public:
    explicit Server(const std::string& payload) : payload_(payload) {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(listener_ >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        assert(bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        socklen_t length = sizeof(address);
        assert(getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        port_ = ntohs(address.sin_port);
        assert(listen(listener_, 8) == 0);
        worker_ = std::thread([this] { serve(); });
    }
    ~Server() {
        stopping_ = true;
        (void)shutdown(listener_, SHUT_RDWR);
        (void)close(listener_);
        worker_.join();
    }
    std::string url(std::string_view route) const {
        return "http://127.0.0.1:" + std::to_string(port_) + std::string(route);
    }
    unsigned headRequests() const { return headRequests_.load(); }

private:
    void serve() {
        while (!stopping_) {
            const int connection = accept(listener_, nullptr, nullptr);
            if (connection < 0) break;
            timeval timeout{2, 0};
            (void)setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            (void)setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            respond(connection);
            (void)shutdown(connection, SHUT_RDWR);
            (void)close(connection);
        }
    }
    void respond(int connection) const {
        std::string request;
        char buffer[2048];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
            const auto count = recv(connection, buffer, sizeof(buffer), 0);
            if (count <= 0) return;
            request.append(buffer, static_cast<std::size_t>(count));
        }
        const auto firstSpace = request.find(' ');
        const auto secondSpace = request.find(' ', firstSpace + 1);
        if (firstSpace == std::string::npos || secondSpace == std::string::npos) return;
        const auto route = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        if (request.substr(0, firstSpace) == "HEAD") {
            ++headRequests_;
            (void)sendAll(connection, "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        const auto rangeAt = request.find("\r\nRange: bytes=");
        const bool ranged = rangeAt != std::string::npos;
        std::size_t offset = 0;
        if (ranged) {
            const auto begin = rangeAt + std::strlen("\r\nRange: bytes=");
            const auto end = request.find('-', begin);
            unsigned long long parsed = 0;
            if (end == std::string::npos ||
                !unbound::resume_detail::uint64(std::string_view(request).substr(begin, end - begin), parsed) ||
                parsed >= payload_.size()) return;
            offset = static_cast<std::size_t>(parsed);
        }
        if (route == "/redirect-empty" || route == "/redirect-body") {
            const std::string body = route == "/redirect-body" ? "This is a redirect body, not package data." : "";
            (void)sendAll(connection, "HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: " +
                                      std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            return;
        }
        if (route == "/416" && ranged) {
            (void)sendAll(connection, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 5\r\nConnection: close\r\n\r\nerror");
            return;
        }
        if (!ranged || route == "/ignore") {
            if (!sendAll(connection, "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(payload_.size()) +
                                    "\r\nConnection: close\r\n\r\n")) return;
            if (route == "/broken" && !ranged) {
                // A controlled disconnect after enough paced data for at least
                // one writer slot to be persisted. Unsealed/queued bytes can be
                // discarded exactly as in the application's failed transfer.
                (void)sendAll(connection, std::string_view(payload_).substr(0, 3 * BlockBytes + 73), true);
                return;
            }
            (void)sendAll(connection, payload_, route == "/cancel");
            return;
        }
        std::size_t start = offset;
        std::size_t end = payload_.size() - 1;
        std::size_t total = payload_.size();
        if (route == "/wrong-start") ++start;
        if (route == "/wrong-total") ++total;
        if (route == "/over-limit") total += 100000;
        if (route == "/short-range") end = offset + 99;
        const auto range = route == "/malformed" ? "bytes */" + std::to_string(total)
            : "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(total);
        std::string headers = "HTTP/1.1 206 Partial Content\r\nContent-Range: " + range + "\r\n";
        if (route == "/duplicate") headers += "Content-Range: " + range + "\r\n";
        if (route == "/gzip") headers += "Content-Encoding: gzip\r\n";
        if (route != "/no-length") headers += "Content-Length: " + std::to_string(end - offset + 1) + "\r\n";
        headers += "Connection: close\r\n\r\n";
        if (sendAll(connection, headers))
            (void)sendAll(connection, std::string_view(payload_).substr(offset, end - offset + 1));
    }

    const std::string& payload_;
    int listener_ = -1;
    unsigned port_ = 0;
    std::atomic<bool> stopping_{false};
    mutable std::atomic<unsigned> headRequests_{0};
    std::thread worker_;
};

struct Download {
    std::vector<char> bytes;
    unbound::Sha256 hash;
    unsigned long long accepted = 0;
    unsigned long long maximum = 0;
    bool limitExceeded = false;
    bool writeFailed = false;
    unbound::DownloadPipe* pipe = nullptr;
    unbound::ResumeResponse* response = nullptr;
    unbound::DownloadControl* control = nullptr;
    unsigned long long cancelAt = 0;
    bool cancelInBody = false;

    explicit Download(std::string_view prefix = {}) : bytes(prefix.begin(), prefix.end()), accepted(prefix.size()) {
        hash.update(prefix.data(), prefix.size());
    }
    static bool sink(void* opaque, const char* value, std::size_t count) {
        auto& download = *static_cast<Download*>(opaque);
        download.bytes.insert(download.bytes.end(), value, value + count);
        download.hash.update(value, count);
        return true;
    }
    static size_t body(char* data, size_t size, size_t count, void* opaque) {
        auto& download = *static_cast<Download*>(opaque);
        if (download.control && download.cancelInBody && download.cancelAt &&
            download.accepted >= download.cancelAt)
            download.control->requestCancel();
        if (unbound::downloadCancellationRequested(download.control)) return 0;
        if (download.response && !download.response->bodyAllowed()) return 0;
        std::size_t bytes = 0;
        if (!unbound::checkedDownloadChunk(size, count, download.accepted, download.maximum, bytes)) {
            download.limitExceeded = true;
            return 0;
        }
        if (!download.pipe->push(data, bytes)) return 0;
        download.accepted += bytes;
        return bytes;
    }
    static size_t header(char* data, size_t size, size_t count, void* opaque) {
        if (size && count > std::numeric_limits<std::size_t>::max() / size) return 0;
        const auto bytes = size * count;
        auto& response = *static_cast<unbound::ResumeResponse*>(opaque);
        return response.consume(std::string_view(data, bytes)) ? bytes : 0;
    }
    static int progress(void* opaque, curl_off_t, curl_off_t current, curl_off_t, curl_off_t) {
        auto& download = *static_cast<Download*>(opaque);
        if (download.control && !download.cancelInBody && download.cancelAt && current > 0 &&
            static_cast<unsigned long long>(current) >= download.cancelAt)
            download.control->requestCancel();
        return unbound::downloadCancellationRequested(download.control) ? 1 : 0;
    }
    std::string prefixHash() const {
        auto snapshot = hash;
        return snapshot.finish();
    }
    std::string contents() const { return std::string(bytes.begin(), bytes.end()); }
};

struct Result {
    CURLcode code = CURLE_OK;
    long status = 0;
    bool bodyAllowed = false;
    bool restartRequired = false;
    unsigned long long totalBytes = 0;
    bool cancelled = false;
};

Result perform(const std::string& url, Download& download, bool ranged,
               unsigned long long maximum, unsigned long long expectedTotal = 0,
               CURL* reusable = nullptr, unbound::DownloadControl* control = nullptr,
               unsigned long long cancelAt = 0, bool cancelInBody = false) {
    CURL* curl = reusable ? reusable : curl_easy_init();
    assert(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 128L * 1024L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Download::body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maximum));
    download.maximum = maximum;
    download.control = control;
    download.cancelAt = cancelAt;
    download.cancelInBody = cancelInBody;
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, control ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Download::progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &download);
    unbound::ResumeResponse response(download.bytes.size(), maximum, expectedTotal);
    if (ranged) {
        download.response = &response;
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(download.bytes.size()));
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Download::header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    } else {
        // The app reuses its easy handle when a server ignores Range or returns
        // 416. Clear both the range and header callback before that fresh retry.
        download.response = nullptr;
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(0));
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, nullptr);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr);
    }
    unbound::DownloadPipe pipe;
    assert(pipe.start(Download::sink, &download));
    download.pipe = &pipe;
    Result result;
    result.code = curl_easy_perform(curl);
    // Match the application: drain accepted bytes on a requested cancellation,
    // but do not confuse finish(false)'s intentional discard on network errors
    // with an actual SD sink failure.
    const bool drainAccepted = result.code == CURLE_OK ||
                               unbound::downloadCancellationRequested(control);
    const bool writerOk = pipe.finish(drainAccepted);
    if (drainAccepted && !writerOk) download.writeFailed = true;
    if (result.code == CURLE_OK && !writerOk) result.code = CURLE_WRITE_ERROR;
    download.pipe = nullptr;
    download.response = nullptr;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    result.bodyAllowed = response.bodyAllowed();
    result.restartRequired = response.restartRequired();
    result.totalBytes = response.totalBytes();
    if (unbound::downloadCancellationRequested(control) &&
        !download.writeFailed && !download.limitExceeded) {
        control->markCancelled();
        result.cancelled = control->cancelled();
    }
    if (!reusable) curl_easy_cleanup(curl);
    return result;
}

void verified(const Download& download, const std::string& payload) {
    assert(download.accepted == payload.size());
    assert(download.bytes.size() == payload.size());
    assert(download.contents() == payload);
    assert(download.prefixHash() == digest(payload));
}

// Exercise the production size-resolution helper against a real HTTP server
// that supports GET/Range but deliberately rejects HEAD. The commit gate below
// mirrors downloadFile's existing transport, drained-count, exact-size and hash
// checks; bypassing HEAD must not make any of them optional.
void manifestSizeBypassesUnsupportedHead(Server& server, const std::string& payload) {
    const auto maximum = payload.size() + 4096;
    const auto expectedHash = digest(payload);
    const auto resolve = [&](const std::string& url, unsigned long long declared) {
        return unbound::resolveDownloadSize(declared, maximum, true,
            [&](unsigned long long& detected) {
                CURL* head = curl_easy_init();
                assert(head);
                curl_easy_setopt(head, CURLOPT_URL, url.c_str());
                curl_easy_setopt(head, CURLOPT_PROXY, "");
                curl_easy_setopt(head, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(head, CURLOPT_TIMEOUT, 2L);
                curl_easy_setopt(head, CURLOPT_NOBODY, 1L);
                const auto result = curl_easy_perform(head);
                long status = 0;
                curl_off_t length = -1;
                curl_easy_getinfo(head, CURLINFO_RESPONSE_CODE, &status);
                curl_easy_getinfo(head, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
                curl_easy_cleanup(head);
                if (result != CURLE_OK || status < 200 || status >= 300 || length < 0)
                    return false;
                detected = static_cast<unsigned long long>(length);
                return true;
            });
    };
    const auto canCommit = [](const Result& result, const Download& download,
                              const unbound::DownloadSize& size, const std::string& hash) {
        return size.valid && result.code == CURLE_OK && result.status >= 200 &&
            result.status < 300 && !result.cancelled && !download.writeFailed &&
            !download.limitExceeded && download.bytes.size() == download.accepted &&
            (!size.known || download.bytes.size() == size.bytes) && download.prefixHash() == hash;
    };
    const auto headBefore = server.headRequests();
    for (const char* route : {"/ok", "/redirect-body"}) {
        const auto url = server.url(route);
        const auto size = resolve(url, payload.size());
        assert(size.valid && size.known && !size.probed && size.bytes == payload.size());
        Download download;
        const auto result = perform(url, download, false, maximum, size.bytes);
        assert(canCommit(result, download, size, expectedHash));
        verified(download, payload);
        assert(server.headRequests() == headBefore);
    }
    for (const auto declared : {payload.size() - 1, payload.size() + 1}) {
        const auto url = server.url("/ok");
        const auto size = resolve(url, declared);
        Download download;
        const auto result = perform(url, download, false, maximum, size.bytes);
        assert(result.code == CURLE_OK && result.status == 200);
        assert(download.prefixHash() == expectedHash); // Hash alone cannot excuse the wrong size.
        assert(!canCommit(result, download, size, expectedHash));
        assert(server.headRequests() == headBefore);
    }
    {
        const auto url = server.url("/ok");
        const auto size = resolve(url, payload.size());
        Download download;
        const auto result = perform(url, download, false, maximum, size.bytes);
        assert(result.code == CURLE_OK && download.bytes.size() == size.bytes);
        assert(!canCommit(result, download, size, digest(payload + "different release")));
        assert(server.headRequests() == headBefore);
    }
    {
        const auto url = server.url("/cancel");
        const auto size = resolve(url, payload.size());
        unbound::DownloadControl control;
        Download interrupted;
        const auto result = perform(url, interrupted, false, maximum, size.bytes, nullptr,
                                    &control, BlockBytes + 73, true);
        assert(result.cancelled && control.cancelled());
        assert(!canCommit(result, interrupted, size, expectedHash));
        assert(unbound::retainInterruptedDownload(true, true, interrupted.writeFailed,
            interrupted.limitExceeded, false, result.cancelled, interrupted.bytes.size(),
            result.status == 200, 0));
        const auto prefix = interrupted.contents();
        assert(prefix == std::string_view(payload).substr(0, prefix.size()));
        const unbound::ResumeRecord saved{digest(url + "\n" + expectedHash),
            interrupted.prefixHash(), prefix.size(), size.bytes};
        unbound::ResumeRecord reloaded;
        assert(unbound::parseResumeRecord(unbound::serializeResumeRecord(saved, maximum), maximum, reloaded));
        assert(reloaded.totalBytes == size.bytes && reloaded.prefixSha256 == digest(prefix));
        Download resumed(prefix);
        const auto completed = perform(url, resumed, true, maximum, reloaded.totalBytes);
        assert(completed.bodyAllowed && canCommit(completed, resumed, size, expectedHash));
        verified(resumed, payload);
        assert(server.headRequests() == headBefore);
    }
    {
        const auto url = server.url("/ok");
        const auto size = resolve(url, 0); // Old/unknown records still probe, then safely use GET.
        assert(size.valid && size.probed && !size.known && size.bytes == 0);
        assert(server.headRequests() == headBefore + 1);
        Download download;
        const auto result = perform(url, download, false, maximum);
        assert(canCommit(result, download, size, expectedHash));
        verified(download, payload);
    }
}

void validRangeAndRedirects(Server& server, const std::string& payload) {
    for (const char* route : {"/ok", "/redirect-empty", "/redirect-body", "/no-length"}) {
        Download download(std::string_view(payload).substr(0, BlockBytes + 19));
        const auto result = perform(server.url(route), download, true, payload.size() + 4096, payload.size());
        assert(result.code == CURLE_OK && result.status == 206 && result.bodyAllowed);
        assert(result.totalBytes == payload.size() && !result.restartRequired);
        verified(download, payload);
    }
    {
        CURL* head = curl_easy_init();
        assert(head);
        const auto url = server.url("/ok");
        curl_easy_setopt(head, CURLOPT_URL, url.c_str());
        curl_easy_setopt(head, CURLOPT_PROXY, "");
        curl_easy_setopt(head, CURLOPT_NOBODY, 1L);
        assert(curl_easy_perform(head) == CURLE_OK);
        long status = 0;
        curl_easy_getinfo(head, CURLINFO_RESPONSE_CODE, &status);
        assert(status == 405);
        curl_easy_cleanup(head);
        Download download(std::string_view(payload).substr(0, 173));
        const auto result = perform(url, download, true, payload.size() + 4096, 0);
        assert(result.code == CURLE_OK && result.bodyAllowed && result.totalBytes == payload.size());
        verified(download, payload);
    }
}

void safeFreshFallbacks(Server& server, const std::string& payload) {
    for (const char* route : {"/ignore", "/416"}) {
        CURL* reusable = curl_easy_init();
        assert(reusable);
        const auto prefix = std::string_view(payload).substr(0, BlockBytes + 73);
        Download resumed(prefix);
        const auto result = perform(server.url(route), resumed, true, payload.size() + 4096, payload.size(), reusable);
        assert(result.code != CURLE_OK && result.restartRequired && !result.bodyAllowed);
        assert(resumed.contents() == prefix && resumed.accepted == prefix.size());
        assert(resumed.prefixHash() == digest(prefix));
        // Equivalent to the root's join + truncate + reset counters/hash path.
        Download fresh;
        const auto restarted = perform(server.url(route), fresh, false, payload.size() + 4096, 0, reusable);
        assert(restarted.code == CURLE_OK && restarted.status == 200);
        verified(fresh, payload);
        curl_easy_cleanup(reusable);
    }
}

void interruptedThenResumed(Server& server, const std::string& payload) {
    Download interrupted;
    const auto result = perform(server.url("/broken"), interrupted, false, payload.size() + 4096);
    assert(result.code == CURLE_PARTIAL_FILE && result.status == 200);
    assert(!interrupted.writeFailed && !result.cancelled);
    assert(!interrupted.bytes.empty() && interrupted.bytes.size() < payload.size());
    assert(interrupted.contents() == std::string_view(payload).substr(0, interrupted.bytes.size()));
    assert(interrupted.prefixHash() == digest(interrupted.contents()));
    const auto prefix = interrupted.contents();
    const unbound::ResumeRecord record{std::string(64, 'a'), interrupted.prefixHash(), prefix.size(), payload.size()};
    const auto metadata = unbound::serializeResumeRecord(record, payload.size() + 4096);
    unbound::ResumeRecord reloaded;
    assert(unbound::parseResumeRecord(metadata, payload.size() + 4096, reloaded));
    assert(reloaded.bytes == prefix.size() && reloaded.prefixSha256 == digest(prefix));
    Download resumed(prefix);
    const auto completed = perform(server.url("/broken"), resumed, true, payload.size() + 4096, reloaded.totalBytes);
    assert(completed.code == CURLE_OK && completed.bodyAllowed);
    verified(resumed, payload);
}

void userCancelledThenResumed(Server& server, const std::string& payload) {
    for (const bool cancelInBody : {false, true}) {
        unbound::DownloadControl control;
        Download interrupted;
        const auto url = server.url("/cancel");
        const auto result = perform(url, interrupted, false, payload.size() + 4096,
                                    payload.size(), nullptr, &control,
                                    BlockBytes + 73, cancelInBody);
        assert(result.code == (cancelInBody ? CURLE_WRITE_ERROR : CURLE_ABORTED_BY_CALLBACK));
        assert(result.status == 200 && result.cancelled && control.cancelled());
        assert(!interrupted.writeFailed && !interrupted.limitExceeded);
        // The mandatory writer join sealed and drained the final partial block.
        // No incoming callback bytes may be counted without existing in the prefix.
        assert(interrupted.accepted == interrupted.bytes.size());
        assert(interrupted.bytes.size() >= BlockBytes + 73 && interrupted.bytes.size() < payload.size());
        const auto prefix = interrupted.contents();
        assert(prefix == std::string_view(payload).substr(0, prefix.size()));
        assert(interrupted.prefixHash() == digest(prefix));
        assert(unbound::retainInterruptedDownload(true, true, interrupted.writeFailed,
                   interrupted.limitExceeded, false, result.cancelled,
                   prefix.size(), result.status == 200, 0));

        const auto key = digest(url + "\n" + digest(payload));
        const unbound::ResumeRecord record{key, interrupted.prefixHash(), prefix.size(), payload.size()};
        const auto metadata = unbound::serializeResumeRecord(record, payload.size() + 4096);
        unbound::ResumeRecord reloaded;
        assert(unbound::parseResumeRecord(metadata, payload.size() + 4096, reloaded));
        assert(reloaded.requestKey == key && reloaded.bytes == interrupted.bytes.size());
        assert(reloaded.prefixSha256 == digest(prefix) && reloaded.totalBytes == payload.size());

        // A fresh control belongs to the resumed operation, not the cancelled one.
        unbound::DownloadControl retry;
        Download resumed(prefix);
        const auto completed = perform(url, resumed, true, payload.size() + 4096,
                                       reloaded.totalBytes, nullptr, &retry);
        assert(completed.code == CURLE_OK && completed.status == 206 && completed.bodyAllowed);
        assert(!completed.cancelled && !retry.cancelRequested() && !resumed.writeFailed);
        verified(resumed, payload);
    }
}

void rejectsBeforeWriting(Server& server, const std::string& payload) {
    for (const char* route : {"/wrong-start", "/wrong-total", "/over-limit", "/malformed", "/duplicate", "/gzip"}) {
        const auto prefix = std::string_view(payload).substr(0, 211);
        Download download(prefix);
        const auto result = perform(server.url(route), download, true, payload.size() + 4096, payload.size());
        assert(result.code != CURLE_OK && !result.bodyAllowed && !result.restartRequired);
        assert(download.contents() == prefix && download.accepted == prefix.size());
        assert(download.prefixHash() == digest(prefix));
    }
    {
        Download download(std::string_view(payload).substr(0, 211));
        const auto result = perform(server.url("/short-range"), download, true, payload.size() + 4096, payload.size());
        assert(result.code == CURLE_OK && result.bodyAllowed);
        // This legal partial response cannot pass root's full-file commit gate.
        assert(download.bytes.size() != result.totalBytes);
        assert(download.prefixHash() != digest(payload));
    }
}
} // namespace

int main() {
    assert(curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK);
    const auto payload = makePayload();
    {
        Server server(payload);
        manifestSizeBypassesUnsupportedHead(server, payload);
        validRangeAndRedirects(server, payload);
        safeFreshFallbacks(server, payload);
        interruptedThenResumed(server, payload);
        userCancelledThenResumed(server, payload);
        rejectsBeforeWriting(server, payload);
    }
    curl_global_cleanup();
    std::puts("Real libcurl manifest-size HEAD bypass, integrity, resume and user cancellation tests passed");
}
