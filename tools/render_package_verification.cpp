// Desktop-only QA of actual saved-package verification rendering and input.
#include "ui.hpp"
#include "input.hpp"
#include "install_activity.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Capture {
    std::uint64_t pixelsHash = 1469598103934665603ULL;
    int trackOrange = 0;
};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
Capture capture(const std::filesystem::path& path) {
    SDL_Window* window = SDL_GetWindowFromID(1);
    SDL_Renderer* renderer = window ? SDL_GetRenderer(window) : nullptr;
    int width = 0, height = 0;
    require(renderer && SDL_GetRendererOutputSize(renderer, &width, &height) == 0,
            "Actual SDL output is available");
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                         SDL_PIXELFORMAT_RGBA32);
    if (!surface) throw std::runtime_error(SDL_GetError());
    if (SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                             surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        throw std::runtime_error(SDL_GetError());
    }
    Capture result;
    const Uint32 orange = SDL_MapRGBA(surface->format, 255, 92, 0, 255);
    for (int y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const Uint32*>(
            static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch);
        int orangePixels = 0;
        for (int x = 0; x < width; ++x) {
            result.pixelsHash = (result.pixelsHash ^ row[x]) * 1099511628211ULL;
            if (width == 1280 && height == 720 && y >= 250 && y < 650 &&
                x >= 274 && x < 1196) orangePixels += row[x] == orange;
        }
        result.trackOrange = std::max(result.trackOrange, orangePixels);
    }
    const bool saved = IMG_SavePNG(surface, path.string().c_str()) == 0;
    SDL_FreeSurface(surface);
    require(saved, "Actual verification frame saved");
    std::cout << path.string() << '\n';
    return result;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path output = argv[1];
    std::filesystem::create_directories(output);
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    unbound::Ui ui;
    try {
        std::string error;
        require(ui.initialize(error), "Real UI initializes with packaged assets");
        ui.loading("Checking saved package", "Opening the saved ZIP for a read-only integrity check...",
                   -1, true, "Cancel verification");
        require(capture(output / "verification-opening-tv.png").trackOrange == 0,
                "Unknown verification progress shows no fabricated fill");

        unbound::InstallActivity activity(1000);
        activity.observe("Checking saved package", "Verified 0 bytes", 0, 1000);
        activity.observe("Checking saved package",
                         "Verified 477.0 MB of 1287.8 MB\nTime remaining: 2m 04s  |  6.31 MiB/s",
                         37, 73000);
        const auto measured = activity.snapshot(73000);
        require(measured.percent == 37 && measured.detail.find("Elapsed 01:12") != std::string::npos,
                "Real verification percentage and elapsed details remain authoritative");
        // Start a download first, then replace it with verification. Even an
        // already-running animation must not paint over the verification page.
        ui.loading("Downloading Unbound 0.2.0 base (1/1)", "477.0 MB of 1287.8 MB", 37, true);
        const auto show = [&] {
            ui.loading(measured.title, measured.detail, measured.percent, true, "Cancel verification");
        };
        show();
        const auto initial = capture(output / "verification-37-tv.png");
        require(initial.trackOrange >= 337 && initial.trackOrange <= 345,
                "Measured verification paints 37 percent of its track");
        ui.touchAction({true, false, 0, 0});
        ui.touchAction({true, true, 640, 686});
        SDL_Delay(90);
        ui.animateDownloadWaiting();
        const auto afterTick = capture(output / "verification-37-after-tick-tv.png");
        require(afterTick.pixelsHash == initial.pixelsHash,
                "Download dance frames do not appear on saved-package verification");
        show();
        const auto action = ui.touchAction({true, false, 0, 0});
        require(action && action->kind == unbound::TouchActionKind::Button &&
                action->value == static_cast<int>(unbound::button::B),
                "Held B touch survives verification progress redraws");

        SDL_Window* window = SDL_GetWindowFromID(1);
        require(window != nullptr, "Actual UI window remains available");
        SDL_SetWindowSize(window, 854, 480);
        show();
        const auto gamepad = capture(output / "verification-37-gamepad.png");
        SDL_Delay(90);
        ui.animateDownloadWaiting();
        require(capture(output / "verification-37-after-tick-gamepad.png").pixelsHash == gamepad.pixelsHash,
                "GamePad verification stays unchanged across animation ticks");
        ui.touchAction({true, false, 0, 0});
        ui.touchAction({true, true, 640, 686});
        show();
        const auto gamepadAction = ui.touchAction({true, false, 0, 0});
        require(gamepadAction && gamepadAction->kind == unbound::TouchActionKind::Button &&
                gamepadAction->value == static_cast<int>(unbound::button::B),
                "GamePad Cancel verification footer publishes B action");
        ui.shutdown();
        std::cout << "Actual measured verification, custom Cancel footer and no-animation QA passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        ui.shutdown();
        return 1;
    }
}
