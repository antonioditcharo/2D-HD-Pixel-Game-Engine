#include "engine/game/logic_world.h"

#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"

#include <algorithm>
#include <cctype>
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

std::vector<std::string> tags_of(const world::World& world, world::Entity entity) {
    std::vector<std::string> out;
    const Tags* tags = world.is_alive(entity) && world.registry().find_type<Tags>() != nullptr
                           ? world.get<Tags>(entity)
                           : nullptr;
    if (tags == nullptr) {
        return out;
    }
    std::string word;
    const auto keep = [&out, &word] {
        if (!word.empty() && std::ranges::find(out, word) == out.end()) {
            out.push_back(word);
        }
        word.clear();
    };
    for (const char c : tags->tags) {
        if (c == ',' || c == ' ' || c == ';' || c == '\t') {
            keep();
        } else {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    keep();
    return out;
}

std::vector<std::string> logic_names(const world::World& world, world::Entity entity) {
    std::vector<std::string> names = names_of(world, entity);
    for (const std::string& tag : tags_of(world, entity)) {
        names.push_back("#" + tag);
    }
    return names;
}

std::vector<world::Entity> tagged(const world::World& world, std::string_view tag) {
    std::string wanted;
    for (const char c : tag) {
        wanted += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::vector<world::Entity> out;
    const reflect::TypeInfo* type = world.registry().find_type<Tags>();
    if (type == nullptr || wanted.empty()) {
        return out;
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef) {
        const std::vector<std::string> tags = tags_of(world, entity);
        if (std::ranges::find(tags, wanted) != tags.end()) {
            out.push_back(entity);
        }
    });
    return out;
}

bool emit(logic::Vm& vm, const world::World& world, world::Entity entity, std::string_view event,
          std::span<const reflect::Value> payload) {
    bool delivered = false;
    for (const std::string& name : names_of(world, entity)) {
        delivered = vm.emit(name, event, payload) || delivered;
    }
    // Rules about its tags, with the entity itself as "it".
    const std::vector<std::string> tags = tags_of(world, entity);
    if (!tags.empty()) {
        std::vector<reflect::Value> with_it(payload.begin(), payload.end());
        with_it.emplace_back(world.guid(entity).to_string());
        for (const std::string& tag : tags) {
            delivered = vm.emit("#" + tag, event, with_it) || delivered;
        }
    }
    return delivered;
}

void attach(logic::Vm& vm, world::World& world) {
    // "For each object tagged ...": the tagged entities, by GUID.
    vm.set_group_resolver([&world](std::string_view tag) {
        std::vector<std::string> names;
        for (const world::Entity entity : tagged(world, tag)) {
            names.push_back(world.guid(entity).to_string());
        }
        return names;
    });
    vm.set_object_resolver([&world](std::string_view name) {
        std::vector<reflect::ObjectRef> parts;
        // A tag: its members' parts, for checking the events Rules about it take.
        if (name.starts_with('#')) {
            for (const world::Entity member : tagged(world, name.substr(1))) {
                for (const reflect::TypeInfo* type : world.components(member)) {
                    parts.push_back(world.get(member, *type));
                }
            }
            return parts;
        }
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
        // A tag ("#enemy"): what every object tagged so has, each kind once.
        if (name.starts_with('#')) {
            std::vector<const reflect::TypeInfo*> types;
            for (const world::Entity entity : tagged(world, name.substr(1))) {
                for (const reflect::TypeInfo* type : world.components(entity)) {
                    if (std::ranges::find(types, type) == types.end()) {
                        types.push_back(type);
                    }
                }
            }
            return types;
        }
        const world::Entity entity = resolve(world, name);
        return entity.is_valid() ? world.components(entity)
                                 : std::vector<const reflect::TypeInfo*>{};
    };
}

} // namespace engine::game
