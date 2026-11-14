// Physics on Box2D, through the facade: falling, resting, bouncing, pushing, damping,
// contacts, hostile input and determinism.

#include "engine/physics/physics.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace physics = engine::physics;

namespace {

std::unique_ptr<physics::PhysicsWorld> make_world(double gravity_y) {
    auto world = physics::PhysicsWorld::create(0.0, gravity_y);
    REQUIRE(world != nullptr);
    return world;
}

/// A static floor 20 wide whose top is at y = 5.
physics::BodyId floor(physics::PhysicsWorld& world) {
    physics::BodyDef def;
    def.type = physics::BodyType::Static;
    def.x = 0.0;
    def.y = 5.5;
    def.width = 20.0;
    def.height = 1.0;
    def.user = 1;
    return world.add(def);
}

/// A body's state, which must exist.
physics::BodyState state_of(const physics::PhysicsWorld& world, physics::BodyId body) {
    const std::optional<physics::BodyState> state = world.state(body);
    REQUIRE(state.has_value());
    return state.value_or(physics::BodyState{});
}

void run(physics::PhysicsWorld& world, int steps) {
    for (int i = 0; i < steps; ++i) {
        world.step(1.0 / 60.0);
    }
}

} // namespace

TEST_CASE("A box falls under gravity and comes to rest on the floor, touching it once") {
    const auto world = make_world(10.0);
    floor(*world);
    physics::BodyDef crate;
    crate.y = 0.0;
    crate.user = 2;
    const physics::BodyId id = world->add(crate);
    REQUIRE(id.is_valid());
    CHECK(world->body_count() == 2);

    std::vector<physics::Contact> touches;
    for (int i = 0; i < 180; ++i) {
        world->step(1.0 / 60.0);
        touches.insert(touches.end(), world->contacts().begin(), world->contacts().end());
        if (i == 10) {
            // Falling: down the screen is +y, and it speeds up.
            const physics::BodyState falling = world->state(id).value_or(physics::BodyState{});
            CHECK(falling.velocity_y > 1.0);
            CHECK(falling.y > 0.0);
        }
    }
    const physics::BodyState rest = world->state(id).value_or(physics::BodyState{});
    CHECK(rest.y == doctest::Approx(4.5).epsilon(0.01)); // its bottom on the floor's top
    CHECK(std::abs(rest.velocity_y) < 0.01);
    CHECK(rest.angle == 0.0); // fixed rotation by default
    REQUIRE(touches.size() == 1);
    CHECK(((touches[0].a == 1 && touches[0].b == 2) || (touches[0].a == 2 && touches[0].b == 1)));
}

TEST_CASE("Bouncy balls bounce, kinematic bodies push, and damping slows things down") {
    SUBCASE("Bounce") {
        const auto world = make_world(10.0);
        floor(*world);
        physics::BodyDef ball;
        ball.shape = physics::ShapeKind::Circle;
        ball.radius = 0.5;
        ball.restitution = 1.0;
        const physics::BodyId id = world->add(ball);
        double lowest_velocity = 0.0;
        for (int i = 0; i < 90; ++i) {
            world->step(1.0 / 60.0);
            lowest_velocity = std::min(lowest_velocity, state_of(*world, id).velocity_y);
        }
        CHECK(lowest_velocity < -5.0); // it came back up, fast
    }
    SUBCASE("Push") {
        const auto world = make_world(0.0);
        physics::BodyDef pusher;
        pusher.type = physics::BodyType::Kinematic;
        pusher.x = 0.0;
        const physics::BodyId mover = world->add(pusher);
        physics::BodyDef crate;
        crate.x = 2.0;
        const physics::BodyId box = world->add(crate);
        world->set_velocity(mover, 2.0, 0.0);
        run(*world, 60);
        CHECK(state_of(*world, mover).x == doctest::Approx(2.0).epsilon(0.01)); // never pushed back
        CHECK(state_of(*world, box).x > 2.9); // pushed ahead of it
    }
    SUBCASE("Damping") {
        const auto world = make_world(0.0);
        physics::BodyDef sliding;
        const physics::BodyId free = world->add(sliding);
        sliding.x = 10.0;
        sliding.damping = 5.0;
        const physics::BodyId damped = world->add(sliding);
        world->push(free, 1.0, 0.0);
        world->push(damped, 1.0, 0.0);
        run(*world, 60);
        CHECK(state_of(*world, free).velocity_x == doctest::Approx(1.0)); // mass 1: 1 unit/s
        CHECK(state_of(*world, damped).velocity_x < 0.05);
    }
}

TEST_CASE("Bad input is refused or clamped, and bodies can be removed") {
    const auto world = make_world(0.0);
    physics::BodyDef bad;
    bad.x = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(world->add(bad).is_valid());
    bad.x = 1.0e9;
    CHECK_FALSE(world->add(bad).is_valid());
    physics::BodyDef odd;
    odd.width = -5.0;
    odd.density = std::numeric_limits<double>::infinity();
    odd.restitution = 7.0;
    const physics::BodyId id = world->add(odd); // clamped, not refused
    REQUIRE(id.is_valid());
    CHECK_FALSE(world->set_position(id, std::numeric_limits<double>::infinity(), 0.0));
    CHECK(world->set_position(id, 3.0, 4.0));
    CHECK(state_of(*world, id).x == 3.0);
    world->step(std::numeric_limits<double>::quiet_NaN()); // ignored
    CHECK(world->remove(id));
    CHECK_FALSE(world->remove(id));
    CHECK_FALSE(world->state(id).has_value());
    CHECK_FALSE(world->push(id, 1.0, 0.0));
    world->add(physics::BodyDef{});
    world->clear();
    CHECK(world->body_count() == 0);
}

TEST_CASE("The same steps give the same result") {
    const auto simulate = [] {
        const auto world = make_world(10.0);
        floor(*world);
        std::vector<physics::BodyId> boxes;
        for (int i = 0; i < 10; ++i) {
            physics::BodyDef box;
            box.x = (i % 3) * 0.6;
            box.y = -i * 1.2;
            box.shape = i % 2 == 0 ? physics::ShapeKind::Box : physics::ShapeKind::Circle;
            boxes.push_back(world->add(box));
        }
        run(*world, 120);
        std::vector<double> positions;
        for (const physics::BodyId box : boxes) {
            positions.push_back(state_of(*world, box).x);
            positions.push_back(state_of(*world, box).y);
        }
        return positions;
    };
    CHECK(simulate() == simulate());
}
