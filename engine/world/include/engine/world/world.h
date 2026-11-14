#pragma once

// The world: entities with GUIDs, names and a hierarchy, holding components that are
// types registered in the reflection registry (ADR-006). Storage is flecs [S70]; flecs
// stays an implementation detail, and everything here is type-erased over the registry,
// which is what scene files and the editor work with.
//
// See docs/dev/world.md.

#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace engine::world {

/// A handle to an entity. Handles of destroyed entities are never valid again, even when
/// the storage is reused.
struct Entity {
    std::uint64_t id = 0;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return id != 0; }
    friend constexpr bool operator==(Entity, Entity) = default;
};

/// How the world creates, copies, moves and destroys one component type, generated from
/// the C++ type by World::register_component().
struct ComponentOps {
    std::size_t size = 0;
    std::size_t alignment = 0;
    const void* key{nullptr}; ///< The reflection type key.
    void (*construct)(void* at, std::int32_t count){nullptr};
    void (*destroy)(void* at, std::int32_t count){nullptr};
    void (*copy)(void* to, const void* from, std::int32_t count){nullptr};
    void (*move)(void* to, void* from, std::int32_t count){nullptr};
    void (*copy_construct)(void* to, const void* from, std::int32_t count){nullptr};
    void (*move_construct)(void* to, void* from, std::int32_t count){nullptr};
};

namespace detail {

template <typename T>
[[nodiscard]] ComponentOps component_ops() {
    ComponentOps ops;
    ops.size = sizeof(T);
    ops.alignment = alignof(T);
    ops.key = reflect::ObjectRef::key_of<T>();
    ops.construct = [](void* at, std::int32_t count) {
        for (std::int32_t i = 0; i < count; ++i) {
            ::new (static_cast<T*>(at) + i) T();
        }
    };
    ops.destroy = [](void* at, std::int32_t count) {
        for (std::int32_t i = 0; i < count; ++i) {
            (static_cast<T*>(at) + i)->~T();
        }
    };
    ops.copy = [](void* to, const void* from, std::int32_t count) {
        for (std::int32_t i = 0; i < count; ++i) {
            static_cast<T*>(to)[i] = static_cast<const T*>(from)[i];
        }
    };
    ops.move = [](void* to, void* from, std::int32_t count) {
        for (std::int32_t i = 0; i < count; ++i) {
            static_cast<T*>(to)[i] = std::move(static_cast<T*>(from)[i]);
        }
    };
    ops.copy_construct = [](void* to, const void* from, std::int32_t count) {
        for (std::int32_t i = 0; i < count; ++i) {
            ::new (static_cast<T*>(to) + i) T(static_cast<const T*>(from)[i]);
        }
    };
    ops.move_construct = [](void* to, void* from, std::int32_t count) {
        for (std::int32_t i = 0; i < count; ++i) {
            ::new (static_cast<T*>(to) + i) T(std::move(static_cast<T*>(from)[i]));
        }
    };
    return ops;
}

} // namespace detail

/// The world. Not thread-safe.
///
/// **Component pointers move.** An ObjectRef or T* returned for a component stays valid
/// only until the next change to which components any entity has (adding or removing a
/// component, creating or destroying an entity). Look components up again after such a
/// change.
class World {
public:
    explicit World(const reflect::Registry& registry);
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;
    ~World();

    // --- Component types --------------------------------------------------------------

    /// Makes T usable as a component. T must be registered in the reflection registry,
    /// default-constructible, copyable and movable. Returns false if T is not registered;
    /// registering twice is harmless.
    template <typename T>
    bool register_component() {
        static_assert(std::is_default_constructible_v<T> && std::is_copy_constructible_v<T> &&
                          std::is_copy_assignable_v<T> && std::is_move_constructible_v<T> &&
                          std::is_move_assignable_v<T>,
                      "Components must be default-constructible, copyable and movable.");
        const reflect::TypeInfo* type = registry().template find_type<T>();
        return type != nullptr && register_component(*type, detail::component_ops<T>());
    }

    /// True if `type` was registered with register_component().
    [[nodiscard]] bool is_component(const reflect::TypeInfo& type) const;

    /// The component types, in the order they were registered.
    [[nodiscard]] std::vector<const reflect::TypeInfo*> component_types() const;

    // --- Entities ---------------------------------------------------------------------

    /// Creates an entity with a new random GUID.
    Entity create(std::string_view name = {});
    /// Creates an entity with the given GUID, as loading a scene does. Returns an invalid
    /// handle if the GUID is zero or already used.
    Entity create(Guid guid, std::string_view name = {});
    /// Destroys an entity and all its descendants. Returns false if it was not alive.
    bool destroy(Entity entity);

    [[nodiscard]] bool is_alive(Entity entity) const;
    [[nodiscard]] std::size_t entity_count() const;

    /// The entity's GUID; zero if it is not alive.
    [[nodiscard]] Guid guid(Entity entity) const;
    /// The entity with this GUID, or an invalid handle.
    [[nodiscard]] Entity find(Guid guid) const;

    /// The entities with this exact name, in creation order.
    [[nodiscard]] std::vector<Entity> find_named(std::string_view name) const;

    /// The entity's display name. Names need not be unique: references use GUIDs.
    [[nodiscard]] std::string_view name(Entity entity) const;
    bool set_name(Entity entity, std::string_view name);

    // --- Hierarchy --------------------------------------------------------------------

    /// Makes `child` the last child of `parent`, or a root if `parent` is invalid. Refuses,
    /// returning false, if either is not alive or if `parent` is `child` or one of its
    /// descendants.
    bool set_parent(Entity child, Entity parent);
    /// The parent, or an invalid handle for a root.
    [[nodiscard]] Entity parent(Entity entity) const;
    /// The children, in order.
    [[nodiscard]] std::vector<Entity> children(Entity entity) const;
    /// The entities without a parent, in creation order.
    [[nodiscard]] std::vector<Entity> roots() const;

    // --- Components -------------------------------------------------------------------

    /// Adds a default-constructed component of `type`, or returns the existing one.
    /// Returns a null reference if the entity is not alive or `type` is not a component.
    reflect::ObjectRef add(Entity entity, const reflect::TypeInfo& type);
    /// The component of `type`, or a null reference.
    [[nodiscard]] reflect::ObjectRef get(Entity entity, const reflect::TypeInfo& type) const;
    /// Removes the component. Returns false if the entity did not have it.
    bool remove(Entity entity, const reflect::TypeInfo& type);
    /// The entity's components, in the order their types were registered.
    [[nodiscard]] std::vector<const reflect::TypeInfo*> components(Entity entity) const;

    template <typename T>
    T* add(Entity entity) {
        const reflect::TypeInfo* type = registry().template find_type<T>();
        return type == nullptr ? nullptr : add(entity, *type).template as<T>();
    }

    template <typename T>
    [[nodiscard]] T* get(Entity entity) const {
        const reflect::TypeInfo* type = registry().template find_type<T>();
        return type == nullptr ? nullptr : get(entity, *type).template as<T>();
    }

    /// Calls `visit` for every entity that has a component of `type`. `visit` must not add
    /// or remove components or entities.
    void each(const reflect::TypeInfo& type,
              const std::function<void(Entity, reflect::ObjectRef)>& visit) const;

    [[nodiscard]] const reflect::Registry& registry() const noexcept;

    struct Impl;

private:
    bool register_component(const reflect::TypeInfo& type, const ComponentOps& ops);

    std::unique_ptr<Impl> impl_;
};

} // namespace engine::world
