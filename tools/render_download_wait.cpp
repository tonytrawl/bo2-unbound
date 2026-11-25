// Desktop-only validation of the real download UI and its wait-loop redraws.
// Build with the real ui/model/background_music sources and SDL2/image/ttf.
// Run in a temporary directory containing fs:/vol/content -> project/content.
// A temporary coreinit/debug.h stub is sufficient for the desktop audio build.
// This harness does not participate in the Wii U application's runtime.

#include "ui.hpp"
#include "input.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Capture {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

Capture capture(const std::filesystem::path& filename) {
    SDL_Window* window = SDL_GetWindowFromID(1);
    SDL_Renderer* renderer = window ? SDL_GetRenderer(window) : nullptr;
    Capture result;
    if (!renderer || SDL_GetRendererOutputSize(renderer, &result.width, &result.height) != 0)
        throw std::runtime_error("Cannot locate actual UI renderer: " + std::string(SDL_GetError()));
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, result.width, result.height,
                                                         32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) throw std::runtime_error(SDL_GetError());
    if (SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                             surface->pixels, surface->pitch) != 0 ||
        IMG_SavePNG(surface, filename.string().c_str()) != 0) {
        const std::string error = std::string(SDL_GetError()) + " " + IMG_GetError();
        SDL_FreeSurface(surface);
        throw std::runtime_error("Capture failed: " + error);
    }
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
    for (int y = 0; y < result.height; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch);
        std::copy_n(row, result.width, result.pixels.begin() + y * result.width);
    }
    SDL_FreeSurface(surface);
    std::cout << filename.string() << '\n';
    return result;
}

std::size_t differences(const Capture& before, const Capture& after,
                        int logicalTop = 0, int logicalBottom = 720) {
    if (before.width != after.width || before.height != after.height)
        throw std::runtime_error("Capture dimensions changed unexpectedly");
    std::size_t changed = 0;
    for (int y = logicalTop * before.height / 720;
         y < logicalBottom * before.height / 720; ++y) {
        for (int x = 0; x < before.width; ++x) {
            const auto index = static_cast<std::size_t>(y) * before.width + x;
            changed += before.pixels[index] != after.pixels[index];
        }
    }
    return changed;
}

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: render-download-wait ABSOLUTE_OUTPUT_DIRECTORY\n";
        return 2;
    }
    const std::filesystem::path output = argv[1];
    std::filesystem::create_directories(output);
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    unbound::Ui ui;
    try {
        std::string error;
        require(ui.initialize(error), ("Actual UI initializes: " + error).c_str());
        const std::string title = "Downloading Unbound 0.2.0 full release (1/1)";
        const std::string detail = "296.0 MB of 1.20 GB\n"
                                   "Time remaining: 9m 18s  |  1.56 MiB/s";
        ui.loading(title, detail, 24, true);
        const Capture initial = capture(output / "download-tv-0000ms.png");
        const auto background = initial.pixels[500 * initial.width];
        bool outerSlotsClear = true;
        for (const int left : {396, 756}) {
            for (int y = 500; y < 628; ++y) for (int x = left; x < left + 128; ++x)
                outerSlotsClear = outerSlotsClear && initial.pixels[y * initial.width + x] == background;
        }
        require(outerSlotsClear, "Only the originally centered download clip remains visible");

        // Hold Cancel across independent animation redraws. The hit map must
        // retain its identity rather than silently cancelling the held press.
        ui.touchAction({true, false, 0, 0});
        ui.touchAction({true, true, 640, 686});
        SDL_Delay(120);
        ui.animateDownloadWaiting();
        const Capture first = capture(output / "download-tv-0120ms.png");
        require(differences(initial, first, 500, 628) > 0,
                "Independent wait-loop ticks advance the real sprites");
        require(differences(initial, first, 0, 500) == 0 &&
                differences(initial, first, 628, 720) == 0,
                "Animation redraw preserves status text, progress and footer");
        SDL_Delay(630);
        ui.animateDownloadWaiting();
        const Capture second = capture(output / "download-tv-0750ms.png");
        require(differences(first, second, 500, 628) > 0,
                "Original frame deadlines continue after a quiet transfer interval");
        SDL_Delay(1100);
        ui.animateDownloadWaiting();
        capture(output / "download-tv-1850ms.png");
        const auto action = ui.touchAction({true, false, 0, 0});
        require(action && action->kind == unbound::TouchActionKind::Button &&
                action->value == static_cast<int>(unbound::button::B),
                "Cancel touch stays usable while the animations run");

        ui.loading("Downloading Unbound 0.2.0P1.123456789-beta.123456789 full release "
                   "for Europe (Spanish/Italian) (12/12)",
                   "1.10 GB of 1.20 GB\nTime remaining: 1m 12s  |  1.56 MiB/s\n"
                   "Download is still active. Verifying received data while writing to SD.",
                   91, true);
        capture(output / "download-long-title-detail.png");

        const std::string veryLongDetail =
            "1.10 GB of 1.20 GB\nTime remaining: 1m 12s  |  1.56 MiB/s\n"
            "The connection is active and the downloaded data is being verified.\n"
            "This diagnostic intentionally uses a long multi-line status.\n"
            "Downloaded files are written to the SD cache before installation.\n"
            "Please keep the SD card connected until this operation finishes.\n"
            "A retry will use the same package and destination.\n"
            "Package destination: USB storage, USA, english content.\n"
            "The installed version marker changes only after a successful install.\n"
            "Additional recovery information remains readable in the status card.\n"
            "The long status takes priority over optional download artwork.";
        ui.loading(title, veryLongDetail, 91, true);
        const Capture dense = capture(output / "download-dense-status.png");
        SDL_Delay(70);
        ui.animateDownloadWaiting();
        const Capture denseAfter = capture(output / "download-dense-status-after-tick.png");
        require(differences(dense, denseAfter) == 0,
                "Dense status text suppresses optional animations without overwriting text");

        ui.loading("Unpacking Unbound 0.2.0 full release (1/1)",
                   "348.2 MB of 2.01 GB\nTime remaining: 1m 48s  |  16.2 MiB/s",
                   17, false);
        const Capture unpack = capture(output / "unpack-no-animation.png");
        SDL_Delay(70);
        ui.animateDownloadWaiting();
        const Capture unpackAfter = capture(output / "unpack-after-tick.png");
        require(differences(unpack, unpackAfter) == 0,
                "Unpacking never inherits the cancellable-download animation");

        ui.loading(title, detail, 24, true);
        ui.modal("Installation stopped",
                 "Package 1 of 1 stopped.\nThe diagnostic example failed safely.\n\n"
                 "Retry this package before launching Black Ops II. The installed "
                 "version marker was not advanced.", true);
        const Capture modal = capture(output / "error-modal.png");
        SDL_Delay(90);
        ui.animateDownloadWaiting();
        const Capture modalAfter = capture(output / "error-modal-after-tick.png");
        require(differences(modal, modalAfter) == 0,
                "Error modal immediately deactivates download animation redraws");

        SDL_Window* window = SDL_GetWindowFromID(1);
        require(window != nullptr, "Actual UI window remains available");
        SDL_SetWindowSize(window, 854, 480);
        ui.loading(title, detail, 24, true);
        const Capture gamepad = capture(output / "download-gamepad-0000ms.png");
        require(gamepad.width == 854 && gamepad.height == 480,
                "GamePad capture uses the physical 854 x 480 output size");
        SDL_Delay(180);
        ui.animateDownloadWaiting();
        const Capture gamepadAfter = capture(output / "download-gamepad-0180ms.png");
        require(differences(gamepad, gamepadAfter, 500, 628) > 0,
                "Animation remains active after GamePad scaling");

        double totalMs = 0.0;
        double slowestMs = 0.0;
        constexpr int frames = 20;
        for (int frame = 0; frame < frames; ++frame) {
            SDL_Delay(36);
            const Uint64 started = SDL_GetPerformanceCounter();
            ui.animateDownloadWaiting();
            const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - started) *
                                   1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
            totalMs += elapsed;
            slowestMs = std::max(slowestMs, elapsed);
        }
        std::cout << "Desktop SDL software redraw: mean=" << totalMs / frames
                  << "ms max=" << slowestMs << "ms; 20 due frames, capture I/O excluded.\n";
        ui.shutdown();
        std::cout << "Actual UI download-animation render validation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        ui.shutdown();
        return 1;
    }
}
