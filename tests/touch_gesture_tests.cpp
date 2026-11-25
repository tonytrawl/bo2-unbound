#include "touch.hpp"

#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <iostream>

using namespace unbound;

namespace {

constexpr TouchSample Up{true, false, 0, 0};
constexpr std::array<TouchTarget, 2> Targets = {{
    {{100, 100, 200, 80}, TouchActionKind::HomeAction, 4},
    {{320, 100, 80, 80}, TouchActionKind::Sidebar, 1},
}};

TouchSample down(int x = 120, int y = 120) {
    return {true, true, x, y};
}

void arm(TapGesture& gesture) {
    assert(!gesture.update(Up, Targets, 1));
}

void initialContactAndSingleRelease() {
    TapGesture gesture;
    // A finger already down at startup must not activate anything on its lift.
    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    assert(!gesture.update(down(), Targets, 1));
    for (int i = 0; i < 200; ++i)
        assert(!gesture.update(down(), Targets, 1));
    const auto action = gesture.update(Up, Targets, 1);
    assert((action == TouchAction{TouchActionKind::HomeAction, 4}));
    for (int i = 0; i < 200; ++i)
        assert(!gesture.update(Up, Targets, 1));
    // A separate deliberate press/release can activate once again.
    assert(!gesture.update(down(), Targets, 1));
    assert((gesture.update(Up, Targets, 1) ==
            TouchAction{TouchActionKind::HomeAction, 4}));
}

void movementBoundAndTargetEdges() {
    TapGesture gesture;
    arm(gesture);
    assert(!gesture.update(down(130, 130), Targets, 1));
    assert(!gesture.update(down(148, 130), Targets, 1));
    // Exactly the 18px radius permits normal resistive-screen jitter.
    assert(gesture.update(Up, Targets, 1));
    assert(!gesture.update(down(130, 130), Targets, 1));
    assert(!gesture.update(down(148, 131), Targets, 1));
    assert(!gesture.update(down(130, 130), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    // A small movement crossing a target's edge also cancels permanently.
    assert(!gesture.update(down(298, 130), Targets, 1));
    assert(!gesture.update(down(301, 130), Targets, 1));
    assert(!gesture.update(down(298, 130), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    // Starting outside and sliding onto a control is never a tap.
    assert(!gesture.update(down(95, 120), Targets, 1));
    assert(!gesture.update(down(105, 120), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    // A wide drag cannot activate the different control on its release.
    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update(down(350, 130), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
}

void invalidDisconnectAndResetBlock() {
    TapGesture gesture;
    arm(gesture);
    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update({}, Targets, 1));
    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    assert(!gesture.update(down(), Targets, 1));
    assert(gesture.update(Up, Targets, 1));

    assert(!gesture.update(down(), Targets, 1));
    gesture.reset();
    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    assert(!gesture.update(down(), Targets, 1));
    assert(gesture.update(Up, Targets, 1));

    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update(down(-1, 100), Targets, 1));
    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    assert(!gesture.update(down(1280, 120), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
    assert(!gesture.update(down(120, 720), Targets, 1));
    assert(!gesture.update(Up, Targets, 1));
}

void pageAndGeometryChangesCannotRetarget() {
    TapGesture gesture;
    arm(gesture);
    assert(!gesture.update(down(), Targets, 1));
    assert(!gesture.update(down(), Targets, 2));
    assert(!gesture.update(down(), Targets, 2));
    assert(!gesture.update(Up, Targets, 2));
    assert(!gesture.update(down(), Targets, 2));
    assert(gesture.update(Up, Targets, 2));

    auto changed = Targets;
    changed[0].value = 1;
    assert(!gesture.update(down(), Targets, 2));
    // Even an accidental missed revision increment cannot activate a different
    // kind/value/rectangle: complete captured target equality is checked.
    assert(!gesture.update(Up, changed, 2));
    changed[0] = Targets[0];
    changed[0].rect.w += 5;
    assert(!gesture.update(down(), Targets, 2));
    assert(!gesture.update(Up, changed, 2));
    assert(!gesture.update(down(), Targets, 2));
    assert(!gesture.update(Up, std::span<const TouchTarget>{}, 2));
    assert(!gesture.update(down(), Targets, 2));
    assert(!gesture.update(Up, Targets, 3));
    assert(!gesture.update(down(), Targets, 3));
    assert(gesture.update(Up, Targets, 3));
}

void meaningfulReleasePositionAndTopmostTarget() {
    TapGesture gesture;
    arm(gesture);
    assert(!gesture.update(down(), Targets, 1));
    // Ignore even out-of-range coordinates once the device reports no contact.
    assert(gesture.update({true, false, INT_MIN, INT_MAX}, Targets, 1));
    const std::array<TouchTarget, 2> layered = {{
        {{100, 100, 200, 80}, TouchActionKind::InstallRow, 0},
        {{110, 110, 40, 40}, TouchActionKind::InstallChoice, 1},
    }};
    assert(!gesture.update(down(), layered, 2));
    assert((gesture.update(Up, layered, 2) ==
            TouchAction{TouchActionKind::InstallChoice, 1}));
}

void calibrationValidationAndRectangleBounds() {
    const auto topLeft = calibratedTouchSample(true, true, 0, 0);
    assert(topLeft.valid && topLeft.touching && topLeft.x == 0 && topLeft.y == 0);
    const auto bottomRight = calibratedTouchSample(true, true, 1279, 719);
    assert(bottomRight.valid && bottomRight.x == 1279 && bottomRight.y == 719);
    assert(!calibratedTouchSample(false, true, 120, 120).valid);
    assert(!calibratedTouchSample(true, true, -1, 0).valid);
    assert(!calibratedTouchSample(true, true, 1280, 0).valid);
    assert(!calibratedTouchSample(true, true, 0, 720).valid);
    const auto release = calibratedTouchSample(false, false, 65535, 65535);
    assert(release.valid && !release.touching);

    const TouchRect rectangle{100, 100, 200, 80};
    assert(rectangle.contains(100, 100));
    assert(rectangle.contains(299, 179));
    assert(!rectangle.contains(300, 179));
    assert(!rectangle.contains(299, 180));
    assert(!rectangle.contains(99, 100));
    assert(!TouchRect{}.contains(0, 0));
    assert(!(TouchRect{0, 0, -5, 10}.contains(0, 0)));
    assert(!(TouchRect{INT_MIN, 0, INT_MAX, 10}.contains(INT_MAX, 0)));
}

void missingSamplesRetentionTimeoutAndRollover() {
    TouchSampleRetention retained;
    assert(!retained.noSamples(1).valid);
    assert(retained.acquired(down(), 100).valid);
    assert(retained.noSamples(200).valid);
    assert(retained.noSamples(200).touching);
    assert(!retained.noSamples(201).valid);
    retained.reset();
    assert(!retained.noSamples(100).valid);

    constexpr auto nearWrap = UINT32_MAX - 50;
    retained.acquired(down(), nearWrap);
    assert(retained.noSamples(49).valid);
    assert(!retained.noSamples(50).valid);
    retained.acquired({}, 60);
    assert(!retained.noSamples(60).valid);
    retained.acquired(Up, 1000);
    assert(retained.noSamples(1001).valid);
    assert(!retained.noSamples(1001).touching);

    TapGesture gesture;
    arm(gesture);
    retained.acquired(down(), 2000);
    assert(!gesture.update(retained.noSamples(2000), Targets, 1));
    assert(!gesture.update(retained.noSamples(2101), Targets, 1));
    assert(!gesture.update(retained.acquired(down(), 2102), Targets, 1));
    assert(!gesture.update(retained.acquired(Up, 2103), Targets, 1));
    assert(!gesture.update(retained.acquired(down(), 2104), Targets, 1));
    assert(gesture.update(retained.acquired(Up, 2105), Targets, 1));
}

}

int main() {
    initialContactAndSingleRelease();
    movementBoundAndTargetEdges();
    invalidDisconnectAndResetBlock();
    pageAndGeometryChangesCannotRetarget();
    meaningfulReleasePositionAndTopmostTarget();
    calibrationValidationAndRectangleBounds();
    missingSamplesRetentionTimeoutAndRollover();
    std::cout << "Calibrated touch, release-only taps, drag rejection, transition guards and retention tests passed\n";
}
