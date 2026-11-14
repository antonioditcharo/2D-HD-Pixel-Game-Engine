#pragma once

// Physics (ADR-008): rigid bodies on Box2D v3, behind a small facade so Box2D's types never
// appear outside src/physics.cpp. One world unit is one tile, which Box2D treats as one
// metre: everyday objects of a tile or so are what it is tuned for. Maps use y downwards,
// so gravity "down" is +y.
//
// See docs/dev/physics.md.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace engine::physics {

enum class BodyType : std::uint8_t {
    Static,    ///< Never moves: walls, the ground.
    Dynamic,   ///< Moved by forces and collisions: crates, balls.
    Kinematic, ///< Moved by its velocity only; pushes dynamic bodies, is never pushed.
};

enum class ShapeKind : std::uint8_t { Box, Circle };

struct BodyDef {
    BodyType type = BodyType::Dynamic;
    double x = 0.0; ///< The centre, in world units.
    double y = 0.0;
    ShapeKind shape = ShapeKind::Box;
    double width = 1.0; ///< For boxes.
    double height = 1.0;
    double radius = 0.5; ///< For circles.
    double density = 1.0;
    double friction = 0.6;
    double restitution = 0.0; ///< Bounciness, 0 to 1.
    double damping = 0.0;     ///< Slows the body down over time (linear damping).
    double gravity_scale = 1.0;
    bool fixed_rotation = true; ///< Sprites do not turn, so by default bodies do not either.
    std::uint64_t user = 0;     ///< Reported in contacts, to tell bodies apart.
};

struct BodyId {
    std::uint32_t value = 0; ///< 0 is no body.
    [[nodiscard]] bool is_valid() const noexcept { return value != 0; }
    friend bool operator==(BodyId, BodyId) = default;
};

struct BodyState {
    double x = 0.0; ///< The centre.
    double y = 0.0;
    double angle = 0.0; ///< Radians.
    double velocity_x = 0.0;
    double velocity_y = 0.0;
};

/// Two bodies that started touching, by their `user` values.
struct Contact {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    friend bool operator==(const Contact&, const Contact&) = default;
};

class PhysicsWorld {
public:
    /// Gravity in world units per second squared; (0, 0) for top-down games.
    [[nodiscard]] static std::unique_ptr<PhysicsWorld> create(double gravity_x, double gravity_y);

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;
    ~PhysicsWorld();

    /// Adds a body. Sizes, density, friction and the rest are clamped to sensible ranges, and
    /// non-finite numbers replaced by defaults, so hostile scene data cannot break the
    /// simulation.
    BodyId add(const BodyDef& def);
    bool remove(BodyId body);
    /// Removes every body.
    void clear();

    /// Advances the simulation by `seconds` (4 substeps). Contacts that began during it are
    /// then in contacts().
    void step(double seconds);
    [[nodiscard]] const std::vector<Contact>& contacts() const noexcept;

    [[nodiscard]] std::optional<BodyState> state(BodyId body) const;
    bool set_position(BodyId body, double x, double y);
    bool set_velocity(BodyId body, double velocity_x, double velocity_y);
    /// An instant push, in mass times units per second.
    bool push(BodyId body, double impulse_x, double impulse_y);
    [[nodiscard]] std::size_t body_count() const noexcept;

    struct Impl;

private:
    explicit PhysicsWorld(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::physics
