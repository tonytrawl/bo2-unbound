#pragma once

#include "model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace unbound {

enum class InstallDiscoveryPath { Missing, Directory, File, Other, Unreadable };
enum class TitleProbe { Missing, Invalid, Valid };

inline std::string stockTitleRoot(Device device, std::uint64_t titleId) {
    const auto* sku = findSkuByTitle(titleId);
    if ((device != Device::MLC && device != Device::USB) || !sku ||
        (titleId != sku->baseTitleId && titleId != sku->updateTitleId)) return {};
    char root[128]{};
    std::snprintf(root, sizeof(root), "%s:/usr/title/%08x/%08x",
        device == Device::USB ? "storage_usb01" : "storage_mlc01",
        static_cast<unsigned>(titleId >> 32), static_cast<unsigned>(titleId));
    return root;
}

namespace install_discovery_detail {
inline bool xmlNameCharacter(unsigned char character, bool first) {
    return std::isalpha(character) || character == '_' ||
        (!first && (std::isdigit(character) || character == '.' || character == '-'));
}

// Stock app.xml has one <app> root and simple, immediate child fields. This
// deliberately rejects DTDs, entities, nested fields, and ambiguous duplicates.
// It is not a permissive search for tag text in comments or unrelated nodes.
class StockXmlReader {
public:
    explicit StockXmlReader(const std::string& text) : text_(text) {
        if (text_.rfind("\xef\xbb\xbf", 0) == 0) position_ = 3;
    }
    bool whitespaceAndComments() {
        while (true) {
            while (position_ < text_.size() &&
                   std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_;
            if (text_.compare(position_, 4, "<!--") != 0) return true;
            const auto end = text_.find("-->", position_ + 4);
            if (end == std::string::npos) return false;
            position_ = end + 3;
        }
    }
    bool declaration() {
        if (!whitespaceAndComments()) return false;
        if (text_.compare(position_, 5, "<?xml") != 0) return true;
        const auto boundary = position_ + 5;
        if (boundary >= text_.size() ||
            !std::isspace(static_cast<unsigned char>(text_[boundary]))) return false;
        const auto end = text_.find("?>", boundary);
        if (end == std::string::npos) return false;
        position_ = end + 2;
        return whitespaceAndComments();
    }
    bool tag(std::string& name, std::string& type, bool& closing, bool& selfClosing) {
        name.clear(); type.clear(); closing = selfClosing = false;
        if (position_ >= text_.size() || text_[position_++] != '<') return false;
        if (position_ < text_.size() && text_[position_] == '/') {
            closing = true;
            ++position_;
        }
        if (!nameToken(name)) return false;
        std::vector<std::string> attributes;
        while (position_ < text_.size()) {
            bool spaced = false;
            while (position_ < text_.size() &&
                   std::isspace(static_cast<unsigned char>(text_[position_]))) {
                ++position_;
                spaced = true;
            }
            if (position_ >= text_.size()) return false;
            if (text_[position_] == '>') { ++position_; return true; }
            if (text_[position_] == '/' && !closing &&
                position_ + 1 < text_.size() && text_[position_ + 1] == '>') {
                position_ += 2;
                selfClosing = true;
                return true;
            }
            if (closing || !spaced || attributes.size() >= 16) return false;
            std::string attribute;
            if (!nameToken(attribute) ||
                std::find(attributes.begin(), attributes.end(), attribute) != attributes.end())
                return false;
            attributes.push_back(attribute);
            skipWhitespace();
            if (position_ >= text_.size() || text_[position_++] != '=') return false;
            skipWhitespace();
            if (position_ >= text_.size() ||
                (text_[position_] != '"' && text_[position_] != '\'')) return false;
            const char quote = text_[position_++];
            const auto end = text_.find(quote, position_);
            if (end == std::string::npos) return false;
            const std::string value = text_.substr(position_, end - position_);
            if (value.find('<') != std::string::npos || value.find('&') != std::string::npos)
                return false;
            if (attribute == "type") type = value;
            position_ = end + 1;
        }
        return false;
    }
    bool leafValue(std::string& value) {
        const auto end = text_.find('<', position_);
        if (end == std::string::npos) return false;
        value = trim(text_.substr(position_, end - position_));
        if (value.find('&') != std::string::npos) return false;
        position_ = end;
        return true;
    }
    bool done() { return whitespaceAndComments() && position_ == text_.size(); }
private:
    bool nameToken(std::string& name) {
        const auto start = position_;
        if (start >= text_.size() ||
            !xmlNameCharacter(static_cast<unsigned char>(text_[start]), true)) return false;
        ++position_;
        while (position_ < text_.size() &&
               xmlNameCharacter(static_cast<unsigned char>(text_[position_]), false)) ++position_;
        name = text_.substr(start, position_ - start);
        return name.size() <= 64;
    }
    void skipWhitespace() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_;
    }
    const std::string& text_;
    std::size_t position_ = 0;
};

inline bool xmlUnsigned(const std::string& value, unsigned base,
                         std::uint64_t maximum, std::uint64_t& result) {
    if (value.empty()) return false;
    result = 0;
    for (const unsigned char character : value) {
        unsigned digit = 16;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
        if (digit >= base || digit > maximum || result > (maximum - digit) / base) return false;
        result = result * base + digit;
    }
    return true;
}

inline std::string foldedLanguage(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}
} // namespace install_discovery_detail

inline bool stockTitleMetadata(const std::string& xml, std::uint64_t expectedTitleId,
                               std::uint16_t& titleVersion) {
    using namespace install_discovery_detail;
    titleVersion = 0;
    if (xml.empty() || xml.size() > 64 * 1024) return false;
    StockXmlReader reader(xml);
    std::string name, type, value;
    bool closing = false, selfClosing = false;
    if (!reader.declaration() || !reader.tag(name, type, closing, selfClosing) ||
        name != "app" || closing || selfClosing) return false;
    bool foundTitle = false, foundVersion = false;
    std::uint16_t parsedTitleVersion = 0;
    for (unsigned field = 0; field < 128; ++field) {
        if (!reader.whitespaceAndComments() || !reader.tag(name, type, closing, selfClosing))
            return false;
        if (closing) {
            if (name != "app" || selfClosing || !foundTitle || !reader.done()) return false;
            titleVersion = parsedTitleVersion;
            return true;
        }
        const std::string child = name;
        const std::string childType = type;
        value.clear();
        if (!selfClosing) {
            if (!reader.leafValue(value) || !reader.tag(name, type, closing, selfClosing) ||
                !closing || selfClosing || name != child) return false;
        }
        std::uint64_t parsed = 0;
        if (child == "title_id") {
            if (foundTitle || childType != "hexBinary" || value.size() != 16 ||
                !xmlUnsigned(value, 16, std::numeric_limits<std::uint64_t>::max(), parsed) ||
                parsed != expectedTitleId) return false;
            foundTitle = true;
        } else if (child == "title_version") {
            if (foundVersion) return false;
            foundVersion = true;
            // Missing/unsupported/malformed version is unknown, never borrowed
            // from MCP's potentially different storage copy.
            const bool hexadecimal = childType == "hexBinary";
            if ((hexadecimal || childType == "unsignedInt") &&
                xmlUnsigned(value, hexadecimal ? 16 : 10, 65535, parsed))
                parsedTitleVersion = static_cast<std::uint16_t>(parsed);
        }
    }
    return false;
}

// IO must provide pathState(path), readText(path, output, maximum), and
// listDirectory(path, names, maximum, error). No writing methods are required.
template<class IO>
TitleProbe discoverStockTitle(Device device, std::uint64_t titleId,
                              InstallLocation& location, std::string& error, IO& io) {
    location = {};
    error.clear();
    const std::string root = stockTitleRoot(device, titleId);
    if (root.empty()) { error = "The requested title destination is unsupported"; return TitleProbe::Invalid; }
    const auto state = io.pathState(root);
    if (state == InstallDiscoveryPath::Missing) return TitleProbe::Missing;
    if (state != InstallDiscoveryPath::Directory) {
        error = "The title destination is not a readable folder: " + root;
        return TitleProbe::Invalid;
    }
    for (const char* child : {"code", "content"}) {
        if (io.pathState(root + "/" + child) != InstallDiscoveryPath::Directory) {
            error = "The title folder is incomplete or unreadable: " + root + "/" + child;
            return TitleProbe::Invalid;
        }
    }
    std::string xml;
    std::uint16_t version = 0;
    if (io.pathState(root + "/code/app.xml") != InstallDiscoveryPath::File ||
        !io.readText(root + "/code/app.xml", xml, 64 * 1024) ||
        !stockTitleMetadata(xml, titleId, version)) {
        error = "The title ID in code/app.xml could not be verified for " + root;
        return TitleProbe::Invalid;
    }
    location = {device, root, version, true};
    return TitleProbe::Valid;
}

template<class IO>
bool discoverInstallLanguages(const InstallLocation& location,
                               std::vector<std::string>& languages, std::string& error, IO& io) {
    using install_discovery_detail::foldedLanguage;
    languages.clear();
    error.clear();
    const auto knownSkus = [&](Game game) {
        const auto skus = gameSkus(game);
        return std::any_of(skus.begin(), skus.end(), [&](const auto& sku) {
            return location.root == stockTitleRoot(location.device, sku.updateTitleId);
        });
    };
    const bool knownUpdateRoot = location.present &&
        (location.device == Device::MLC || location.device == Device::USB) &&
        (knownSkus(Game::BlackOpsII) || knownSkus(Game::Ghosts));
    if (!knownUpdateRoot) {
        error = "The language scan destination is not a verified game update root";
        return false;
    }
    const std::string content = location.root + "/content";
    std::vector<std::string> names;
    if (!io.listDirectory(content, names, 4096, error)) return false;
    if (names.size() > 4096) {
        error = "The content folder has too many entries to scan safely: " + content;
        return false;
    }
    for (const auto& name : names) {
        if (!supportedLanguageFolder(name)) continue;
        const auto state = io.pathState(content + "/" + name);
        if (state == InstallDiscoveryPath::Missing || state == InstallDiscoveryPath::Unreadable) {
            error = "Could not inspect the language folder " + content + "/" + name;
            return false;
        }
        if (state != InstallDiscoveryPath::Directory) continue;
        if (std::any_of(languages.begin(), languages.end(), [&](const auto& found) {
                return foldedLanguage(found) == foldedLanguage(name);
            })) {
            error = "More than one language folder differs only by letter case in " + content;
            return false;
        }
        languages.push_back(name);
    }
    std::sort(languages.begin(), languages.end(), [](const auto& left, const auto& right) {
        return foldedLanguage(left) < foldedLanguage(right);
    });
    return true;
}

} // namespace unbound
