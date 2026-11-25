#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace unbound {

// VPADGetTPCalibratedPointEx supplies these coordinates in the same 1280 x 720
// logical space used by SDL, independently of the GamePad's physical pixels.
struct TouchSample {
    bool valid = false;
    bool touching = false;
    int x = 0;
    int y = 0;
};

struct TouchRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(int pointX, int pointY) const {
        // Use subtraction in the wider type to avoid overflow for malformed
        // rectangles. The right/bottom edge is exclusive like SDL's rectangles.
        const auto offsetX = static_cast<std::int64_t>(pointX) - x;
        const auto offsetY = static_cast<std::int64_t>(pointY) - y;
        return w > 0 && h > 0 && offsetX >= 0 && offsetY >= 0 &&
               offsetX < w && offsetY < h;
    }

    bool operator==(const TouchRect&) const = default;
};

enum class TouchActionKind {
    Button,
    Sidebar,
    HomeAction,
    GameChoice,
    InstallRow,
    // 0/1: previous/next destination. 2/3: previous/next language.
    InstallChoice,
    LocalModRow,
    MapPackRow,
    MapPackDetails,
    PluginRow,
    WorkshopTab,
    CustomMapRow,
    CustomMapAction,
    PackageEntry,
    PluginAction,
};

struct TouchTarget {
    TouchRect rect;
    TouchActionKind kind = TouchActionKind::Button;
    int value = 0;
    bool operator==(const TouchTarget&) const = default;
};

struct TouchAction {
    TouchActionKind kind = TouchActionKind::Button;
    int value = 0;
    bool operator==(const TouchAction&) const = default;
};

// A renderer publishes only currently visible, enabled targets. Its revision
// stays stable across redraws, and changes when the page, target identity,
// enabled state, scroll offset or geometry changes. reset() must also be called
// before a modal/operation takes control, including a transition without new
// geometry. A held finger cannot carry an action across either kind of change.
class TapGesture {
public:
    static constexpr int MaximumMovement = 18;

    void reset() {
        blocked_ = true;
        pressing_ = false;
        target_.reset();
    }

    std::optional<TouchAction> update(const TouchSample& sample,
                                     std::span<const TouchTarget> targets,
                                     std::uint64_t layoutRevision) {
        if (!sample.valid ||
            (sample.touching && (sample.x < 0 || sample.x >= 1280 ||
                                 sample.y < 0 || sample.y >= 720))) {
            reset();
            return std::nullopt;
        }

        if (blocked_) {
            // Initial use, invalid input and transitions all require an observed
            // release. A still-held contact is never mistaken for a new press.
            if (!sample.touching)
                blocked_ = false;
            return std::nullopt;
        }

        if (pressing_ && layoutRevision != revision_) {
            reset();
            if (!sample.touching)
                blocked_ = false;
            return std::nullopt;
        }

        if (sample.touching) {
            if (!pressing_) {
                pressing_ = true;
                revision_ = layoutRevision;
                startX_ = sample.x;
                startY_ = sample.y;
                target_ = findTarget(targets, sample.x, sample.y);
            }
            lastX_ = sample.x;
            lastY_ = sample.y;
            const auto deltaX = static_cast<std::int64_t>(sample.x) - startX_;
            const auto deltaY = static_cast<std::int64_t>(sample.y) - startY_;
            if (deltaX * deltaX + deltaY * deltaY >
                MaximumMovement * MaximumMovement)
                target_.reset();
            if (target_ && findTarget(targets, sample.x, sample.y) != target_)
                target_.reset();
            return std::nullopt;
        }

        if (!pressing_)
            return std::nullopt;
        pressing_ = false;
        // An up sample commonly has stale or zero coordinates. Hit test the
        // last accurate *touched* point instead of that meaningless position.
        const auto target = target_;
        target_.reset();
        if (target && findTarget(targets, lastX_, lastY_) == target)
            return TouchAction{target->kind, target->value};
        return std::nullopt;
    }

private:
    static std::optional<TouchTarget> findTarget(
        std::span<const TouchTarget> targets, int x, int y) {
        // Later draw registrations are on top of earlier ones. A modal or
        // individual arrow wins over the broad row behind it.
        for (auto it = targets.rbegin(); it != targets.rend(); ++it)
            if (it->rect.contains(x, y))
                return *it;
        return std::nullopt;
    }

    bool blocked_ = true;
    bool pressing_ = false;
    int startX_ = 0;
    int startY_ = 0;
    int lastX_ = 0;
    int lastY_ = 0;
    std::uint64_t revision_ = 0;
    std::optional<TouchTarget> target_;
};

// Validate calibrated down coordinates without inventing a touch at the nearest
// edge. For an up sample position/axis-validity has no meaning; successful VPAD
// acquisition itself is enough to observe the release and rearm the gesture.
inline TouchSample calibratedTouchSample(bool accurate, bool touching,
                                         int x, int y) {
    if (!touching)
        return {true, false, 0, 0};
    if (!accurate || x < 0 || x >= 1280 || y < 0 || y >= 720)
        return {};
    return {true, true, x, y};
}

// VPAD_READ_NO_SAMPLES means no new data, not a disconnect. Keep a recently
// acquired state briefly to avoid jitter, but never manufacture an up event or
// retain a stalled/disconnected contact indefinitely. unsigned subtraction also
// handles the SDL millisecond clock rolling over.
class TouchSampleRetention {
public:
    static constexpr std::uint32_t MaximumAgeMs = 100;

    TouchSample acquired(TouchSample sample, std::uint32_t now) {
        last_ = sample;
        acquiredAt_ = now;
        haveSample_ = sample.valid;
        return sample;
    }

    TouchSample noSamples(std::uint32_t now) const {
        return haveSample_ && now - acquiredAt_ <= MaximumAgeMs ? last_
                                                               : TouchSample{};
    }

    void reset() {
        haveSample_ = false;
        last_ = {};
    }

private:
    bool haveSample_ = false;
    TouchSample last_;
    std::uint32_t acquiredAt_ = 0;
};

}
