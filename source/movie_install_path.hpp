#pragma once

#include <array>
#include <string>

namespace unbound {

enum class MovieInstallPathStatus { Unchanged, Movie, Invalid };
struct MovieInstallPath {
    MovieInstallPathStatus status = MovieInstallPathStatus::Invalid;
    std::string logical;
    std::string destination;
    // Native localized video ROOT, not the parent of a nested movie file.
    // The native caller combines this with its verified console UPDATE root.
    std::string movieDirectory;
    std::string error;
};

// Native FS paths use a 640-byte buffer including the terminator. Account for
// same-directory replacement names before writing any part of the package.
inline bool movieInstallTargetFits(const std::string& target, bool directory) {
    constexpr std::size_t ReplacementSuffixBytes = sizeof(".unbound-replacing") - 1;
    const auto suffix = directory ? 0 : ReplacementSuffixBytes;
    const auto slash = target.find_last_of('/');
    const auto leafBytes = slash == std::string::npos ? target.size() : target.size() - slash - 1;
    return !target.empty() && target.size() <= 639 - suffix && leafBytes <= 255 - suffix;
}

namespace movie_install_detail {
inline std::string lower(std::string value) {
    for (char& ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}
inline bool component(const std::string& value) {
    if (value.empty() || value.size() > 255 || value == "." || value == ".." ||
        value.find_first_of("/\\:") != std::string::npos) return false;
    for (const unsigned char ch : value) if (ch < 0x20 || ch == 0x7f) return false;
    const auto folded = lower(value);
    if (folded.find(".unbound-link-") != std::string::npos) return false;
    for (const std::string suffix : {".unbound-new", ".unbound-old", ".unbound-replacing"})
        if (folded.ends_with(suffix)) return false;
    return true;
}
inline bool safePath(const std::string& value) {
    if (value.empty() || value.size() > 639 || value.back() == '/') return false;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        if (!component(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin)))
            return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}
inline std::string nativeLanguage(const std::string& value) {
    const auto folded = lower(value);
    if (folded == "japanese") return "fulljap";
    for (const char* language : {"english", "french", "german", "spanish", "italian", "fulljap"})
        if (folded == language) return folded;
    return {};
}
inline bool underOrEqual(const std::string& value, const std::string& prefix) {
    return value == prefix || value.rfind(prefix + "/", 0) == 0;
}
} // namespace movie_install_detail

// A normal game-file installation route. The existing Wii U cinematic loader
// searches <native-language>/video before shared video. No runtime redirection,
// plugin, game-code change, SD payload, or stock video-folder conversion is used.
// Input has already been normalized/localized by the normal core mapper. The
// caller still validates allowed code/content paths and the physical title root.
inline MovieInstallPath routeMovieInstallPath(const std::string& logical,
                                               const std::string& selectedLanguage) {
    MovieInstallPath result;
    result.logical = logical;
    if (!movie_install_detail::safePath(logical)) {
        result.error = "The package contains an unsafe movie-install path.";
        return result;
    }
    const auto folded = movie_install_detail::lower(logical);
    const std::string global = "update/content/video";
    std::string originalRoot;
    std::string explicitLanguage;
    if (movie_install_detail::underOrEqual(folded, global)) {
        originalRoot = global;
    } else {
        const std::string content = "update/content/";
        if (folded.rfind(content, 0) == 0) {
            const auto end = folded.find('/', content.size());
            if (end != std::string::npos) {
                const auto language = folded.substr(content.size(), end - content.size());
                const auto native = movie_install_detail::nativeLanguage(language);
                const auto root = content + language + "/video";
                if (!native.empty() && movie_install_detail::underOrEqual(folded, root)) {
                    originalRoot = root;
                    explicitLanguage = native;
                }
            }
        }
    }
    if (originalRoot.empty()) {
        // The caller's generic FF/code validation owns language handling for
        // unrelated files. A bad selected language must not reroute those files.
        result.status = MovieInstallPathStatus::Unchanged;
        result.destination = logical;
        return result;
    }
    if (!movie_install_detail::underOrEqual(logical, originalRoot)) {
        result.error = "The package movie directory has inconsistent capitalization.";
        return result;
    }
    const auto selectedNative = movie_install_detail::nativeLanguage(selectedLanguage);
    if (selectedNative.empty()) {
        result.error = "Choose a supported content language before installing movies.";
        return result;
    }
    const auto native = explicitLanguage.empty() ? selectedNative : explicitLanguage;
    result.movieDirectory = "update/content/" + native + "/video";
    result.destination = result.movieDirectory + logical.substr(originalRoot.size());
    if (!movie_install_detail::safePath(result.destination)) {
        result.movieDirectory.clear();
        result.destination.clear();
        result.error = "The localized movie-install destination exceeds its safe path limits.";
        return result;
    }
    result.status = MovieInstallPathStatus::Movie;
    return result;
}

} // namespace unbound
