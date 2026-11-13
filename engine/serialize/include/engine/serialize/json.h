#pragma once

// JSON serialization for the project format (ADR-011, plan/03 §4): reflected objects and
// logic assets to and from JSON text.
//
// - Deterministic: saving the same content twice gives byte-identical text, because object
//   keys are always sorted.
// - Never throws for bad input: every problem is reported as an Issue with the place it
//   was found, and loading continues where it safely can.
// - Versioned: every file carries a format name and a schema version.
//
// See docs/dev/serialization.md.

#include "engine/foundation/reflect.h"
#include "engine/logic/ir.h"
#include "engine/world/world.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::serialize {

/// A JSON value. Objects keep their keys sorted, which makes output deterministic.
using Json = nlohmann::json;

/// A problem found while reading.
struct Issue {
    /// Where, as a JSON Pointer (RFC 6901), for example "/rules/3/do/0". Empty for the
    /// whole document.
    std::string path;
    std::string message; ///< Plain language.

    friend bool operator==(const Issue&, const Issue&) = default;
};

// ---------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------

/// Parses JSON text. Returns nothing, with an issue, if it is not valid JSON.
[[nodiscard]] std::optional<Json> parse(std::string_view text, std::vector<Issue>& issues);

/// Formats JSON as text: two-space indentation, sorted keys, a final newline, and UTF-8
/// written as is. Invalid UTF-8 in strings is replaced rather than failing.
[[nodiscard]] std::string to_text(const Json& json);

// ---------------------------------------------------------------------------------------
// Reflected objects
// ---------------------------------------------------------------------------------------

/// The properties of `object` as a JSON object keyed by property id. Choices are written
/// as their entry id, so files stay valid when an enum's values are renumbered.
/// Read-only properties are not written: they are computed or set by the game, not
/// authored. Returns an empty object for a null reference or a type mismatch.
[[nodiscard]] Json object_to_json(reflect::ObjectRef object, const reflect::TypeInfo& type);

/// Reads properties from `json` into `object`. Properties missing from `json` keep their
/// current value. Every key that cannot be applied (unknown, read-only, wrong kind, out of
/// range, unknown choice) leaves that property unchanged and is reported. `path` is
/// prefixed to the issues' paths.
[[nodiscard]] std::vector<Issue> object_from_json(const Json& json, reflect::ObjectRef object,
                                                  const reflect::TypeInfo& type,
                                                  std::string_view path = "");

// ---------------------------------------------------------------------------------------
// Logic assets
// ---------------------------------------------------------------------------------------

/// The format name and current schema version written into logic asset files.
inline constexpr std::string_view logic_format = "sparxel.logic";
inline constexpr int logic_version = 1;

[[nodiscard]] Json logic_to_json(const logic::LogicAsset& asset);

struct LogicLoad {
    /// The asset, or nothing if the document is not a usable logic asset.
    std::optional<logic::LogicAsset> asset;
    /// Every problem found. An asset can load with issues, for example an unknown key,
    /// which is reported and ignored.
    std::vector<Issue> issues;
};

/// Reads a logic asset, migrating older schema versions first. A document from a newer
/// version of Sparxel is refused rather than read wrongly.
[[nodiscard]] LogicLoad logic_from_json(const Json& json);

// ---------------------------------------------------------------------------------------
// Scenes
// ---------------------------------------------------------------------------------------

inline constexpr std::string_view scene_format = "sparxel.scene";
inline constexpr int scene_version = 1;

/// Every entity of `world` with its GUID, name, parent and components. Entities are
/// listed parents first: roots in creation order, each followed by its descendants in
/// order. Components are keyed by their registered type name.
[[nodiscard]] Json scene_to_json(const world::World& world);
/// The same for one entity and its descendants only (a prefab): `root` first, without its
/// parent.
[[nodiscard]] Json subtree_to_json(const world::World& world, world::Entity root);
/// A copy of a scene document with every GUID replaced by a new one, parents following,
/// so it can be added to a world that has the originals: a prefab placed again.
[[nodiscard]] Json with_new_guids(const Json& scene);

struct SceneLoad {
    /// The entities created, in the order of the file. Empty if the scene was refused.
    std::vector<world::Entity> entities;
    std::vector<Issue> issues;
};

/// Adds the entities of a scene document to `world`. The whole document is checked
/// first, and nothing is created if the scene cannot be loaded faithfully: an invalid or
/// repeated GUID, a GUID already in the world, or a parent that is neither earlier in the
/// file nor already in the world. Problems inside components (an unknown component type,
/// a property that does not fit) are reported, and the rest of the entity still loads.
[[nodiscard]] SceneLoad scene_from_json(const Json& json, world::World& world);

} // namespace engine::serialize
