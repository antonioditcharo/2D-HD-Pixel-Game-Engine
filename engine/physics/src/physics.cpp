#include "engine/physics/physics.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace engine::physics {

namespace {

/// `value` if it is finite and within [low, high]; clamped if finite; `fallback` if not.
[[nodiscard]] float sane(double value, double low, double high, double fallback) {
    return static_cast<float>(std::isfinite(value) ? std::clamp(value, low, high) : fallback);
}

/// Positions beyond this are refused: Box2D works in single precision.
constexpr double max_coordinate = 1.0e6;

} // namespace

struct PhysicsWorld::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        if (b2World_IsValid(world)) {
            b2DestroyWorld(world);
        }
    }

    [[nodiscard]] const b2BodyId* find(BodyId body) const {
        const auto found = bodies.find(body.value);
        return found == bodies.end() || !b2Body_IsValid(found->second.id) ? nullptr
                                                                          : &found->second.id;
    }

    struct Entry {
        b2BodyId id;
        std::uint64_t user = 0;
    };

    b2WorldId world = b2_nullWorldId;
    std::unordered_map<std::uint32_t, Entry> bodies;
    std::unordered_map<std::uint64_t, std::uint64_t> users; ///< b2StoreBodyId -> user
    std::uint32_t next = 1;
    std::vector<Contact> contacts;
};

PhysicsWorld::PhysicsWorld(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

PhysicsWorld::~PhysicsWorld() = default;

std::unique_ptr<PhysicsWorld> PhysicsWorld::create(double gravity_x, double gravity_y) {
    auto impl = std::make_unique<Impl>();
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity =
        b2Vec2{sane(gravity_x, -1000.0, 1000.0, 0.0), sane(gravity_y, -1000.0, 1000.0, 0.0)};
    impl->world = b2CreateWorld(&def);
    if (!b2World_IsValid(impl->world)) {
        return nullptr;
    }
    return std::unique_ptr<PhysicsWorld>(new PhysicsWorld(std::move(impl)));
}

BodyId PhysicsWorld::add(const BodyDef& def) {
    Impl& p = *impl_;
    if (!std::isfinite(def.x) || !std::isfinite(def.y) || std::abs(def.x) > max_coordinate ||
        std::abs(def.y) > max_coordinate) {
        return {};
    }
    b2BodyDef body = b2DefaultBodyDef();
    body.type = def.type == BodyType::Static    ? b2_staticBody
                : def.type == BodyType::Dynamic ? b2_dynamicBody
                                                : b2_kinematicBody;
    body.position = b2Vec2{static_cast<float>(def.x), static_cast<float>(def.y)};
    body.fixedRotation = def.fixed_rotation;
    body.gravityScale = sane(def.gravity_scale, -100.0, 100.0, 1.0);
    body.linearDamping = sane(def.damping, 0.0, 100.0, 0.0);
    const b2BodyId id = b2CreateBody(p.world, &body);

    b2ShapeDef shape = b2DefaultShapeDef();
    shape.density = sane(def.density, 0.001, 1000.0, 1.0);
    shape.material.friction = sane(def.friction, 0.0, 10.0, 0.6);
    shape.material.restitution = sane(def.restitution, 0.0, 1.0, 0.0);
    shape.enableContactEvents = true;
    if (def.shape == ShapeKind::Circle) {
        const b2Circle circle{b2Vec2{0.0F, 0.0F}, sane(def.radius, 0.01, 1000.0, 0.5)};
        b2CreateCircleShape(id, &shape, &circle);
    } else {
        const b2Polygon box = b2MakeBox(sane(def.width, 0.02, 2000.0, 1.0) / 2.0F,
                                        sane(def.height, 0.02, 2000.0, 1.0) / 2.0F);
        b2CreatePolygonShape(id, &shape, &box);
    }
    const BodyId handle{p.next++};
    p.bodies.emplace(handle.value, Impl::Entry{id, def.user});
    p.users.emplace(b2StoreBodyId(id), def.user);
    return handle;
}

bool PhysicsWorld::remove(BodyId body) {
    Impl& p = *impl_;
    const auto found = p.bodies.find(body.value);
    if (found == p.bodies.end()) {
        return false;
    }
    if (b2Body_IsValid(found->second.id)) {
        p.users.erase(b2StoreBodyId(found->second.id));
        b2DestroyBody(found->second.id);
    }
    p.bodies.erase(found);
    return true;
}

void PhysicsWorld::clear() {
    Impl& p = *impl_;
    for (const auto& [handle, entry] : p.bodies) {
        if (b2Body_IsValid(entry.id)) {
            b2DestroyBody(entry.id);
        }
    }
    p.bodies.clear();
    p.users.clear();
    p.contacts.clear();
}

void PhysicsWorld::step(double seconds) {
    Impl& p = *impl_;
    p.contacts.clear();
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return;
    }
    b2World_Step(p.world, static_cast<float>(std::min(seconds, 0.25)), 4);
    const b2ContactEvents events = b2World_GetContactEvents(p.world);
    for (int i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent& event = events.beginEvents[i];
        if (!b2Shape_IsValid(event.shapeIdA) || !b2Shape_IsValid(event.shapeIdB)) {
            continue;
        }
        const auto a = p.users.find(b2StoreBodyId(b2Shape_GetBody(event.shapeIdA)));
        const auto b = p.users.find(b2StoreBodyId(b2Shape_GetBody(event.shapeIdB)));
        if (a != p.users.end() && b != p.users.end()) {
            p.contacts.push_back(Contact{a->second, b->second});
        }
    }
}

const std::vector<Contact>& PhysicsWorld::contacts() const noexcept {
    return impl_->contacts;
}

std::optional<BodyState> PhysicsWorld::state(BodyId body) const {
    const b2BodyId* id = impl_->find(body);
    if (id == nullptr) {
        return std::nullopt;
    }
    const b2Vec2 position = b2Body_GetPosition(*id);
    const b2Vec2 velocity = b2Body_GetLinearVelocity(*id);
    return BodyState{position.x, position.y, b2Rot_GetAngle(b2Body_GetRotation(*id)), velocity.x,
                     velocity.y};
}

bool PhysicsWorld::set_position(BodyId body, double x, double y) {
    const b2BodyId* id = impl_->find(body);
    if (id == nullptr || !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > max_coordinate ||
        std::abs(y) > max_coordinate) {
        return false;
    }
    b2Body_SetTransform(*id, b2Vec2{static_cast<float>(x), static_cast<float>(y)},
                        b2Body_GetRotation(*id));
    return true;
}

bool PhysicsWorld::set_velocity(BodyId body, double velocity_x, double velocity_y) {
    const b2BodyId* id = impl_->find(body);
    if (id == nullptr || !std::isfinite(velocity_x) || !std::isfinite(velocity_y)) {
        return false;
    }
    b2Body_SetLinearVelocity(
        *id, b2Vec2{static_cast<float>(velocity_x), static_cast<float>(velocity_y)});
    return true;
}

bool PhysicsWorld::push(BodyId body, double impulse_x, double impulse_y) {
    const b2BodyId* id = impl_->find(body);
    if (id == nullptr || !std::isfinite(impulse_x) || !std::isfinite(impulse_y)) {
        return false;
    }
    b2Body_ApplyLinearImpulseToCenter(
        *id, b2Vec2{static_cast<float>(impulse_x), static_cast<float>(impulse_y)}, true);
    return true;
}

std::size_t PhysicsWorld::body_count() const noexcept {
    return impl_->bodies.size();
}

} // namespace engine::physics
