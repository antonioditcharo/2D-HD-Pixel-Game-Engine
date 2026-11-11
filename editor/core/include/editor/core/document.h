#pragma once

// The editor's model of an open project (plan/06): the project's settings and assets,
// every reflected type, and the scene being edited, with undo and redo. It has no user
// interface, so it is tested on its own; the Qt application (editor/app) shows it.
//
// Undo works by snapshots: each edit records the scene before and after it, as the JSON
// the scene file holds, and undoing loads the earlier one. Entities are found again by
// GUID, which survives that. Consecutive changes to one property of one entity (typing
// into a field, dragging) merge into a single step.
//
// See docs/dev/editor.md.

#include "engine/assets/assets.h"
#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"
#include "engine/game/runtime.h"
#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"
#include "engine/world/world.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

class Document {
public:
    /// Opens the project in `folder` and its start scene. Returns null, with issues in
    /// plain language, if either cannot be read. Problems that do not stop editing (an
    /// unknown setting, an asset issue, a component that could not be loaded) are added to
    /// `issues` too.
    [[nodiscard]] static std::unique_ptr<Document>
    open(const std::filesystem::path& folder, std::vector<engine::serialize::Issue>& issues);

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;
    ~Document();

    [[nodiscard]] const std::filesystem::path& folder() const noexcept;
    [[nodiscard]] const engine::game::ProjectSettings& settings() const noexcept;
    [[nodiscard]] const engine::reflect::Registry& registry() const noexcept;
    [[nodiscard]] engine::assets::AssetDatabase& assets() noexcept;
    [[nodiscard]] engine::world::World& world() noexcept;
    [[nodiscard]] const engine::world::World& world() const noexcept;
    /// The scene file being edited, relative to assets/.
    [[nodiscard]] const std::string& scene_path() const noexcept;

    /// Every entity, parents before their children, in the scene's order.
    [[nodiscard]] std::vector<engine::world::Entity> entities() const;
    /// The component types an entity can have, by display name.
    [[nodiscard]] std::vector<const engine::reflect::TypeInfo*> component_types() const;

    // ----- Edits. Each is one step of undo, and marks the scene changed. -----

    /// Sets a property of an entity's component. Returns false, changing nothing, with
    /// `error` in plain language, if the value is refused (read-only, out of range, ...).
    bool set_property(engine::Guid entity, const engine::reflect::TypeInfo& component,
                      const engine::reflect::PropertyInfo& property,
                      const engine::reflect::Value& value, std::string& error);
    /// Moves an entity's Position to (x, y). Moves of one entity merge, as a drag does.
    bool move_entity(engine::Guid entity, double x, double y);
    /// Paints `tile` (-1 erases) into the cell of `tilemap` at map point (x, y). The map
    /// grows to the right and down to reach it, up to scene2d::max_tilemap_side; cells
    /// above or left of its corner are refused. A stroke of painting on one map is one
    /// undo step (end_merge() ends it). Returns false if nothing could be painted there.
    bool paint_tile(engine::Guid tilemap, double x, double y, std::int32_t tile);
    /// Creates an entity with a Position at (x, y). Returns its GUID.
    engine::Guid create_entity(std::string_view name, double x, double y);
    /// Places a picture from the assets (its path in assets/, such as "chars/hero.png")
    /// at map point (x, y), snapped to whole pixels: a new entity named after the file,
    /// with a Position and a Sprite showing it, as one undo step. Returns its GUID, or
    /// an invalid one, with `error` in plain language, if the asset is not a picture.
    engine::Guid place_picture(std::string_view asset_path, double x, double y, std::string& error);
    bool delete_entity(engine::Guid entity);
    bool rename(engine::Guid entity, std::string_view name);
    bool add_component(engine::Guid entity, const engine::reflect::TypeInfo& component);
    bool remove_component(engine::Guid entity, const engine::reflect::TypeInfo& component);

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    /// What the next undo or redo does, for example "Change Speed of hero"; empty if none.
    [[nodiscard]] std::string undo_label() const;
    [[nodiscard]] std::string redo_label() const;
    bool undo();
    bool redo();
    /// Ends the current merge, so the next change to the same property is a new step
    /// (when a field loses focus, or a drag ends).
    void end_merge() noexcept;

    /// True if the scene has changes that are not saved.
    [[nodiscard]] bool dirty() const noexcept;
    /// Writes the scene to its file, atomically. Returns false with `error` if it could not.
    bool save(std::string& error);

private:
    struct Impl;
    explicit Document(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/// Where an entity's sprite picture covers the map, in tiles, as Classic 2D draws it;
/// nothing if it has no sprite with a readable picture.
[[nodiscard]] std::optional<engine::scene2d::Box>
sprite_bounds(const engine::world::World& world, const engine::assets::AssetDatabase& assets,
              engine::world::Entity entity, std::uint32_t ppu);

/// The entity drawn topmost at map point (x, y) in Classic 2D: sprites first (the
/// last-drawn one whose picture covers the point), then tilemaps with a tile there.
/// Invalid if there is none. `ppu` sizes pictures, as the project does.
[[nodiscard]] engine::world::Entity pick(const engine::world::World& world,
                                         const engine::assets::AssetDatabase& assets, double x,
                                         double y, std::uint32_t ppu);

/// The Diorama camera the game uses for `settings`, looking at map point (x, y).
[[nodiscard]] engine::scene2d::DioramaCamera
diorama_camera(const engine::game::ProjectSettings& settings, double x, double y);

/// Where an entity's sprite appears in a Diorama camera's image, as the smallest
/// rectangle around its billboard: {left, bottom, right, top} with x and y from -1 to 1
/// (y up). Nothing without a sprite with a readable picture, or behind the eye.
[[nodiscard]] std::optional<std::array<double, 4>>
diorama_sprite_rect(const engine::world::World& world, const engine::assets::AssetDatabase& assets,
                    engine::world::Entity entity, const engine::scene2d::DioramaCamera& camera,
                    float aspect);

/// The entity under image point (ndc_x, ndc_y) of a Diorama camera: the nearest sprite
/// whose billboard covers it, else the tilemap with a tile where the ray meets the
/// ground. Invalid if there is none.
[[nodiscard]] engine::world::Entity pick_diorama(const engine::world::World& world,
                                                 const engine::assets::AssetDatabase& assets,
                                                 const engine::scene2d::DioramaCamera& camera,
                                                 float aspect, double ndc_x, double ndc_y);

} // namespace editor
