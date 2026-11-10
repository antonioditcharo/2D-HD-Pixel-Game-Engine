#include "engine/game/logic_world.h"

#include "engine/foundation/guid.h"

#include <optional>
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
