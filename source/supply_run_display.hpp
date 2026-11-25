#pragma once

#include <algorithm>
#include <cstdint>

namespace unbound {

class SupplyRunDisplay {
public:
    static constexpr std::uint32_t TransitionMilliseconds = 300;

    bool enabled() const { return enabled_; }

    void restore(bool enabled) {
        enabled_ = enabled;
        from_ = enabled ? 1.0f : 0.0f;
        started_ = 0;
        animated_ = false;
    }

    float amount(std::uint32_t now) const {
        if (!animated_) return enabled_ ? 1.0f : 0.0f;
        const float time = std::min(1.0f, float(now - started_) / TransitionMilliseconds);
        const float eased = time * time * (3.0f - 2.0f * time);
        return from_ + ((enabled_ ? 1.0f : 0.0f) - from_) * eased;
    }

    bool transitioning(std::uint32_t now) const {
        return animated_ && now - started_ < TransitionMilliseconds;
    }

    void toggle(std::uint32_t now) {
        from_ = amount(now);
        enabled_ = !enabled_;
        started_ = now;
        animated_ = true;
    }

private:
    bool enabled_ = true;
    bool animated_ = false;
    float from_ = 1.0f;
    std::uint32_t started_ = 0;
};

} // namespace unbound
