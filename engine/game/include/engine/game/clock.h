#pragma once

// The fixed-timestep clock (plan/03 §7): the simulation always advances in equal steps, so
// behaviour does not depend on the frame rate and recorded input replays exactly. Rendering
// interpolates between the last two steps.

#include <cstdint>

namespace engine::game {

class FixedStepClock {
public:
    /// `step_seconds` is the length of one simulation step (1/60 by default). At most
    /// `max_steps` run per frame: after a long stall the simulation slows down for a moment
    /// rather than trying to catch up all at once and stalling again.
    explicit FixedStepClock(double step_seconds = 1.0 / 60.0, std::uint32_t max_steps = 8);

    struct Frame {
        std::uint32_t steps = 0; ///< How many simulation steps to run now.
        double alpha = 0.0;      ///< How far between the last two steps to draw, 0 to 1.
        double dropped = 0.0;    ///< Seconds skipped because of `max_steps`.
    };

    /// Adds the real time that passed since the last frame. Negative or non-finite times
    /// count as zero.
    Frame advance(double seconds);

    [[nodiscard]] double step_seconds() const noexcept { return step_; }
    /// Steps run since the clock started: the step number for input recording.
    [[nodiscard]] std::uint64_t step_count() const noexcept { return count_; }

private:
    double step_;
    std::uint32_t max_steps_;
    double accumulator_ = 0.0;
    std::uint64_t count_ = 0;
};

} // namespace engine::game
