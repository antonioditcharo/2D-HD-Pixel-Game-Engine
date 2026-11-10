#include "engine/game/clock.h"

#include <doctest/doctest.h>

#include <limits>

namespace game = engine::game;

TEST_CASE("The clock runs whole steps and reports how far into the next one it is") {
    game::FixedStepClock clock(0.25);
    game::FixedStepClock::Frame frame = clock.advance(0.6);
    CHECK(frame.steps == 2);
    CHECK(frame.alpha == doctest::Approx(0.4));
    frame = clock.advance(0.15);
    CHECK(frame.steps == 1); // 0.1 left over + 0.15
    CHECK(frame.alpha == doctest::Approx(0.0));
    CHECK(clock.step_count() == 3);
}

TEST_CASE("Exact frames give exact steps despite rounding") {
    game::FixedStepClock clock; // 1/60 s
    int steps = 0;
    for (int i = 0; i < 600; ++i) {
        steps += static_cast<int>(clock.advance(1.0 / 60.0).steps);
    }
    CHECK(steps == 600);
}

TEST_CASE("A long stall runs at most max_steps, then drops the backlog") {
    game::FixedStepClock clock(0.1, 4);
    const game::FixedStepClock::Frame frame = clock.advance(2.05);
    CHECK(frame.steps == 4);
    CHECK(frame.dropped == doctest::Approx(1.6));
    CHECK(frame.alpha == doctest::Approx(0.5));
    CHECK(clock.advance(0.0).steps == 0);
}

TEST_CASE("Bad times count as zero") {
    game::FixedStepClock clock(0.1);
    CHECK(clock.advance(-1.0).steps == 0);
    CHECK(clock.advance(std::numeric_limits<double>::infinity()).steps == 0);
    CHECK(clock.advance(std::numeric_limits<double>::quiet_NaN()).steps == 0);
    CHECK(clock.advance(0.1).steps == 1);
}
