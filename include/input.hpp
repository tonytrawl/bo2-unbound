#pragma once

#include <cstdint>
#include "touch.hpp"

namespace unbound {

// Controller-independent button bits consumed by the application. Platform
// button values must be translated into this set before the UI sees them.
namespace button {
inline constexpr std::uint32_t A = 1u << 0;
inline constexpr std::uint32_t B = 1u << 1;
inline constexpr std::uint32_t X = 1u << 2;
inline constexpr std::uint32_t L = 1u << 3;
inline constexpr std::uint32_t Plus = 1u << 4;
inline constexpr std::uint32_t Left = 1u << 5;
inline constexpr std::uint32_t Right = 1u << 6;
inline constexpr std::uint32_t Up = 1u << 7;
inline constexpr std::uint32_t Down = 1u << 8;
inline constexpr std::uint32_t Minus = 1u << 9;
inline constexpr std::uint32_t Y = 1u << 10;
}

struct AppInput {
    std::uint32_t buttons = 0;
    std::uint32_t held = 0;
    TouchSample touch;
};

void initializeInput();
void shutdownInput();
AppInput readInput();

}
