#include "engine/foundation/text.h"
#include "engine/serialize/json.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "detail.h"

namespace engine::serialize {

namespace {

using detail::child;
using detail::describe;
using detail::positive_integer;
using text::quote;

/// One entity of a scene document, after the check pass.
struct Planned {
    Guid guid;
    std::string name;
    Guid parent;
    const Json* components{nullptr};
    std::string path;
};

} // namespace

namespace {

/// Entities from `roots` down, parents first, as scene documents list them.
Json entities_json(const world::World& world, std::vector<world::Entity> pending,
                   world::Entity root_without_parent) {
    Json entities = Json::array();
    // Parents first, without recursion: a stack of entities still to write.
    std::reverse(pending.begin(), pending.end());
    while (!pending.empty()) {
        const world::Entity entity = pending.back();
        pending.pop_back();

        Json item = Json::object();
        item["guid"] = world.guid(entity).to_string();
        if (!world.name(entity).empty()) {
            item["name"] = std::string(world.name(entity));
        }
        if (const world::Entity parent = world.parent(entity);
            parent.is_valid() && entity != root_without_parent) {
            item["parent"] = world.guid(parent).to_string();
        }
        Json components = Json::object();
        for (const reflect::TypeInfo* type : world.components(entity)) {
            components[type->name] = object_to_json(world.get(entity, *type), *type);
        }
        if (!components.empty()) {
            item["components"] = std::move(components);
        }
        entities.push_back(std::move(item));

        const std::vector<world::Entity> children = world.children(entity);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    Json out = Json::object();
    out["format"] = scene_format;
    out["version"] = scene_version;
    out["entities"] = std::move(entities);
    return out;
}

} // namespace

Json scene_to_json(const world::World& world) {
    return entities_json(world, world.roots(), world::Entity{});
}

Json subtree_to_json(const world::World& world, world::Entity root) {
    return entities_json(
        world, root.is_valid() ? std::vector<world::Entity>{root} : std::vector<world::Entity>{},
        root);
}

Json with_new_guids(const Json& scene) {
    Json out = scene;
    if (!out.is_object() || !out.contains("entities") || !out["entities"].is_array()) {
        return out;
    }
    std::map<std::string, std::string> renamed;
    for (Json& item : out["entities"]) {
        if (item.is_object() && item.contains("guid") && item["guid"].is_string()) {
            const std::string fresh = Guid::generate().to_string();
            renamed[item["guid"].get<std::string>()] = fresh;
            item["guid"] = fresh;
        }
    }
    for (Json& item : out["entities"]) {
        if (item.is_object() && item.contains("parent") && item["parent"].is_string()) {
            const auto found = renamed.find(item["parent"].get<std::string>());
            if (found != renamed.end()) {
                item["parent"] = found->second;
            }
        }
    }
    return out;
}

SceneLoad scene_from_json(const Json& json, world::World& world) {
    SceneLoad result;
    std::vector<Issue>& issues = result.issues;
    if (!json.is_object()) {
        issues.push_back(
            Issue{"", "This is not a scene: it must be a group of named values (a JSON object)."});
        return result;
    }
    const auto format = json.find("format");
    if (format == json.end() || !format->is_string() ||
        format->get_ref<const std::string&>() != scene_format) {
        issues.push_back(Issue{"/format", "This is not a scene: its format is not " +
                                              quote(scene_format) + "."});
        return result;
    }
    const auto version = json.find("version");
    const std::optional<std::uint64_t> file_version =
        version == json.end() ? std::nullopt : positive_integer(*version);
    if (!file_version) {
        issues.push_back(
            Issue{"/version", "The format version is missing or not a whole number above 0."});
        return result;
    }
    if (*file_version > static_cast<std::uint64_t>(scene_version)) {
        issues.push_back(Issue{
            "/version", "This scene was saved by a newer version of Sparxel (format version " +
                            std::to_string(*file_version) + "). This version reads up to " +
                            std::to_string(scene_version) + ". Update Sparxel to open it."});
        return result;
    }
    for (const auto& [key, value] : json.items()) {
        if (key != "format" && key != "version" && key != "entities") {
            issues.push_back(
                Issue{child("", key), "Unknown entry " + quote(key) + ". It was ignored."});
        }
    }
    const auto entities = json.find("entities");
    if (entities == json.end()) {
        return result; // An empty scene.
    }
    if (!entities->is_array()) {
        issues.push_back(
            Issue{"/entities", "This must be a list, but is " + describe(*entities) + "."});
        return result;
    }

    // Check pass: nothing is created unless the whole hierarchy can be built faithfully.
    std::vector<Planned> planned;
    std::unordered_set<Guid, GuidHash> seen;
    bool ok = true;
    for (std::size_t i = 0; i < entities->size(); ++i) {
        const Json& item = (*entities)[i];
        Planned entry;
        entry.path = child("/entities", i);
        if (!item.is_object()) {
            issues.push_back(
                Issue{entry.path,
                      "An entity must be a group of named values, but is " + describe(item) + "."});
            ok = false;
            continue;
        }
        for (const auto& [key, value] : item.items()) {
            if (key != "guid" && key != "name" && key != "parent" && key != "components") {
                issues.push_back(Issue{child(entry.path, key),
                                       "Unknown entry " + quote(key) + ". It was ignored."});
            }
        }
        const auto guid = item.find("guid");
        const std::optional<Guid> parsed = guid != item.end() && guid->is_string()
                                               ? Guid::parse(guid->get_ref<const std::string&>())
                                               : std::nullopt;
        if (!parsed) {
            issues.push_back(
                Issue{child(entry.path, "guid"),
                      "Every entity needs a GUID of 32 hexadecimal digits, but this one is " +
                          (guid == item.end() ? std::string("missing") : describe(*guid)) + "."});
            ok = false;
            continue;
        }
        entry.guid = *parsed;
        if (!seen.insert(entry.guid).second) {
            issues.push_back(Issue{child(entry.path, "guid"),
                                   "The GUID " + entry.guid.to_string() +
                                       " is used by an earlier entity in this scene."});
            ok = false;
        } else if (world.find(entry.guid).is_valid()) {
            issues.push_back(
                Issue{child(entry.path, "guid"),
                      "The GUID " + entry.guid.to_string() + " is already used in the world."});
            ok = false;
        }
        if (const auto name = item.find("name"); name != item.end()) {
            if (name->is_string()) {
                entry.name = name->get<std::string>();
            } else {
                issues.push_back(Issue{child(entry.path, "name"),
                                       "A name must be text, but is " + describe(*name) + "."});
                ok = false;
            }
        }
        if (const auto parent = item.find("parent"); parent != item.end()) {
            const std::optional<Guid> parent_guid =
                parent->is_string() ? Guid::parse(parent->get_ref<const std::string&>())
                                    : std::nullopt;
            if (!parent_guid) {
                issues.push_back(Issue{child(entry.path, "parent"),
                                       "A parent must be the GUID of another entity, but is " +
                                           describe(*parent) + "."});
                ok = false;
            } else if (*parent_guid == entry.guid ||
                       (!seen.contains(*parent_guid) && !world.find(*parent_guid).is_valid())) {
                issues.push_back(
                    Issue{child(entry.path, "parent"),
                          "The parent " + parent_guid->to_string() +
                              " is neither an earlier entity in this scene nor in the world."});
                ok = false;
            } else {
                entry.parent = *parent_guid;
            }
        }
        if (const auto components = item.find("components"); components != item.end()) {
            if (components->is_object()) {
                entry.components = &*components;
            } else {
                issues.push_back(Issue{child(entry.path, "components"),
                                       "Components must be a group of named values, but are " +
                                           describe(*components) + "."});
                ok = false;
            }
        }
        planned.push_back(std::move(entry));
    }
    if (!ok) {
        return result;
    }

    // Create pass.
    for (const Planned& entry : planned) {
        const world::Entity entity = world.create(entry.guid, entry.name);
        result.entities.push_back(entity);
        if (entry.parent.is_valid()) {
            world.set_parent(entity, world.find(entry.parent));
        }
        if (entry.components == nullptr) {
            continue;
        }
        const std::string components_path = child(entry.path, "components");
        for (const auto& [type_name, properties] : entry.components->items()) {
            const std::string where = child(components_path, type_name);
            const reflect::TypeInfo* type = world.registry().find_type(type_name);
            if (type == nullptr || !world.is_component(*type)) {
                issues.push_back(Issue{where, "There is no component type called " +
                                                  quote(type_name) + ". It was ignored."});
                continue;
            }
            const reflect::ObjectRef component = world.add(entity, *type);
            std::vector<Issue> found = object_from_json(properties, component, *type, where);
            issues.insert(issues.end(), std::make_move_iterator(found.begin()),
                          std::make_move_iterator(found.end()));
        }
    }
    return result;
}

} // namespace engine::serialize
