#include "engine/game/clock.h"

#include <cmath>

namespace engine::game {

FixedStepClock::FixedStepClock(double step_seconds, std::uint32_t max_steps)
    : step_(std::isfinite(step_seconds) && step_seconds > 0.0 ? step_seconds : 1.0 / 60.0),
      max_steps_(max_steps == 0 ? 1 : max_steps) {}

FixedStepClock::Frame FixedStepClock::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    accumulator_ += seconds;
    Frame frame;
    // A tiny tolerance so that, for example, three frames of exactly 1/60 s give three
    // steps despite rounding in the sum.
    constexpr double tolerance = 1e-9;
    while (accumulator_ + tolerance >= step_ && frame.steps < max_steps_) {
        accumulator_ -= step_;
        ++frame.steps;
    }
    if (accumulator_ + tolerance >= step_) {
        // Still behind after max_steps: drop the backlog, keeping the partial step.
        const double whole = std::floor((accumulator_ + tolerance) / step_);
        frame.dropped = whole * step_;
        accumulator_ -= frame.dropped;
    }
    if (accumulator_ < 0.0) {
        accumulator_ = 0.0;
    }
    count_ += frame.steps;
    frame.alpha = accumulator_ / step_;
    return frame;
}

} // namespace engine::game
