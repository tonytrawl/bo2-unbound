// Desktop-only capture of the real error modal and its Download Fresh action.
// Uses the packaged assets, real Ui implementation and ordinary SDL hit maps.
#include "ui.hpp"
#include "input.hpp"
#include <SDL2/SDL_image.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void capture(const std::filesystem::path& path) {
    SDL_Window* window = SDL_GetWindowFromID(1);
    SDL_Renderer* renderer = window ? SDL_GetRenderer(window) : nullptr;
    int width = 0, height = 0;
    if (!renderer || SDL_GetRendererOutputSize(renderer, &width, &height) != 0)
        throw std::runtime_error("Actual UI renderer is unavailable");
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                         SDL_PIXELFORMAT_RGBA32);
    if (!surface) throw std::runtime_error(SDL_GetError());
    const bool ok = SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
                                         surface->pixels, surface->pitch) == 0 &&
                    IMG_SavePNG(surface, path.string().c_str()) == 0;
    SDL_FreeSurface(surface);
    if (!ok) throw std::runtime_error(SDL_GetError());
    std::cout << path.string() << '\n';
}

bool tapButton(unbound::Ui& ui, int x, int y, std::uint32_t expected) {
    ui.touchAction({true, false, 0, 0});
    ui.touchAction({true, true, x, y});
    const auto action = ui.touchAction({true, false, 0, 0});
    return action && action->kind == unbound::TouchActionKind::Button &&
           action->value == static_cast<int>(expected);
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
        if (!ui.initialize(error)) throw std::runtime_error(error);
        const std::string body =
            "The installed version no longer matches the unfinished installation.\n\n"
            "Press A to download Unbound 0.2.0 from the beginning and reinstall the "
            "full release with its required patches.\n\nDestination: USB storage\n"
            "Language: english\n\n1 package: Unbound 0.2.0 full release\n"
            "Total download: 1.20 GB\n\nSaved downloads for these packages will be "
            "cleared. Keep the game closed until installation finishes.";
        ui.modal("Installation stopped", body, true, "DOWNLOAD FRESH", 0, true);
        capture(output / "download-fresh-top-tv.png");
        if (!tapButton(ui, 160, 686, unbound::button::A) ||
            !tapButton(ui, 1120, 686, unbound::button::B))
            throw std::runtime_error("Default A or Cancel B modal touch action failed");
        const std::string longBody = body +
            "\n\nFull release: 1.20 GB\nRequired patch 1: 18.0 MB\n"
            "Required patch 2: 12.0 MB\nRequired patch 3: 24.0 MB\n"
            "All required packages will be installed in their saved order.\n"
            "The game must stay closed until the full chain finishes.";
        const int maximum = ui.modal("Installation stopped", longBody, true, "DOWNLOAD FRESH", 0, true);
        if (maximum <= 0) throw std::runtime_error("Long recovery message did not scroll");
        capture(output / "download-fresh-long-top-tv.png");
        ui.modal("Installation stopped", longBody, true, "DOWNLOAD FRESH", maximum, true);
        capture(output / "download-fresh-long-bottom-tv.png");
        if (SDL_Window* window = SDL_GetWindowFromID(1)) SDL_SetWindowSize(window, 854, 480);
        ui.modal("Installation stopped", body, true, "DOWNLOAD FRESH", 0, true);
        capture(output / "download-fresh-top-gamepad.png");
        ui.modal("Installation stopped", longBody, true, "DOWNLOAD FRESH", maximum, true);
        capture(output / "download-fresh-long-bottom-gamepad.png");
        ui.shutdown();
        std::cout << "Actual recovery modal, scroll, A Download Fresh and B Cancel validation passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ui.shutdown();
        return 1;
    }
}
