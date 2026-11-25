#pragma once

namespace unbound {

struct DownloadSize {
    bool valid = true;
    bool known = false;
    bool probed = false;
    unsigned long long bytes = 0;
};

// Verified release records already contain the complete asset size. Do not
// make another HTTP request for that same information. Unknown-size assets
// retain the existing best-effort HEAD probe and bounded streaming fallback.
template <typename Probe>
DownloadSize resolveDownloadSize(unsigned long long declaredBytes,
                                 unsigned long long maximumBytes,
                                 bool hasExpectedDigest, Probe&& probe) {
    if (declaredBytes) {
        if (!hasExpectedDigest || declaredBytes > maximumBytes)
            return {false, false, false, 0};
        return {true, true, false, declaredBytes};
    }
    unsigned long long detectedBytes = 0;
    const bool known = probe(detectedBytes);
    return {true, known, true, known ? detectedBytes : 0};
}

} // namespace unbound
