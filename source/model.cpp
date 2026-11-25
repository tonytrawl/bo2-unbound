#include "model.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace unbound {

const char* gameName(Game game) {
    return game == Game::Ghosts ? "Call of Duty: Ghosts" : "Black Ops II";
}

const char* regionName(Region region) {
    switch (region) {
        case Region::USA: return "USA";
        case Region::Europe: return "Europe";
        case Region::Japan: return "Japan";
        default: return "Unknown";
    }
}

const char* deviceName(Device device) {
    switch (device) {
        case Device::MLC: return "Internal storage";
        case Device::USB: return "USB storage";
        case Device::Disc: return "Game disc";
        default: return "Not installed";
    }
}

const GameSku* findSkuByTitle(std::uint64_t titleId) {
    for (const auto game : {Game::BlackOpsII, Game::Ghosts}) {
        for (const auto& sku : gameSkus(game)) {
            if (sku.baseTitleId == titleId || sku.updateTitleId == titleId || sku.aocTitleId == titleId)
                return &sku;
        }
    }
    return nullptr;
}

std::uint16_t requiredOfficialUpdateVersion(const GameSku* sku) {
    if (!sku) return 0;
    if (sku->game == Game::Ghosts) return 112;
    switch (sku->region) {
        case Region::USA:
        case Region::Europe: return 128;
        case Region::Japan: return 96;
        default: return 0;
    }
}

OfficialUpdateStatus officialUpdateStatus(const GameInstall& install) {
    const auto required = requiredOfficialUpdateVersion(install.sku);
    if (!required) return OfficialUpdateStatus::UnknownGame;
    if (!install.update.present) return OfficialUpdateStatus::Missing;
    if (!install.update.titleVersion) return OfficialUpdateStatus::UnknownVersion;
    return install.update.titleVersion < required
        ? OfficialUpdateStatus::Outdated : OfficialUpdateStatus::Current;
}

std::string officialUpdateSummary(const GameInstall& install) {
    const auto required = requiredOfficialUpdateVersion(install.sku);
    switch (officialUpdateStatus(install)) {
        case OfficialUpdateStatus::Missing:
            return "Missing - need v" + std::to_string(required);
        case OfficialUpdateStatus::UnknownVersion:
            return "Unknown - need v" + std::to_string(required);
        case OfficialUpdateStatus::Outdated:
            return "v" + std::to_string(install.update.titleVersion) +
                   " - need v" + std::to_string(required);
        case OfficialUpdateStatus::Current:
            return "v" + std::to_string(install.update.titleVersion) + " - current";
        default: return "Game not identified";
    }
}

std::string officialUpdateWarning(const GameInstall& install) {
    const auto state = officialUpdateStatus(install);
    if (state == OfficialUpdateStatus::Current) return {};
    if (state == OfficialUpdateStatus::UnknownGame && install.game == Game::Ghosts)
        return "HQ could not identify a supported Ghosts game. The official update requirement is v112.";
    if (state == OfficialUpdateStatus::UnknownGame)
        return "HQ could not identify a supported Black Ops II game, so the official "
               "update version could not be checked. USA and European games require "
               "v128; Japanese games require v96.";
    const std::string expected = "v" + std::to_string(requiredOfficialUpdateVersion(install.sku));
    const std::string name = gameName(install.sku->game);
    std::string message;
    if (state == OfficialUpdateStatus::Missing)
        message = "No official " + name + " update was found.";
    else if (state == OfficialUpdateStatus::UnknownVersion)
        message = "An official " + name + " update was found, but HQ could not read its version.";
    else
        message = "Your official " + name + " update is v" +
                  std::to_string(install.update.titleVersion) + ", which is older than " + expected + ".";
    return message + "\n\nYour " + regionName(install.sku->region) +
           " game needs " + expected + ". Update the game from the Wii U Menu before "
           "playing Unbound. HQ installs Unbound files, not Nintendo's official game update.";
}

std::string languageFolderForSku(const GameSku* sku, const std::string& locale) {
    if (!sku) return "english";
    std::string code = locale;
    std::transform(code.begin(), code.end(), code.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const auto separator = code.find_first_of("-_ ");
    if (separator != std::string::npos) code.resize(separator);
    const std::string key = sku->key;
    if (key == "JPN") return "japanese";
    if (key == "EUR_DE") return "german";
    if (key == "EUR_ES_IT") return code == "it" ? "italian" : "spanish";
    if (key == "EUR_EN_FR_DE") return code == "de" ? "german" : (code == "fr" ? "french" : "english");
    if (sku->game == Game::Ghosts && key == "USA") return code == "fr" ? "french" : "english";
    if (key == "EUR_EN_FR") return code == "fr" ? "french" : "english";
    if (code == "fr") return "french";
    if (code == "es") return "spanish";
    return "english";
}

bool supportedLanguageFolder(const std::string& folder) {
    static constexpr const char* Languages[] = {
        "english", "french", "spanish", "italian", "german", "japanese", "fulljap"
    };
    std::string lower = folder;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return std::any_of(std::begin(Languages), std::end(Languages),
                      [&lower](const char* value) { return lower == value; });
}

bool validInstallPreferences(const InstallPreferences& preferences) {
    const bool automatic = preferences.updateDevice == Device::Missing &&
                           preferences.updateTitleId == 0;
    const auto* sku = findSkuByTitle(preferences.updateTitleId);
    const bool manual = (preferences.updateDevice == Device::MLC ||
                         preferences.updateDevice == Device::USB) && sku &&
                        sku->updateTitleId == preferences.updateTitleId;
    return (automatic || manual) && (preferences.languageFolder.empty() ||
                                     supportedLanguageFolder(preferences.languageFolder));
}

std::string installTargetKey(std::uint64_t updateTitleId, Device device) {
    InstallPreferences preferences;
    preferences.updateDevice = device;
    preferences.updateTitleId = updateTitleId;
    if (!validInstallPreferences(preferences)) return {};
    if (!updateTitleId) return "auto";
    std::ostringstream value;
    value << (device == Device::USB ? "usb:" : "mlc:") << std::hex
          << std::setfill('0') << std::setw(16) << updateTitleId;
    return value.str();
}

bool parseInstallPreferences(const std::string& text, InstallPreferences& preferences,
                             std::string& error) {
    preferences = {};
    error.clear();
    bool sawTarget = false, sawLanguage = false, sawPending = false;
    std::istringstream lines(text);
    std::string line;
    const auto fail = [&](const std::string& message) {
        preferences = {};
        error = message;
        return false;
    };
    while (std::getline(lines, line)) {
        if (line.rfind("\xef\xbb\xbf", 0) == 0) line.erase(0, 3);
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';' ||
            (line.front() == '[' && line.back() == ']')) continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = trim(line.substr(0, equals));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (key != "install_target" && key != "install_language" &&
            key != "install_language_pending") continue;
        std::string value = trim(line.substr(equals + 1));
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (key == "install_target") {
            if (sawTarget) return fail("More than one install_target setting was found.");
            sawTarget = true;
            if (lower == "auto") continue;
            if (lower.size() != 20 || (lower.rfind("usb:", 0) != 0 &&
                                      lower.rfind("mlc:", 0) != 0) ||
                !std::all_of(lower.begin() + 4, lower.end(), [](unsigned char c) {
                    return std::isxdigit(c);
                })) return fail("The saved install target is invalid. Choose an existing update in Install / Update.");
            std::istringstream number(lower.substr(4));
            number >> std::hex >> preferences.updateTitleId;
            preferences.updateDevice = lower.rfind("usb:", 0) == 0 ? Device::USB : Device::MLC;
        } else if (key == "install_language") {
            if (sawLanguage) return fail("More than one install_language setting was found.");
            sawLanguage = true;
            if (lower != "auto") preferences.languageFolder = value;
        } else {
            if (sawPending) return fail("More than one install_language_pending setting was found.");
            sawPending = true;
            if (lower == "on" || lower == "true" || lower == "1") preferences.languageInstallPending = true;
            else if (lower == "off" || lower == "false" || lower == "0") preferences.languageInstallPending = false;
            else return fail("The saved language-install state is invalid.");
        }
    }
    if (!validInstallPreferences(preferences))
        return fail("The saved install selection is not a supported update destination or language.");
    return true;
}

std::string replaceInstallPreferences(const std::string& text,
                                      const InstallPreferences& preferences) {
    if (!validInstallPreferences(preferences)) return {};
    std::istringstream lines(text);
    std::string line, output;
    while (std::getline(lines, line)) {
        if (line.rfind("\xef\xbb\xbf", 0) == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = trim(line);
        const auto equals = stripped.find('=');
        std::string key = equals == std::string::npos ? std::string{} : trim(stripped.substr(0, equals));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (key != "install_target" && key != "install_language" &&
            key != "install_language_pending") output += line + "\n";
    }
    output += "install_target=" + installTargetKey(preferences.updateTitleId, preferences.updateDevice) + "\n";
    output += "install_language=" + (preferences.languageFolder.empty() ? "auto" : preferences.languageFolder) + "\n";
    output += std::string("install_language_pending=") + (preferences.languageInstallPending ? "on\n" : "off\n");
    return output;
}

bool localizeArchivePath(const std::string& path, const std::string& languageFolder,
                         std::string& localized) {
    if (!supportedLanguageFolder(languageFolder)) return false;
    std::istringstream stream(path);
    std::string component;
    localized.clear();
    while (std::getline(stream, component, '/')) {
        if (!localized.empty()) localized.push_back('/');
        localized += component == "@language" ? languageFolder : component;
    }
    return !localized.empty();
}

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::vector<std::string> splitEscaped(const std::string& line, char separator) {
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;
    for (char c : line) {
        if (escaped) {
            if (c == 'n') current.push_back('\n');
            else if (c == 'r') current.push_back('\r');
            else if (c == 't') current.push_back('\t');
            else current.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == separator) {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (escaped) current.push_back('\\');
    fields.push_back(trim(current));
    return fields;
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64 || id.front() == '-' || id.back() == '-') return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

bool safeRelativePath(const std::string& path) {
    if (path.empty() || path.size() > 768 || path.front() == '/' || path.front() == '\\') return false;
    if (path.find(':') != std::string::npos || path.find('\\') != std::string::npos) return false;
    if (std::any_of(path.begin(), path.end(), [](unsigned char value) {
            return value < 0x20 || value == 0x7f;
        })) return false;
    std::istringstream stream(path);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == "." || part == "..") return false;
    }
    return true;
}

std::vector<std::string> directoryPrefixes(const std::string& path) {
    std::vector<std::string> result;
    if (path.empty()) return result;
    std::string current;
    std::size_t start = 0;
    const std::string aromaRoot = "fs:/vol/external01";
    if (path == aromaRoot || path.rfind(aromaRoot + "/", 0) == 0) {
        current = aromaRoot;
        start = path.size() == aromaRoot.size() ? path.size() : aromaRoot.size() + 1;
    } else {
        const auto colon = path.find(":/");
        if (colon != std::string::npos) {
            current = path.substr(0, colon + 2);
            start = colon + 2;
        }
    }
    while (start < path.size()) {
        const auto slash = path.find('/', start);
        const auto part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/') current.push_back('/');
            current += part;
            result.push_back(current);
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return result;
}

static std::vector<unsigned long> versionParts(const std::string& input) {
    std::vector<unsigned long> values;
    const auto patchMarker = input.find_first_of("Pp");
    const std::string base = input.substr(0, patchMarker);
    std::string part;
    std::istringstream stream(base);
    while (std::getline(stream, part, '.')) {
        unsigned long value = 0;
        for (unsigned char c : part) {
            if (!std::isdigit(c)) break;
            value = value * 10 + (c - '0');
        }
        values.push_back(value);
    }
    if (patchMarker == std::string::npos) {
        values.push_back(0);
    } else {
        std::istringstream patches(input.substr(patchMarker + 1));
        while (std::getline(patches, part, '.')) {
            unsigned long value = 0;
            for (unsigned char c : part) {
                if (!std::isdigit(c)) break;
                value = value * 10 + (c - '0');
            }
            values.push_back(value);
        }
    }
    return values;
}

int compareVersions(const std::string& left, const std::string& right) {
    auto a = versionParts(left);
    auto b = versionParts(right);
    const auto count = std::max(a.size(), b.size());
    a.resize(count); b.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static bool ignoredLine(const std::string& line) {
    const auto value = trim(line);
    return value.empty() || value.front() == '#';
}

static bool validReleaseVersion(const std::string& value) {
    if (value.empty()) return false;
    std::size_t position = 0;
    for (int component = 0; component < 3; ++component) {
        const std::size_t start = position;
        while (position < value.size() && std::isdigit(static_cast<unsigned char>(value[position])))
            ++position;
        if (position == start) return false;
        if (component < 2) {
            if (position >= value.size() || value[position] != '.') return false;
            ++position;
        }
    }
    if (position == value.size()) return true;
    if (value[position++] != 'P' || position == value.size()) return false;
    bool componentHasDigit = false;
    for (; position < value.size(); ++position) {
        const unsigned char c = value[position];
        if (std::isdigit(c)) componentHasDigit = true;
        else if (c == '.' && componentHasDigit) componentHasDigit = false;
        else return false;
    }
    return componentHasDigit;
}

static bool validSha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c);
    });
}

static bool parsePositiveBytes(const std::string& value, std::uint64_t& output) {
    if (value.empty()) return false;
    std::uint64_t result = 0;
    for (const unsigned char c : value) {
        if (!std::isdigit(c)) return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        result = result * 10 + digit;
    }
    if (!result) return false;
    output = result;
    return true;
}

static bool validAppVersionLabel(const std::string& value) {
    return !value.empty() && value.size() <= 48 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return std::isalnum(c) || c == '.' || c == '-' || c == '_';
           });
}

std::vector<Release> parseReleases(const std::string& text, std::vector<std::string>* errors) {
    std::vector<Release> result;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (ignoredLine(line)) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fields = splitEscaped(line, '|');
        if (!fields.empty() && (fields[0] == "PACK" || fields[0] == "FILE")) continue;
        if (fields.size() == 4 && !fields[0].empty() && !fields[1].empty() &&
            !fields[2].empty() && !fields[3].empty()) {
            result.push_back({fields[0], ReleaseKind::Full, "", fields[1], fields[2], fields[3], 0});
            continue;
        }
        if (fields.size() != 7 || !validId(fields[0]) ||
            (fields[1] != "FULL" && fields[1] != "PATCH") ||
            !validReleaseVersion(fields[3]) || fields[4].rfind("https://", 0) != 0 ||
            !validSha256(fields[5])) {
            if (errors) errors->push_back("Invalid releases.txt line " + std::to_string(lineNumber));
            continue;
        }
        std::uint64_t bytes = 0;
        if (!parsePositiveBytes(fields[6], bytes)) {
            if (errors) errors->push_back("Invalid package size on releases.txt line " + std::to_string(lineNumber));
            continue;
        }
        const bool patch = fields[1] == "PATCH";
        if ((!patch && fields[2] != "-") ||
            (patch && (!validReleaseVersion(fields[2]) || compareVersions(fields[2], fields[3]) >= 0))) {
            if (errors) errors->push_back("Invalid release edge on releases.txt line " + std::to_string(lineNumber));
            continue;
        }
        result.push_back({fields[0], patch ? ReleaseKind::Patch : ReleaseKind::Full,
                          patch ? fields[2] : "", fields[3], fields[4], fields[5], bytes});
    }
    return result;
}

std::vector<AppRelease> parseAppReleases(const std::string& text,
                                         std::vector<std::string>* errors) {
    std::vector<AppRelease> result;
    std::unordered_map<std::uint64_t, bool> seenRevisions;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto directive = trim(line);
        // The directive is deliberately a comment to every legacy content-feed
        // parser. Only new HQ builds interpret this exact prefix.
        if (directive.rfind("#HQ|", 0) != 0) continue;
        const auto fields = splitEscaped(directive.substr(1), '|');
        std::uint64_t revision = 0;
        std::uint64_t bytes = 0;
        if (fields.size() != 6 || fields[0] != "HQ" ||
            !parsePositiveBytes(fields[1], revision) ||
            !validAppVersionLabel(fields[2]) ||
            fields[3].rfind("https://", 0) != 0 ||
            !validSha256(fields[4]) || !parsePositiveBytes(fields[5], bytes)) {
            if (errors)
                errors->push_back("Invalid #HQ directive on release-feed line " +
                                  std::to_string(lineNumber));
            continue;
        }
        if (!seenRevisions.emplace(revision, true).second) {
            if (errors)
                errors->push_back("Duplicate HQ revision on release-feed line " +
                                  std::to_string(lineNumber));
            continue;
        }
        result.push_back({revision, fields[2], fields[3], fields[4], bytes});
    }
    return result;
}

const AppRelease* latestAppRelease(const std::vector<AppRelease>& releases) {
    if (releases.empty()) return nullptr;
    return &*std::max_element(releases.begin(), releases.end(),
                              [](const AppRelease& left, const AppRelease& right) {
                                  return left.revision < right.revision;
                              });
}

bool sameAppRelease(const AppRelease& left, const AppRelease& right) {
    return left.revision == right.revision && left.version == right.version &&
           left.downloadUrl == right.downloadUrl && left.sha256 == right.sha256 &&
           left.fileBytes == right.fileBytes;
}

bool replaceAppReleaseDirectives(const std::string& text,
                                 const AppRelease* retainedRelease,
                                 std::size_t maximumBytes,
                                 std::string& output) {
    std::istringstream stream(text);
    output.clear();
    auto append = [&output, maximumBytes](const std::string& value) {
        if (value.size() > maximumBytes || output.size() > maximumBytes - value.size())
            return false;
        output += value;
        return true;
    };
    std::string line;
    while (std::getline(stream, line)) {
        std::string comparable = line;
        if (!comparable.empty() && comparable.back() == '\r') comparable.pop_back();
        if (trim(comparable).rfind("#HQ|", 0) == 0) continue;
        if (!append(line) || !append("\n")) { output.clear(); return false; }
    }
    if (retainedRelease) {
        const std::string directive =
            "#HQ|" + std::to_string(retainedRelease->revision) + "|" +
            retainedRelease->version + "|" + retainedRelease->downloadUrl + "|" +
            retainedRelease->sha256 + "|" +
            std::to_string(retainedRelease->fileBytes) + "\n";
        if (!append(directive)) { output.clear(); return false; }
    }
    return true;
}

std::vector<WorkshopMod> parseWorkshop(const std::string& text, std::vector<std::string>* errors) {
    std::vector<WorkshopMod> result;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (ignoredLine(line)) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fields = splitEscaped(line, '|');
        if ((fields.size() != 6 && fields.size() != 10) || !validId(fields[0]) || fields[1].empty() || fields[4].empty()) {
            if (errors) errors->push_back("Invalid workshop.txt line " + std::to_string(lineNumber));
            continue;
        }
        WorkshopMod mod{fields[0], fields[1], fields[2], fields[3], fields[4], fields[5],
                        0, 0, 0, ""};
        if (fields.size() == 10) {
            auto parseCount = [](const std::string& value, unsigned& output) {
                if (value.empty()) return false;
                unsigned result = 0;
                for (const unsigned char c : value) {
                    if (!std::isdigit(c)) return false;
                    const unsigned digit = c - '0';
                    if (result > (std::numeric_limits<unsigned>::max() - digit) / 10) return false;
                    result = result * 10 + digit;
                }
                output = result;
                return true;
            };
            if (!parseCount(fields[6], mod.upvotes) || !parseCount(fields[7], mod.downvotes) ||
                !parseCount(fields[8], mod.commentCount)) {
                if (errors) errors->push_back("Invalid workshop.txt ratings on line " + std::to_string(lineNumber));
                continue;
            }
            mod.featuredComment = fields[9];
        }
        result.push_back(mod);
    }
    return result;
}

namespace {

bool validPluginArchivePath(const std::string& path) {
    static constexpr const char* PluginPrefix =
        "wiiu/environments/aroma/plugins/";
    static constexpr const char* ModulePrefix =
        "wiiu/environments/aroma/modules/";
    if (!safeRelativePath(path)) return false;
    const auto validLeaf = [&path](const char* prefix, const char* suffix) {
        const std::string root(prefix);
        if (path.rfind(root, 0) != 0 || path.size() <= root.size()) return false;
        const std::string leaf = path.substr(root.size());
        return leaf.find('/') == std::string::npos &&
               leaf.size() > std::char_traits<char>::length(suffix) &&
               leaf.rfind(suffix) == leaf.size() - std::char_traits<char>::length(suffix);
    };
    return validLeaf(PluginPrefix, ".wps") || validLeaf(ModulePrefix, ".wms");
}

std::string folded(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parseManagedPaths(const std::string& value, std::vector<std::string>& paths) {
    paths.clear();
    std::istringstream stream(value);
    std::string path;
    std::unordered_map<std::string, bool> seen;
    while (std::getline(stream, path, ',')) {
        path = trim(path);
        if (!validPluginArchivePath(path) ||
            !seen.emplace(folded(path), true).second || paths.size() >= 16)
            return false;
        paths.push_back(path);
    }
    return !paths.empty();
}

} // namespace

std::vector<WorkshopPlugin> parsePluginCatalog(const std::string& text,
                                                std::vector<std::string>* errors) {
    std::vector<WorkshopPlugin> result;
    std::unordered_map<std::string, bool> seenKeys;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (ignoredLine(line)) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto fields = splitEscaped(line, '|');
        WorkshopPlugin plugin;
        if (fields.size() != 8 || fields[0] != "PLUGIN" ||
            !validId(fields[1]) || fields[2].empty() || fields[3].empty() ||
            fields[4].empty() || fields[5].rfind("https://", 0) != 0 ||
            fields[6].rfind("https://", 0) != 0 ||
            !parseManagedPaths(fields[7], plugin.managedPaths) ||
            !seenKeys.emplace(folded(fields[1]), true).second) {
            if (errors)
                errors->push_back("Invalid workshop-plugins.txt line " +
                                  std::to_string(lineNumber));
            continue;
        }
        plugin.key = fields[1];
        plugin.name = fields[2];
        plugin.creator = fields[3];
        plugin.description = fields[4];
        plugin.releaseFeedUrl = fields[5];
        plugin.homepageUrl = fields[6];
        result.push_back(std::move(plugin));
    }
    return result;
}

bool parsePluginRelease(const std::string& text, const WorkshopPlugin& plugin,
                        PluginRelease& release, std::vector<std::string>* errors) {
    release = {};
    bool foundRelease = false;
    std::unordered_map<std::string, std::string> approved;
    for (const auto& path : plugin.managedPaths) approved.emplace(folded(path), path);
    std::unordered_map<std::string, bool> seenFiles;
    bool sawPlugin = false;
    std::istringstream stream(text);
    std::string line;
    unsigned lineNumber = 0;
    bool valid = true;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto directive = trim(line);
        if (directive.rfind("#PLUGIN|", 0) == 0) {
            const auto fields = splitEscaped(directive.substr(1), '|');
            if (fields.size() >= 2 && fields[1] != plugin.key) continue;
            std::uint64_t revision = 0;
            std::uint64_t bytes = 0;
            if (foundRelease || fields.size() != 7 || fields[0] != "PLUGIN" ||
                fields[1] != plugin.key || !parsePositiveBytes(fields[2], revision) ||
                !validAppVersionLabel(fields[3]) || fields[4].rfind("https://", 0) != 0 ||
                !validSha256(fields[5]) || !parsePositiveBytes(fields[6], bytes)) {
                if (errors)
                    errors->push_back("Invalid #PLUGIN directive on release-feed line " +
                                      std::to_string(lineNumber));
                valid = false;
                continue;
            }
            foundRelease = true;
            release.pluginKey = fields[1];
            release.revision = revision;
            release.version = fields[3];
            release.downloadUrl = fields[4];
            release.sha256 = folded(fields[5]);
            release.compressedBytes = bytes;
            continue;
        }
        if (directive.rfind("#PLUGIN_FILE|", 0) == 0) {
            const auto fields = splitEscaped(directive.substr(1), '|');
            if (fields.size() >= 2 && fields[1] != plugin.key) continue;
            std::uint64_t bytes = 0;
            const std::string key = fields.size() > 2 ? folded(fields[2]) : std::string{};
            if (fields.size() != 5 || fields[0] != "PLUGIN_FILE" ||
                fields[1] != plugin.key || !validPluginArchivePath(fields[2]) ||
                approved.find(key) == approved.end() || approved[key] != fields[2] ||
                !validSha256(fields[3]) || !parsePositiveBytes(fields[4], bytes) ||
                !seenFiles.emplace(key, true).second || release.files.size() >= 16) {
                if (errors)
                    errors->push_back("Invalid #PLUGIN_FILE directive on release-feed line " +
                                      std::to_string(lineNumber));
                valid = false;
                continue;
            }
            sawPlugin = sawPlugin || fields[2].rfind(
                "wiiu/environments/aroma/plugins/", 0) == 0;
            release.files.push_back({fields[2], folded(fields[3]), bytes});
            continue;
        }
        if (ignoredLine(line)) continue;
        const auto fields = splitEscaped(line, '|');
        // A plugin may share HQ's release-chain file. Ordinary content records
        // and another plugin's legacy release record are unrelated here.
        if (!fields.empty() && fields[0] != "FILE" && fields[0] != plugin.key)
            continue;
        // Eight-field FILE rows belong to map packs, not legacy plugin files.
        if (fields.size() == 8 && fields[0] == "FILE" &&
            (fields[2] == "FULL" || fields[2] == "PATCH")) continue;
        if (!fields.empty() && fields[0] == "FILE") {
            std::uint64_t bytes = 0;
            const std::string key = fields.size() > 1 ? folded(fields[1]) : std::string{};
            if (fields.size() != 4 || !validPluginArchivePath(fields[1]) ||
                approved.find(key) == approved.end() || approved[key] != fields[1] ||
                !validSha256(fields[2]) ||
                !parsePositiveBytes(fields[3], bytes) ||
                !seenFiles.emplace(key, true).second ||
                release.files.size() >= 16) {
                if (errors)
                    errors->push_back("Invalid plugin FILE record on line " +
                                      std::to_string(lineNumber));
                valid = false;
                continue;
            }
            sawPlugin = sawPlugin || fields[1].rfind(
                "wiiu/environments/aroma/plugins/", 0) == 0;
            release.files.push_back({fields[1], folded(fields[2]), bytes});
            continue;
        }
        std::uint64_t revision = 0;
        std::uint64_t bytes = 0;
        if (foundRelease || fields.size() != 6 || fields[0] != plugin.key ||
            !parsePositiveBytes(fields[1], revision) ||
            !validAppVersionLabel(fields[2]) || fields[3].rfind("https://", 0) != 0 ||
            !validSha256(fields[4]) || !parsePositiveBytes(fields[5], bytes)) {
            if (errors)
                errors->push_back("Invalid plugin release record on line " +
                                  std::to_string(lineNumber));
            valid = false;
            continue;
        }
        foundRelease = true;
        release.pluginKey = fields[0];
        release.revision = revision;
        release.version = fields[2];
        release.downloadUrl = fields[3];
        release.sha256 = folded(fields[4]);
        release.compressedBytes = bytes;
    }
    if (!foundRelease || release.files.size() != approved.size() || !sawPlugin) {
        if (errors)
            errors->push_back(!foundRelease
                ? "Plugin feed contains no release record"
                : "Plugin feed FILE records do not exactly match the curated path allowlist");
        valid = false;
    }
    if (valid) {
        std::sort(release.files.begin(), release.files.end(),
                  [](const PluginFile& left, const PluginFile& right) {
                      return left.archivePath < right.archivePath;
                  });
    } else {
        release = {};
    }
    return valid;
}

bool samePluginRelease(const PluginRelease& left, const PluginRelease& right) {
    if (left.pluginKey != right.pluginKey || left.revision != right.revision ||
        left.version != right.version || left.downloadUrl != right.downloadUrl ||
        folded(left.sha256) != folded(right.sha256) ||
        left.compressedBytes != right.compressedBytes ||
        left.files.size() != right.files.size())
        return false;
    for (std::size_t index = 0; index < left.files.size(); ++index) {
        if (left.files[index].archivePath != right.files[index].archivePath ||
            folded(left.files[index].sha256) != folded(right.files[index].sha256) ||
            left.files[index].bytes != right.files[index].bytes)
            return false;
    }
    return true;
}

const Release* releaseForSku(const std::vector<Release>& releases, const GameSku* sku) {
    if (!sku) return nullptr;
    const Release* selected = nullptr;
    for (const auto& release : releases) {
        if (release.regionKey != sku->key && release.regionKey != "UNIVERSAL") continue;
        if (!selected || compareVersions(selected->version, release.version) < 0 ||
            (compareVersions(selected->version, release.version) == 0 &&
             release.regionKey == sku->key && selected->regionKey == "UNIVERSAL"))
            selected = &release;
    }
    return selected;
}

std::string latestReleaseVersion(const std::vector<Release>& releases, const GameSku* sku) {
    const auto* release = releaseForSku(releases, sku);
    return release ? release->version : std::string{};
}

bool releaseAppliesToInstalledVersion(const Release& release,
                                      const std::string& installedVersion) {
    const bool missing = installedVersion.empty() || installedVersion == "Not installed";
    if (release.kind == ReleaseKind::Full)
        return missing || compareVersions(installedVersion, release.version) < 0;
    if (missing) return false;

    // All core ZIPs are add/replace-only overlays. A newer full snapshot can
    // roll any older install forward. For a patch, FROM_VERSION is the oldest
    // compatible checkpoint, allowing a cumulative base-to-latest patch to
    // safely skip intermediate patch levels already on the console.
    return compareVersions(installedVersion, release.fromVersion) >= 0 &&
           compareVersions(installedVersion, release.version) < 0;
}

std::vector<const Release*> releasePlanForSku(const std::vector<Release>& releases,
                                               const GameSku* sku,
                                               const std::string& installedVersion) {
    std::vector<const Release*> empty;
    if (!sku) return empty;
    const auto latest = latestReleaseVersion(releases, sku);
    if (latest.empty()) return empty;

    const std::string start = installedVersion.empty() || installedVersion == "Not installed"
        ? std::string{} : installedVersion;
    if (!start.empty() && compareVersions(start, latest) >= 0) return empty;

    struct State {
        std::string version;
        std::uint64_t cost = 0;
        std::vector<const Release*> path;
    };
    struct Later {
        bool operator()(const State& left, const State& right) const {
            if (left.path.size() != right.path.size()) return left.path.size() > right.path.size();
            return left.cost > right.cost;
        }
    };
    struct Best { std::uint64_t cost; std::size_t hops; };
    std::priority_queue<State, std::vector<State>, Later> pending;
    std::unordered_map<std::string, Best> best;
    pending.push({start, 0, {}});
    best.emplace(start, Best{0, 0});

    while (!pending.empty()) {
        State state = pending.top();
        pending.pop();
        const auto currentBest = best.find(state.version);
        if (currentBest == best.end() || currentBest->second.cost != state.cost ||
            currentBest->second.hops != state.path.size()) continue;
        if (state.version == latest) return state.path;
        if (state.path.size() >= 32) continue;

        for (const auto& release : releases) {
            if (release.regionKey != sku->key && release.regionKey != "UNIVERSAL") continue;
            if (!releaseAppliesToInstalledVersion(release, state.version)) continue;
            const std::uint64_t edge = release.compressedBytes ? release.compressedBytes : 1;
            if (state.cost > std::numeric_limits<std::uint64_t>::max() - edge) continue;
            const std::uint64_t cost = state.cost + edge;
            const std::size_t hops = state.path.size() + 1;
            const auto known = best.find(release.version);
            if (known != best.end() &&
                (known->second.hops < hops ||
                 (known->second.hops == hops && known->second.cost <= cost))) continue;
            auto path = state.path;
            path.push_back(&release);
            best[release.version] = {cost, hops};
            pending.push({release.version, cost, std::move(path)});
        }
    }
    return empty;
}

int applyWorkshopVote(WorkshopMod& mod, int currentVote, int requestedVote) {
    if ((requestedVote != 1 && requestedVote != -1) || currentVote == requestedVote)
        return currentVote;
    if (currentVote == 1 && mod.upvotes > 0) --mod.upvotes;
    if (currentVote == -1 && mod.downvotes > 0) --mod.downvotes;
    if (requestedVote == 1) ++mod.upvotes;
    else ++mod.downvotes;
    return requestedVote;
}

std::string modSyncTokenFromManifest(const std::string& text) {
    std::string version;
    std::string syncId;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = trim(line.substr(0, equals));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        const std::string value = trim(line.substr(equals + 1));
        const bool safeValue = !value.empty() && value.size() <= 128 &&
            std::none_of(value.begin(), value.end(), [](unsigned char c) {
                return c < 0x20 || c == 0x7f;
            });
        if (!safeValue) continue;
        if (key == "sync_id") syncId = value;
        else if (key == "version") version = value;
    }
    if (!syncId.empty()) return "sync_id=" + syncId;
    if (!version.empty()) return "version=" + version;
    return {};
}

LocalMod parseModload(const std::string& text, const std::string& folder, bool installed) {
    LocalMod mod{folder, folder, "No description provided.", "Unknown author",
                 modSyncTokenFromManifest(text), installed};
    std::vector<std::string> plain;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        const auto equals = line.find('=');
        if (equals != std::string::npos) {
            std::string key = trim(line.substr(0, equals));
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            const std::string value = trim(line.substr(equals + 1));
            if (key == "name" && !value.empty()) mod.name = value;
            else if ((key == "description" || key == "desc") && !value.empty()) mod.description = value;
            else if ((key == "author" || key == "creator") && !value.empty()) mod.author = value;
            continue;
        }
        const auto fields = splitEscaped(line, ';');
        if (fields.size() >= 3) {
            if (!fields[0].empty()) mod.name = fields[0];
            if (!fields[1].empty()) mod.description = fields[1];
            if (!fields[2].empty()) mod.author = fields[2];
            continue;
        }
        plain.push_back(line);
    }
    if (!plain.empty()) mod.name = plain[0];
    if (plain.size() > 1) mod.description = plain[1];
    if (plain.size() > 2) mod.author = plain[2];
    return mod;
}

std::vector<NewsImage> parseNewsImages(const std::string& text) {
    std::vector<NewsImage> images;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto image = line.find("![");
        const auto captionEnd = image == std::string::npos ? std::string::npos : line.find("](https://", image + 2);
        const auto urlEnd = captionEnd == std::string::npos ? std::string::npos : line.find(')', captionEnd + 2);
        if (image == std::string::npos || captionEnd == std::string::npos || urlEnd == std::string::npos) continue;
        NewsImage result;
        result.caption = line.substr(image + 2, captionEnd - image - 2);
        result.url = line.substr(captionEnd + 2, urlEnd - captionEnd - 2);
        if (result.url.rfind("https://", 0) == 0) images.push_back(result);
    }
    return images;
}

std::string plainNewsPreview(const std::string& text) {
    std::istringstream stream(text);
    std::string result,line;
    while (std::getline(stream,line)) {
        line=trim(line);
        if (line.empty()) continue;
        if (line.rfind("![",0)==0) continue;
        while (!line.empty() && line.front()=='#') line.erase(line.begin());
        line=trim(line);
        if (line.rfind("- ",0)==0) line="• "+line.substr(2);
        for (const std::string marker : {"**","__","*","_"}) {
            std::size_t position=0;
            while ((position=line.find(marker,position))!=std::string::npos) line.erase(position,marker.size());
        }
        if (line=="---") continue;
        if (!result.empty()) result+='\n';
        result+=line;
    }
    return result;
}

double updateSmoothedRate(double previousUnitsPerSecond, std::uint64_t completedDelta,
                          std::uint64_t elapsedMilliseconds) {
    if (elapsedMilliseconds == 0) return previousUnitsPerSecond;
    const double sample = static_cast<double>(completedDelta) * 1000.0 /
                          static_cast<double>(elapsedMilliseconds);
    if (!std::isfinite(sample) || sample < 0.0) return previousUnitsPerSecond;
    if (!std::isfinite(previousUnitsPerSecond) || previousUnitsPerSecond <= 0.0) return sample;

    // A time-weighted moving average prevents the remaining-time display from
    // jumping with every network burst or unusually large ZIP entry. The
    // elapsed-time weighting keeps the result consistent at different callback
    // frequencies and lets a sustained speed change settle over a few seconds.
    constexpr double SmoothingWindowMilliseconds = 3000.0;
    const double alpha = std::min(1.0, static_cast<double>(elapsedMilliseconds) /
                                          SmoothingWindowMilliseconds);
    return previousUnitsPerSecond + (sample - previousUnitsPerSecond) * alpha;
}

bool estimateRemainingSeconds(std::uint64_t completed, std::uint64_t total,
                              double unitsPerSecond, std::uint64_t& seconds) {
    if (total == 0 || completed > total || !std::isfinite(unitsPerSecond) ||
        unitsPerSecond <= 0.0)
        return false;
    if (completed == total) {
        seconds = 0;
        return true;
    }
    const long double remaining = static_cast<long double>(total - completed) /
                                  static_cast<long double>(unitsPerSecond);
    if (!std::isfinite(remaining) ||
        remaining > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        return false;
    seconds = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(remaining)));
    return true;
}

std::string formatRemainingTime(std::uint64_t seconds) {
    if (seconds == 0) return "Finishing...";
    if (seconds < 60) return "Time remaining: " + std::to_string(seconds) + "s";
    const auto minutes = seconds / 60;
    const auto remainder = seconds % 60;
    const std::string paddedSeconds = remainder < 10 ? "0" + std::to_string(remainder)
                                                     : std::to_string(remainder);
    if (minutes < 60)
        return "Time remaining: " + std::to_string(minutes) + "m " + paddedSeconds + "s";
    // Hours omit seconds to keep the Wii U status line compact, so round the
    // displayed minute upward rather than promising completion too early.
    const auto roundedMinutes = minutes + (remainder ? 1 : 0);
    const auto hours = roundedMinutes / 60;
    const auto remainingMinutes = roundedMinutes % 60;
    const std::string paddedMinutes = remainingMinutes < 10
        ? "0" + std::to_string(remainingMinutes) : std::to_string(remainingMinutes);
    return "Time remaining: " + std::to_string(hours) + "h " + paddedMinutes + "m";
}

} // namespace unbound
