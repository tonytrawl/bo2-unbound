#include "input.hpp"

#include <padscore/kpad.h>
#include <vpad/input.h>
#include <SDL2/SDL.h>

#include <array>
#include <cstdint>

namespace unbound {
namespace {

bool proControllerSupportWasEnabled = true;
TouchSampleRetention touchSamples;

std::uint32_t mapVpad(std::uint32_t value) {
    std::uint32_t mapped = 0;
    if (value & VPAD_BUTTON_A) mapped |= button::A;
    if (value & VPAD_BUTTON_B) mapped |= button::B;
    if (value & VPAD_BUTTON_X) mapped |= button::X;
    if (value & VPAD_BUTTON_Y) mapped |= button::Y;
    if (value & VPAD_BUTTON_L) mapped |= button::L;
    if (value & VPAD_BUTTON_PLUS) mapped |= button::Plus;
    if (value & VPAD_BUTTON_MINUS) mapped |= button::Minus;
    if (value & (VPAD_BUTTON_LEFT | VPAD_STICK_L_EMULATION_LEFT))
        mapped |= button::Left;
    if (value & (VPAD_BUTTON_RIGHT | VPAD_STICK_L_EMULATION_RIGHT))
        mapped |= button::Right;
    if (value & (VPAD_BUTTON_UP | VPAD_STICK_L_EMULATION_UP))
        mapped |= button::Up;
    if (value & (VPAD_BUTTON_DOWN | VPAD_STICK_L_EMULATION_DOWN))
        mapped |= button::Down;
    return mapped;
}

std::uint32_t mapWiiRemote(std::uint32_t value) {
    std::uint32_t mapped = 0;
    if (value & WPAD_BUTTON_A) mapped |= button::A;
    if (value & WPAD_BUTTON_B) mapped |= button::B;
    // Give the button-limited Wii Remote access to HQ's X/delete action.
    if (value & WPAD_BUTTON_1) mapped |= button::X;
    if (value & WPAD_BUTTON_2) mapped |= button::Y;
    if (value & WPAD_BUTTON_PLUS) mapped |= button::Plus;
    if (value & WPAD_BUTTON_MINUS) mapped |= button::Minus;
    if (value & WPAD_BUTTON_LEFT) mapped |= button::Left;
    if (value & WPAD_BUTTON_RIGHT) mapped |= button::Right;
    if (value & WPAD_BUTTON_UP) mapped |= button::Up;
    if (value & WPAD_BUTTON_DOWN) mapped |= button::Down;
    return mapped;
}

std::uint32_t mapClassic(std::uint32_t value) {
    std::uint32_t mapped = 0;
    if (value & WPAD_CLASSIC_BUTTON_A) mapped |= button::A;
    if (value & WPAD_CLASSIC_BUTTON_B) mapped |= button::B;
    if (value & WPAD_CLASSIC_BUTTON_X) mapped |= button::X;
    if (value & WPAD_CLASSIC_BUTTON_Y) mapped |= button::Y;
    if (value & WPAD_CLASSIC_BUTTON_L) mapped |= button::L;
    if (value & WPAD_CLASSIC_BUTTON_PLUS) mapped |= button::Plus;
    if (value & WPAD_CLASSIC_BUTTON_MINUS) mapped |= button::Minus;
    if (value & (WPAD_CLASSIC_BUTTON_LEFT |
                 WPAD_CLASSIC_STICK_L_EMULATION_LEFT))
        mapped |= button::Left;
    if (value & (WPAD_CLASSIC_BUTTON_RIGHT |
                 WPAD_CLASSIC_STICK_L_EMULATION_RIGHT))
        mapped |= button::Right;
    if (value & (WPAD_CLASSIC_BUTTON_UP |
                 WPAD_CLASSIC_STICK_L_EMULATION_UP))
        mapped |= button::Up;
    if (value & (WPAD_CLASSIC_BUTTON_DOWN |
                 WPAD_CLASSIC_STICK_L_EMULATION_DOWN))
        mapped |= button::Down;
    return mapped;
}

std::uint32_t mapPro(std::uint32_t value) {
    std::uint32_t mapped = 0;
    if (value & WPAD_PRO_BUTTON_A) mapped |= button::A;
    if (value & WPAD_PRO_BUTTON_B) mapped |= button::B;
    if (value & WPAD_PRO_BUTTON_X) mapped |= button::X;
    if (value & WPAD_PRO_BUTTON_Y) mapped |= button::Y;
    if (value & WPAD_PRO_BUTTON_L) mapped |= button::L;
    if (value & WPAD_PRO_BUTTON_PLUS) mapped |= button::Plus;
    if (value & WPAD_PRO_BUTTON_MINUS) mapped |= button::Minus;
    if (value & (WPAD_PRO_BUTTON_LEFT | WPAD_PRO_STICK_L_EMULATION_LEFT))
        mapped |= button::Left;
    if (value & (WPAD_PRO_BUTTON_RIGHT | WPAD_PRO_STICK_L_EMULATION_RIGHT))
        mapped |= button::Right;
    if (value & (WPAD_PRO_BUTTON_UP | WPAD_PRO_STICK_L_EMULATION_UP))
        mapped |= button::Up;
    if (value & (WPAD_PRO_BUTTON_DOWN | WPAD_PRO_STICK_L_EMULATION_DOWN))
        mapped |= button::Down;
    return mapped;
}

void mergeKpadStatus(AppInput& input, const KPADStatus& status) {
    switch (status.extensionType) {
    case WPAD_EXT_CORE:
    case WPAD_EXT_NUNCHUK:
    case WPAD_EXT_MPLUS:
    case WPAD_EXT_MPLUS_NUNCHUK:
        input.buttons |= mapWiiRemote(status.trigger);
        input.held |= mapWiiRemote(status.hold);
        break;
    case WPAD_EXT_PRO_CONTROLLER:
        input.buttons |= mapPro(status.pro.trigger);
        input.held |= mapPro(status.pro.hold);
        break;
    case WPAD_EXT_CLASSIC:
    case WPAD_EXT_MPLUS_CLASSIC:
        input.buttons |= mapClassic(status.classic.trigger);
        input.held |= mapClassic(status.classic.hold);
        break;
    default:
        break;
    }
}

constexpr std::array<KPADChan, 7> ControllerChannels = {
    WPAD_CHAN_0, WPAD_CHAN_1, WPAD_CHAN_2, WPAD_CHAN_3,
    WPAD_CHAN_4, WPAD_CHAN_5, WPAD_CHAN_6};

}

void initializeInput() {
    touchSamples.reset();
    VPADInit();
    KPADInit();
    // Some titles or environments leave Wii U Pro Controller support disabled.
    // Enable it while HQ owns input, then restore the previous system state on
    // shutdown instead of leaking that setting into the next application.
    proControllerSupportWasEnabled = WPADIsEnabledURC() == TRUE;
    if (!proControllerSupportWasEnabled)
        WPADEnableURCC(TRUE);
    // Request the API's full channel set so any connected controller may drive HQ.
    KPADSetMaxControllers(7);
}

void shutdownInput() {
    touchSamples.reset();
    if (!proControllerSupportWasEnabled)
        WPADEnableURCC(FALSE);
    KPADShutdown();
    VPADShutdown();
}

AppInput readInput() {
    AppInput input;

    VPADStatus vpad{};
    VPADReadError vpadError{};
    if (VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError) > 0 &&
        vpadError == VPAD_READ_SUCCESS) {
        input.buttons |= mapVpad(vpad.trigger);
        input.held |= mapVpad(vpad.hold);
        VPADTouchData calibrated{};
        VPADGetTPCalibratedPointEx(VPAD_CHAN_0, VPAD_TP_1280X720,
                                 &calibrated, &vpad.tpNormal);
        input.touch = touchSamples.acquired(
            calibratedTouchSample(calibrated.validity == VPAD_VALID,
                                  calibrated.touched != 0,
                                  calibrated.x, calibrated.y),
            SDL_GetTicks());
    } else if (vpadError == VPAD_READ_NO_SAMPLES) {
        input.touch = touchSamples.noSamples(SDL_GetTicks());
    } else {
        // A failed/disconnected read must cancel the pending tap, not synthesize
        // a release which could accidentally activate a control.
        touchSamples.reset();
    }

    for (const auto channel : ControllerChannels) {
        KPADStatus kpad{};
        KPADError kpadError{};
        if (KPADReadEx(channel, &kpad, 1, &kpadError) > 0 &&
            kpadError == KPAD_ERROR_OK)
            mergeKpadStatus(input, kpad);
    }

    return input;
}

}
