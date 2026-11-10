#include "engine/game/logic_world.h"

#include "engine/foundation/guid.h"

#include <optional>
#include <string>
#include <vector>

namespace engine::game {

world::Entity resolve(const world::World& world, std::string_view name) {
    if (const std::optional<Guid> guid = Guid::parse(name)) {
        if (const world::Entity entity = world.find(*guid); entity.is_valid()) {
            return entity;
        }
    }
    const std::vector<world::Entity> named = world.find_named(name);
    return named.size() == 1 ? named.front() : world::Entity{};
}

std::vector<std::string> names_of(const world::World& world, world::Entity entity) {
    std::vector<std::string> names;
    if (!world.is_alive(entity)) {
        return names;
    }
    names.push_back(world.guid(entity).to_string());
    const std::string_view name = world.name(entity);
    if (!name.empty() && resolve(world, name) == entity && name != names.front()) {
        names.emplace_back(name);
    }
    return names;
}

bool emit(logic::Vm& vm, const world::World& world, world::Entity entity, std::string_view event,
          std::span<const reflect::Value> payload) {
    bool delivered = false;
    for (const std::string& name : names_of(world, entity)) {
        delivered = vm.emit(name, event, payload) || delivered;
    }
    return delivered;
}

void attach(logic::Vm& vm, world::World& world) {
    vm.set_object_resolver([&world](std::string_view name) {
        std::vector<reflect::ObjectRef> parts;
        const world::Entity entity = resolve(world, name);
        if (!entity.is_valid()) {
            return parts;
        }
        for (const reflect::TypeInfo* type : world.components(entity)) {
            parts.push_back(world.get(entity, *type));
        }
        return parts;
    });
}

logic::ObjectTypes object_types(const world::World& world) {
    return [&world](std::string_view name) {
        const world::Entity entity = resolve(world, name);
        return entity.is_valid() ? world.components(entity)
                                 : std::vector<const reflect::TypeInfo*>{};
    };
}

} // namespace engine::game
