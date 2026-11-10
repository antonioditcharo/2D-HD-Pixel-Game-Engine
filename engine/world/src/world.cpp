#include "engine/world/world.h"

#include <flecs.h>

#include <algorithm>
#include <unordered_map>

namespace engine::world {

namespace {

struct Component {
    const reflect::TypeInfo* type{nullptr};
    ComponentOps ops;
    ecs_entity_t id = 0; ///< The flecs component.
};

/// What the world keeps about each entity besides its components.
struct Record {
    Guid guid;
    std::string name;
    Entity parent;
    std::vector<Entity> children; ///< In order.
    std::uint64_t order = 0;      ///< Creation order, for roots().
};

// flecs lifecycle hooks: they forward to the ComponentOps stored as the hook context.

const ComponentOps& ops_of(const ecs_type_info_t* info) {
    return *static_cast<const ComponentOps*>(info->hooks.ctx);
}

void hook_construct(void* at, std::int32_t count, const ecs_type_info_t* info) {
    ops_of(info).construct(at, count);
}

void hook_destroy(void* at, std::int32_t count, const ecs_type_info_t* info) {
    ops_of(info).destroy(at, count);
}

void hook_copy(void* to, const void* from, std::int32_t count, const ecs_type_info_t* info) {
    ops_of(info).copy(to, from, count);
}

void hook_move(void* to, void* from, std::int32_t count, const ecs_type_info_t* info) {
    ops_of(info).move(to, from, count);
}

void hook_copy_construct(void* to, const void* from, std::int32_t count,
                         const ecs_type_info_t* info) {
    ops_of(info).copy_construct(to, from, count);
}

void hook_move_construct(void* to, void* from, std::int32_t count, const ecs_type_info_t* info) {
    ops_of(info).move_construct(to, from, count);
}

} // namespace

struct World::Impl {
    explicit Impl(const reflect::Registry& registry_in) : registry(registry_in), ecs(ecs_init()) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() { ecs_fini(ecs); }

    const reflect::Registry& registry;
    ecs_world_t* ecs;
    /// Stable addresses: flecs keeps a pointer to each ComponentOps as hook context.
    std::vector<std::unique_ptr<Component>> components;
    std::unordered_map<std::uint64_t, Record> records;
    std::unordered_map<Guid, Entity, GuidHash> by_guid;
    std::uint64_t next_order = 0;

    [[nodiscard]] const Component* find_component(const reflect::TypeInfo& type) const {
        for (const auto& component : components) {
            if (component->type == &type) {
                return component.get();
            }
        }
        return nullptr;
    }

    /// The record of a live entity, or null. Every public operation checks this first, so
    /// no stale or foreign handle ever reaches flecs.
    [[nodiscard]] Record* record(Entity entity) {
        if (!entity.is_valid() || !ecs_is_alive(ecs, entity.id)) {
            return nullptr;
        }
        const auto found = records.find(entity.id);
        return found == records.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const Record* record(Entity entity) const {
        return const_cast<Impl*>(this)->record(
            entity); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    }

    Entity create(Guid guid, std::string_view name) {
        const Entity entity{ecs_new(ecs)};
        Record record;
        record.guid = guid;
        record.name = std::string(name);
        record.order = next_order++;
        records.emplace(entity.id, std::move(record));
        by_guid.emplace(guid, entity);
        return entity;
    }

    void detach(Entity child, Record& child_record) {
        if (Record* parent = record(child_record.parent)) {
            std::erase(parent->children, child);
        }
        child_record.parent = Entity{};
    }
};

World::World(const reflect::Registry& registry) : impl_(std::make_unique<Impl>(registry)) {}

World::~World() = default;

const reflect::Registry& World::registry() const noexcept {
    return impl_->registry;
}

// ---------------------------------------------------------------------------------------
// Component types
// ---------------------------------------------------------------------------------------

bool World::register_component(const reflect::TypeInfo& type, const ComponentOps& ops) {
    if (impl_->find_component(type) != nullptr) {
        return true;
    }
    auto component = std::make_unique<Component>();
    component->type = &type;
    component->ops = ops;

    ecs_component_desc_t desc{};
    desc.type.size = static_cast<ecs_size_t>(ops.size);
    desc.type.alignment = static_cast<ecs_size_t>(ops.alignment);
    component->id = ecs_component_init(impl_->ecs, &desc);
    if (component->id == 0) {
        return false;
    }
    ecs_type_hooks_t hooks{};
    hooks.ctor = &hook_construct;
    hooks.dtor = &hook_destroy;
    hooks.copy = &hook_copy;
    hooks.move = &hook_move;
    hooks.copy_ctor = &hook_copy_construct;
    hooks.move_ctor = &hook_move_construct;
    hooks.ctx = &component->ops;
    ecs_set_hooks_id(impl_->ecs, component->id, &hooks);

    impl_->components.push_back(std::move(component));
    return true;
}

bool World::is_component(const reflect::TypeInfo& type) const {
    return impl_->find_component(type) != nullptr;
}

std::vector<const reflect::TypeInfo*> World::component_types() const {
    std::vector<const reflect::TypeInfo*> types;
    types.reserve(impl_->components.size());
    for (const auto& component : impl_->components) {
        types.push_back(component->type);
    }
    return types;
}

// ---------------------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------------------

Entity World::create(std::string_view name) {
    Guid guid = Guid::generate();
    while (impl_->by_guid.contains(guid)) {
        guid = Guid::generate(); // Practically never; keeps GUIDs unique regardless.
    }
    return impl_->create(guid, name);
}

Entity World::create(Guid guid, std::string_view name) {
    if (!guid.is_valid() || impl_->by_guid.contains(guid)) {
        return Entity{};
    }
    return impl_->create(guid, name);
}

bool World::destroy(Entity entity) {
    Record* root = impl_->record(entity);
    if (root == nullptr) {
        return false;
    }
    impl_->detach(entity, *root);
    // Collect the subtree first, without recursion, then delete it.
    std::vector<Entity> doomed{entity};
    for (std::size_t i = 0; i < doomed.size(); ++i) {
        const Record& record = impl_->records.at(doomed[i].id);
        doomed.insert(doomed.end(), record.children.begin(), record.children.end());
    }
    for (const Entity doomed_entity : doomed) {
        const auto found = impl_->records.find(doomed_entity.id);
        impl_->by_guid.erase(found->second.guid);
        impl_->records.erase(found);
        ecs_delete(impl_->ecs, doomed_entity.id);
    }
    return true;
}

bool World::is_alive(Entity entity) const {
    return impl_->record(entity) != nullptr;
}

std::size_t World::entity_count() const {
    return impl_->records.size();
}

Guid World::guid(Entity entity) const {
    const Record* record = impl_->record(entity);
    return record == nullptr ? Guid{} : record->guid;
}

Entity World::find(Guid guid) const {
    const auto found = impl_->by_guid.find(guid);
    return found == impl_->by_guid.end() ? Entity{} : found->second;
}

std::vector<Entity> World::find_named(std::string_view name) const {
    std::vector<std::pair<std::uint64_t, Entity>> found;
    for (const auto& [id, record] : impl_->records) {
        if (record.name == name) {
            found.emplace_back(record.order, Entity{id});
        }
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Entity> entities;
    entities.reserve(found.size());
    for (const auto& [order, entity] : found) {
        entities.push_back(entity);
    }
    return entities;
}

std::string_view World::name(Entity entity) const {
    const Record* record = impl_->record(entity);
    return record == nullptr ? std::string_view() : std::string_view(record->name);
}

bool World::set_name(Entity entity, std::string_view name) {
    Record* record = impl_->record(entity);
    if (record == nullptr) {
        return false;
    }
    record->name = std::string(name);
    return true;
}

// ---------------------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------------------

bool World::set_parent(Entity child, Entity parent) {
    Record* child_record = impl_->record(child);
    if (child_record == nullptr) {
        return false;
    }
    if (!parent.is_valid()) {
        impl_->detach(child, *child_record);
        return true;
    }
    Record* parent_record = impl_->record(parent);
    if (parent_record == nullptr) {
        return false;
    }
    // Refuse cycles: the parent must not be the child or one of its descendants. A child
    // without children has no descendants, which keeps building a hierarchy (and loading
    // a scene) linear; only moving a subtree walks up from the new parent.
    if (parent == child) {
        return false;
    }
    if (!child_record->children.empty()) {
        for (Entity up = parent; up.is_valid(); up = impl_->records.at(up.id).parent) {
            if (up == child) {
                return false;
            }
        }
    }
    impl_->detach(child, *child_record);
    child_record->parent = parent;
    parent_record->children.push_back(child);
    return true;
}

Entity World::parent(Entity entity) const {
    const Record* record = impl_->record(entity);
    return record == nullptr ? Entity{} : record->parent;
}

std::vector<Entity> World::children(Entity entity) const {
    const Record* record = impl_->record(entity);
    return record == nullptr ? std::vector<Entity>() : record->children;
}

std::vector<Entity> World::roots() const {
    std::vector<std::pair<std::uint64_t, Entity>> found;
    for (const auto& [id, record] : impl_->records) {
        if (!record.parent.is_valid()) {
            found.emplace_back(record.order, Entity{id});
        }
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Entity> roots;
    roots.reserve(found.size());
    for (const auto& [order, entity] : found) {
        roots.push_back(entity);
    }
    return roots;
}

// ---------------------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------------------

reflect::ObjectRef World::add(Entity entity, const reflect::TypeInfo& type) {
    const Component* component = impl_->find_component(type);
    if (component == nullptr || impl_->record(entity) == nullptr) {
        return {};
    }
    void* data = ecs_ensure_id(impl_->ecs, entity.id, component->id, component->ops.size);
    return reflect::ObjectRef::from_key(data, component->ops.key);
}

reflect::ObjectRef World::get(Entity entity, const reflect::TypeInfo& type) const {
    const Component* component = impl_->find_component(type);
    if (component == nullptr || impl_->record(entity) == nullptr ||
        !ecs_has_id(impl_->ecs, entity.id, component->id)) {
        return {};
    }
    void* data = ecs_get_mut_id(impl_->ecs, entity.id, component->id);
    return reflect::ObjectRef::from_key(data, component->ops.key);
}

bool World::remove(Entity entity, const reflect::TypeInfo& type) {
    const Component* component = impl_->find_component(type);
    if (component == nullptr || impl_->record(entity) == nullptr ||
        !ecs_has_id(impl_->ecs, entity.id, component->id)) {
        return false;
    }
    ecs_remove_id(impl_->ecs, entity.id, component->id);
    return true;
}

std::vector<const reflect::TypeInfo*> World::components(Entity entity) const {
    std::vector<const reflect::TypeInfo*> types;
    if (impl_->record(entity) == nullptr) {
        return types;
    }
    for (const auto& component : impl_->components) {
        if (ecs_has_id(impl_->ecs, entity.id, component->id)) {
            types.push_back(component->type);
        }
    }
    return types;
}

void World::each(const reflect::TypeInfo& type,
                 const std::function<void(Entity, reflect::ObjectRef)>& visit) const {
    const Component* component = impl_->find_component(type);
    if (component == nullptr) {
        return;
    }
    // Collect first, so that a visitor cannot invalidate the iteration.
    std::vector<Entity> found;
    ecs_iter_t it = ecs_each_id(impl_->ecs, component->id);
    while (ecs_each_next(&it)) {
        for (std::int32_t i = 0; i < it.count; ++i) {
            found.push_back(Entity{it.entities[i]});
        }
    }
    for (const Entity entity : found) {
        if (impl_->record(entity) != nullptr) {
            visit(entity, get(entity, type));
        }
    }
}

} // namespace engine::world
