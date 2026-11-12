// Behaviours on their own, without a project: blocking bodies, trigger areas and
// interaction, driven by an input state the test sets directly.

#include "engine/game/runtime.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <memory>
#include <vector>

namespace game = engine::game;
namespace input = engine::input;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace world = engine::world;

namespace {

struct Fixture {
    Fixture() {
        game::register_types(registry);
        REQUIRE(registry.errors().empty());
        w = std::make_unique<world::World>(registry);
        REQUIRE(scene2d::register_components(*w));
        REQUIRE(game::register_behaviour_components(*w));
        hero = place("hero", 0.0, 0.0);
        w->add<game::TopDownMover>(hero);
    }

    world::Entity place(const char* name, double x, double y) const {
        const world::Entity e = w->create(name);
        scene2d::Transform2D* at = w->add<scene2d::Transform2D>(e);
        at->x = x;
        at->y = y;
        return e;
    }

    /// Runs `steps` steps of 1/60 s with the given move actions held.
    void walk(int steps, std::initializer_list<input::Key> keys) {
        for (int i = 0; i < steps; ++i) {
            state.begin_step();
            for (const input::Key k :
                 {input::Key::Left, input::Key::Right, input::Key::Up, input::Key::Down}) {
                bool down = false;
                for (const input::Key held : keys) {
                    down = down || held == k;
                }
                state.set(input::Binding::key(k), down);
            }
            game::move_characters(*w, state, 1.0 / 60.0, 16);
        }
    }

    [[nodiscard]] scene2d::Transform2D& at(world::Entity e) const {
        scene2d::Transform2D* t = w->get<scene2d::Transform2D>(e);
        REQUIRE(t != nullptr);
        return *t;
    }

    reflect::Registry registry;
    std::unique_ptr<world::World> w;
    input::InputState state{input::InputMap::rpg_preset()};
    world::Entity hero;
};

} // namespace

TEST_CASE("Boxes overlap only by more than an edge, and stop at each other") {
    const scene2d::Box a{0.0, 0.0, 1.0, 1.0};
    CHECK(scene2d::overlaps(a, scene2d::Box{0.5, 0.5, 1.5, 1.5}));
    CHECK_FALSE(scene2d::overlaps(a, scene2d::Box{1.0, 0.0, 2.0, 1.0})); // sharing an edge
    CHECK_FALSE(scene2d::overlaps(a, scene2d::Box{1.0 - 1e-12, 0.0, 2.0, 1.0}));
    const std::vector<scene2d::Box> crates{
        {3.0, 0.0, 4.0, 1.0}, {-3.0, 0.0, -2.0, 1.0}, {0.0, 5.0, 1.0, 6.0}};
    using scene2d::Axis;
    CHECK(scene2d::sweep_boxes(a, Axis::X, 5.0, crates) == 2.0);
    CHECK(scene2d::sweep_boxes(a, Axis::X, -5.0, crates) == -2.0);
    CHECK(scene2d::sweep_boxes(a, Axis::Y, 10.0, crates) == 4.0);
    CHECK(scene2d::sweep_boxes(a, Axis::Y, -10.0, crates) == -10.0);
    CHECK(scene2d::sweep_boxes(a, Axis::X, 1.0, crates) == 1.0);
    // Already overlapping: it does not block, so things can separate.
    CHECK(scene2d::sweep_boxes(scene2d::Box{2.5, 0.0, 3.5, 1.0}, Axis::X, 5.0, crates) == 5.0);
    // Touching on the other axis only: passes by.
    CHECK(scene2d::sweep_boxes(scene2d::Box{0.0, 1.0, 1.0, 2.0}, Axis::X, 5.0, crates) == 5.0);
}

TEST_CASE("Solid bodies stop movers, but not their own") {
    Fixture f;
    const world::Entity villager = f.place("villager", 2.0, 0.0);
    f.w->add<game::Body>(villager); // 0.75 wide: from 1.625
    f.w->add<game::Body>(f.hero);   // the hero's own body never blocks it

    f.walk(60, {input::Key::Right});                // a second at 4 tiles per second would reach 4
    CHECK(f.at(f.hero).x == doctest::Approx(1.25)); // hitbox right edge on 1.625
    CHECK(f.at(villager).x == 2.0);

    // A disabled body lets the mover through.
    f.w->get<game::Body>(villager)->enabled = false;
    f.walk(15, {input::Key::Right});
    CHECK(f.at(f.hero).x == doctest::Approx(2.25));
}

TEST_CASE("Trigger areas report movers walking in and out, once each") {
    Fixture f;
    const world::Entity door = f.place("door", 5.0, 0.0); // 1 x 1: x 4.5 to 5.5
    f.w->add<game::Trigger>(door);
    const world::Entity guard = f.place("guard", 5.0, 0.0); // a second mover, already inside
    f.w->add<game::TopDownMover>(guard)->enabled = false;

    std::vector<game::TriggerEvent> events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 1);
    CHECK(events[0].trigger == door);
    CHECK(events[0].who == guard);
    CHECK(events[0].entered);
    CHECK(game::update_triggers(*f.w).empty()); // nothing changed

    // The hero's hitbox (0.75 wide) reaches 4.5 once x > 4.125, and leaves past 5.875.
    f.at(f.hero).x = 4.0;
    CHECK(game::update_triggers(*f.w).empty());
    f.at(f.hero).x = 4.2;
    events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 1);
    CHECK(events[0].who == f.hero);
    CHECK(events[0].entered);
    f.at(f.hero).x = 6.0;
    f.at(guard).x = 9.0;
    events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 2); // both left: exits sorted by mover GUID
    CHECK_FALSE(events[0].entered);
    CHECK_FALSE(events[1].entered);
    CHECK(f.w->guid(events[0].who) < f.w->guid(events[1].who));

    // Disabling the area makes everyone inside leave.
    f.at(f.hero).x = 5.0;
    REQUIRE(game::update_triggers(*f.w).size() == 1);
    f.w->get<game::Trigger>(door)->enabled = false;
    events = game::update_triggers(*f.w);
    REQUIRE(events.size() == 1);
    CHECK_FALSE(events[0].entered);
    CHECK(f.w->get<game::Trigger>(door)->inside.empty());
}

TEST_CASE("Interact reaches the nearest interactable the mover faces") {
    Fixture f;
    const world::Entity sign = f.place("sign", 1.0, 0.0); // 1 x 1: x 0.5 to 1.5
    f.w->add<game::Interactable>(sign);
    game::TopDownMover* mover = f.w->get<game::TopDownMover>(f.hero);

    mover->facing = game::Facing::Right; // reach: hitbox moved 0.5 right, x 0.125 to 0.875
    CHECK(game::find_interaction(*f.w, f.hero) == sign);
    mover->facing = game::Facing::Left;
    CHECK_FALSE(game::find_interaction(*f.w, f.hero).is_valid());
    mover->facing = game::Facing::Up; // hitbox y -0.5 to 0, moved up: y -1 to -0.5
    CHECK_FALSE(game::find_interaction(*f.w, f.hero).is_valid());

    // The nearer of two wins; disabled ones and the mover itself are skipped.
    const world::Entity chest = f.place("chest", 0.0, -0.75);
    f.w->add<game::Interactable>(chest); // y -1.75 to -0.75: overlaps the reach upwards
    CHECK(game::find_interaction(*f.w, f.hero) == chest);
    mover->facing = game::Facing::Right;
    const world::Entity near_sign = f.place("near_sign", 0.75, 0.0);
    f.w->add<game::Interactable>(near_sign);
    CHECK(game::find_interaction(*f.w, f.hero) == near_sign);
    f.w->get<game::Interactable>(near_sign)->enabled = false;
    CHECK(game::find_interaction(*f.w, f.hero) == sign);
    f.w->add<game::Interactable>(f.hero);
    CHECK(game::find_interaction(*f.w, f.hero) == sign);
    CHECK_FALSE(game::find_interaction(*f.w, sign).is_valid()); // not a mover

    const reflect::TypeInfo* trigger = f.registry.find_type<game::Trigger>();
    REQUIRE(trigger != nullptr);
    CHECK(trigger->meta.display_name == "Trigger Area");
    CHECK(trigger->find_event("entered") != nullptr);
    CHECK(f.registry.find_type<game::Interactable>()->find_event("interacted") != nullptr);
    CHECK(f.registry.find_type<game::Body>()->meta.display_name == "Solid Body");
}

TEST_CASE("Falling and jumping, one step at a time") {
    // A floor at y = 0 below a ceiling at y = -10: `move` goes as far as it can.
    double y = 0.0;
    double ceiling = -10.0;
    const auto move = [&](double wanted) {
        const double to = std::clamp(y + wanted, ceiling, 0.0);
        const double moved = to - y;
        y = to;
        return moved;
    };
    constexpr double dt = 1.0 / 60.0;
    game::Jumping jumping; // gravity 40, height 3
    game::fall(jumping, false, dt, move);
    CHECK(jumping.on_ground); // pressed against the floor
    CHECK(jumping.fall_speed == 0.0);
    CHECK(y == 0.0);

    // A jump rises its height (less half a step's rise, for whole steps), then lands.
    game::fall(jumping, true, dt, move);
    CHECK_FALSE(jumping.on_ground);
    CHECK(jumping.fall_speed < 0.0);
    double highest = 0.0;
    int steps = 1;
    while (!jumping.on_ground && steps < 600) {
        game::fall(jumping, true, dt, move); // held: no jumping again in the air
        highest = std::min(highest, y);
        ++steps;
    }
    CAPTURE(highest);
    CHECK(highest < -2.8);
    CHECK(highest > -3.0);
    CHECK(y == 0.0);
    CHECK(jumping.fall_speed == 0.0);
    // Up and down in about 2 * sqrt(2 h / g) = 0.77 seconds.
    CHECK(steps >= 44);
    CHECK(steps <= 48);

    // A ceiling 1 tile up stops the rise; it falls back from there.
    ceiling = -1.0;
    game::fall(jumping, true, dt, move);
    while (jumping.fall_speed < 0.0) {
        game::fall(jumping, false, dt, move);
    }
    CHECK(y == -1.0); // bumped its head
    CHECK_FALSE(jumping.on_ground);
    for (int i = 0; i < 60 && !jumping.on_ground; ++i) {
        game::fall(jumping, false, dt, move);
    }
    CHECK(jumping.on_ground);
    CHECK(y == 0.0);

    // From high up, it falls no faster than its fastest fall.
    ceiling = -1000.0;
    y = -500.0;
    jumping.on_ground = false;
    for (int i = 0; i < 120; ++i) {
        game::fall(jumping, false, dt, move);
    }
    CHECK(jumping.fall_speed == 20.0);
    // No gravity, or gravity that is not a number: it stays where it is.
    jumping.fall_speed = 0.0;
    jumping.gravity = std::numeric_limits<double>::quiet_NaN();
    const double before = y;
    game::fall(jumping, false, dt, move);
    CHECK(y == before);
}

TEST_CASE("Jumping: walking left and right, landing on solid tiles and bodies, and jumping") {
    Fixture f;
    f.w->add<game::Jumping>(f.hero);
    // Only now: adding a component moves the entity's others in memory.
    game::TopDownMover* mover = f.w->get<game::TopDownMover>(f.hero);
    mover->hitbox_height = 1.0; // the whole character, seen from the side
    // A solid floor whose top is y = 4, from x = -5 to 15.
    const world::Entity floor = f.place("floor", -5.0, 4.0);
    scene2d::Tilemap* tiles = f.w->add<scene2d::Tilemap>(floor);
    tiles->solid = true;
    tiles->tiles = "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
    const auto run = [&](int steps, std::initializer_list<input::Key> keys) {
        for (int i = 0; i < steps; ++i) {
            f.state.begin_step();
            for (const input::Key k : {input::Key::Left, input::Key::Right, input::Key::Up,
                                       input::Key::Down, input::Key::Space}) {
                bool down = false;
                for (const input::Key held : keys) {
                    down = down || held == k;
                }
                f.state.set(input::Binding::key(k), down);
            }
            game::move_characters(*f.w, f.state, 1.0 / 60.0, 16);
        }
    };
    const game::Jumping& jumping = *f.w->get<game::Jumping>(f.hero);

    // It falls from y = 0 and lands with its feet on the floor.
    run(60, {});
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(jumping.on_ground);
    CHECK(f.at(f.hero).x == 0.0);
    // Down does nothing; Right walks right only, facing right.
    run(30, {input::Key::Down});
    CHECK(f.at(f.hero).y == 4.0);
    run(30, {input::Key::Right});
    CHECK(f.at(f.hero).x == doctest::Approx(2.0)); // half a second at 4 tiles a second
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(mover->facing == game::Facing::Right);
    // Space jumps: it rises off the floor, then lands again.
    run(1, {});
    run(1, {input::Key::Space});
    CHECK(f.at(f.hero).y < 4.0);
    CHECK_FALSE(jumping.on_ground);
    run(10, {input::Key::Space});
    const double up_high = f.at(f.hero).y;
    CHECK(up_high < 2.5);
    run(60, {});
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(jumping.on_ground);
    // Up jumps too.
    run(1, {input::Key::Up});
    CHECK(f.at(f.hero).y < 4.0);
    run(60, {});
    // A Solid Body is something to stand on.
    const world::Entity crate = f.place("crate", 2.0, 4.0);
    game::Body* box = f.w->add<game::Body>(crate);
    box->width = 1.0;
    box->height = 1.0; // its top is y = 3
    run(1, {input::Key::Space});
    run(60, {});
    CHECK(f.at(f.hero).y == 3.0);
    CHECK(jumping.on_ground);
    // Walking off it, it falls back to the floor.
    run(30, {input::Key::Right});
    run(30, {});
    CHECK(f.at(f.hero).y == 4.0);
    // Disabled, it does not walk or jump, but it still falls.
    mover->enabled = false;
    f.at(f.hero).y = 1.0;
    run(60, {input::Key::Space, input::Key::Left});
    CHECK(f.at(f.hero).y == 4.0);
    CHECK(f.at(f.hero).x == doctest::Approx(4.0));
}
