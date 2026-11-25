#pragma once

#include <cstdint>
#include <string_view>

namespace unbound {

// Tester instructions are metadata and have no destination in the game title.
inline constexpr bool testerReadmeEntry(std::string_view path, bool directory,
                                       std::uint64_t bytes) {
    return path == "README_TESTER.txt" && !directory && bytes <= 64 * 1024;
}

} // namespace unbound
