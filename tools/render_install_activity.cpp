// Desktop-only validation against the real Ui::loading and InstallActivity.
// Build with ui.cpp/model.cpp/background_music.cpp and SDL2/image/ttf.
// Run beside fs:/vol/content -> project/content, passing an output directory.
#include "ui.hpp"
#include "install_activity.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct Track {
    int grey = 0;
    int orange = 0;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}

Track capture(const std::filesystem::path& path) {
    SDL_Window* window = SDL_GetWindowFromID(1);
    SDL_Renderer* renderer = window ? SDL_GetRenderer(window) : nullptr;
    int width = 0, height = 0;
    require(renderer && SDL_GetRendererOutputSize(renderer, &width, &height) == 0,
            "Actual UI renderer remains available");
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                         SDL_PIXELFORMAT_RGBA32);
    if (!surface) throw std::runtime_error(SDL_GetError());
    if (SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                             surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        throw std::runtime_error(SDL_GetError());
    }
    Track track;
    if (width == 1280 && height == 720) {
        const Uint32 grey = SDL_MapRGBA(surface->format, 43, 49, 53, 255);
        const Uint32 orange = SDL_MapRGBA(surface->format, 255, 92, 0, 255);
        for (int y = 250; y < 650; ++y) {
            const auto* row = reinterpret_cast<const Uint32*>(
                static_cast<const unsigned char*>(surface->pixels) + y * surface->pitch);
            Track candidate;
            for (int x = 274; x < 1196; ++x) {
                candidate.grey += row[x] == grey;
                candidate.orange += row[x] == orange;
            }
            // Rounded corners make the widest grey and orange spans occur
            // on different rows. Measure each span independently.
            track.grey = std::max(track.grey, candidate.grey);
            track.orange = std::max(track.orange, candidate.orange);
        }
    }
    const bool saved = IMG_SavePNG(surface, path.string().c_str()) == 0;
    SDL_FreeSurface(surface);
    require(saved, "Actual rendered frame saved");
    std::cout << path.string() << '\n';
    return track;
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
        require(ui.initialize(error), "Real UI initializes with packaged fonts and artwork");
        const std::string label = "Unbound 0.2.0 base (1/1)";
        const std::string path = "storage_usb01:/usr/title/0005000e/1010cf00/content/english/sound/loaded";
        const auto show = [&](const unbound::InstallActivitySnapshot& frame) {
            ui.loading(frame.title + ": " + label, frame.detail, frame.percent, false);
        };

        unbound::InstallActivity opening(1000);
        opening.observe("Opening package", "Reading the ZIP directory before installation...", -1, 1000);
        show(opening.snapshot(1000));
        auto track = capture(output / "opening-package-tv.png");
        require(track.grey >= 916 && track.orange == 0,
                "Unknown package-open progress has no fabricated orange percentage fill");

        unbound::InstallActivity preparing(1000);
        preparing.observe("Preparing destination folders", "Destination 39 of 105\nstat: " + path, -1, 3000);
        const auto current = preparing.snapshot(5000);
        require(current.percent == -1 && current.detail.find("Elapsed 00:04") != std::string::npos,
                "Directory activity preserves unknown percentage and real elapsed times");
        show(current);
        track = capture(output / "preparing-destination-tv.png");
        require(track.grey >= 916 && track.orange == 0,
                "Directory preflight has an empty track instead of 0% or a fake 35% fill");

        const auto waiting = preparing.snapshot(19000);
        require(waiting.detail.find("This step is taking longer. Keep the console on.") != std::string::npos,
                "Silent operation gets an honest 15-second wait notice");
        show(waiting);
        capture(output / "preparing-destination-wait-tv.png");

        unbound::InstallActivity longPath(1000);
        longPath.observe("Preparing destination folders", "Destination 104 of 105\nstat: " + path +
            "/unbound_nuketown_zombies_localized_environmental_audio_variant/streamed_effects_and_ambient_sounds", -1, 3000);
        const auto longWaiting = longPath.snapshot(19000);
        show(longWaiting);
        capture(output / "preparing-long-path-wait-tv.png");

        unbound::InstallActivity unpack(1000);
        const std::string measured = "Extracting content/english/sound/loaded/nuketown.sabs\n"
            "Time remaining: 1m 48s  |  16.2 MiB/s  |  794.0 MB of 2.15 GB";
        unpack.observe("Unpacking and installing", measured, 37, 39000);
        const auto actual = unpack.snapshot(39000);
        require(actual.percent == 37 && actual.detail.starts_with(measured),
                "Measured 37% progress, byte counts, speed and ETA remain authoritative");
        show(actual);
        track = capture(output / "unpacking-measured-37-tv.png");
        require(track.orange >= 337 && track.orange <= 345 && track.grey > 575,
                "Actual progress paints 37% of the 922-pixel track");

        SDL_Window* window = SDL_GetWindowFromID(1);
        require(window != nullptr, "Actual UI window remains available");
        SDL_SetWindowSize(window, 854, 480);
        show(waiting);
        capture(output / "preparing-destination-wait-gamepad.png");
        show(longWaiting);
        capture(output / "preparing-long-path-wait-gamepad.png");
        show(actual);
        capture(output / "unpacking-measured-37-gamepad.png");
        ui.shutdown();
        std::cout << "Actual install-activity rendering, unknown progress and measured progress passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        ui.shutdown();
        return 1;
    }
}
