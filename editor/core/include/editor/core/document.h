#pragma once

// The editor's model of an open project (plan/06): the project's settings and assets,
// every reflected type, and the scene being edited, with undo and redo. It has no user
// interface, so it is tested on its own; the Qt application (editor/app) shows it.
//
// Undo works by snapshots: each edit records the scene and the logic files before and
// after it, as the JSON their files hold, and undoing loads the earlier one. Entities are found
// again by GUID, which survives that. Consecutive changes to one property of one entity (typing
// into a field, dragging) merge into a single step.
//
// See docs/dev/editor.md.

#include "engine/assets/assets.h"
#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"
#include "engine/game/runtime.h"
#include "engine/logic/compiler.h"
#include "engine/logic/ir.h"
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

/// A logic asset of the project, and where it is.
struct LogicFile {
    std::string path; ///< Relative to assets/.
    engine::logic::LogicAsset asset;
};

/// A value written into a logic step that can be changed in place: the text a character
/// says, how many seconds to wait, the sound to play.
struct LogicSlot {
    std::size_t argument = 0; ///< Which of the step's arguments it is.
    std::string label;        ///< "Seconds", or the parameter's display name.
    engine::reflect::ValueKind kind = engine::reflect::ValueKind::Text;
    engine::reflect::Value value;
    engine::reflect::NumericLimits limits;
    const engine::reflect::ChoiceInfo* choice{nullptr}; ///< For a choice.
};

/// An action a new step can do: `member` of `object`, reading as `text`.
struct LogicAction {
    std::string object;
    std::string member;
    std::string text; ///< As the block reads, for example "Say {text}".
};

/// An event a Rule can start on: `event` of `object` (empty for the game's own, such as
/// "game_start"), reading as `text`, for example "When [who] interacts with it".
struct LogicEvent {
    std::string object;
    std::string event;
    std::string text;
};

/// The objects a game gives its logic besides its entities, each bound by the runtime to
/// the registered type of the same name.
inline constexpr std::array<std::string_view, 5> game_objects{"audio", "dialogue", "game", "input",
                                                              "lighting"};

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
    /// Changes the project's settings (its look, light, camera, size...), checked as the
    /// game checks its project file. The start scene stays as it is. Returns false,
    /// changing nothing, with `error` in plain language, if a value is out of range.
    /// Consecutive changes merge into one step until end_merge(), as typing does.
    bool set_settings(const engine::game::ProjectSettings& settings, std::string& error);
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

    // ----- Logic. Its edits are steps of the same undo, and saved with the scene. -----

    /// The project's logic files, sorted by path, with any edits.
    [[nodiscard]] const std::vector<LogicFile>& logic() const noexcept;
    /// Problems reading logic files when the project was opened; those files are left out.
    [[nodiscard]] const std::vector<engine::serialize::Issue>& logic_issues() const noexcept;
    /// The values written into `block` of the logic file at `path` that can be changed in
    /// place: a step's or a condition's values, the name of a Sequence, Brain or state, or
    /// where a "Go to" leads and after how long. Empty if there are none, or no such block.
    [[nodiscard]] std::vector<LogicSlot> logic_slots(std::string_view path,
                                                     engine::logic::BlockId block) const;
    /// Changes one of those values. Returns false, changing nothing, with `error` in plain
    /// language, if the value is of the wrong kind or out of range, or a name is not a
    /// valid id or already taken. Renaming a Sequence or a state renames what refers to
    /// it ("Run", "Go to", the first state). Changes to the same value merge into one
    /// step, as typing does.
    bool set_logic_slot(std::string_view path, engine::logic::BlockId step, std::size_t argument,
                        const engine::reflect::Value& value, std::string& error);
    /// Every action a step can do: those of the game's objects and of the scene's named
    /// entities' components, by object, then as they read.
    [[nodiscard]] std::vector<LogicAction> logic_actions() const;
    /// Adds a step doing `action`, its values set to the parameters' defaults: after the
    /// step `after` (first inside it if it is an If or a Repeat), or first under the Rule,
    /// Sequence or Brain state `after`. Returns the new step's block id, or an invalid one
    /// with `error`. Besides the objects' actions, logic_actions() offers the built-in
    /// steps (object ""): log, repeat, run and wait.
    engine::logic::BlockId add_logic_step(std::string_view path, engine::logic::BlockId after,
                                          const LogicAction& action, std::string& error);
    /// Deletes a step, with any steps inside it.
    bool remove_logic_step(std::string_view path, engine::logic::BlockId step);
    /// Adds an "If" step (steps added while it is selected go inside it), or with
    /// `wait_until` a "Wait until" step, testing `condition` (one of logic_conditions())
    /// with its values the defaults. Placed as add_logic_step() places steps.
    engine::logic::BlockId add_logic_branch(std::string_view path, engine::logic::BlockId after,
                                            const LogicAction& condition, bool wait_until,
                                            std::string& error);

    /// Every event a Rule can start on: the game starting, and the events of the game's
    /// objects and of the scene's named entities' components.
    [[nodiscard]] std::vector<LogicEvent> logic_events() const;
    /// Every condition a Rule or a "Go to" can check, as logic_actions() lists actions.
    [[nodiscard]] std::vector<LogicAction> logic_conditions() const;
    /// Adds a Rule starting on `event`, last in the file. Returns its block id, or an
    /// invalid one with `error`.
    engine::logic::BlockId add_logic_rule(std::string_view path, const LogicEvent& event,
                                          std::string& error);
    /// Adds `condition`, its values the defaults, to the Rule or "Go to" `block` is (or
    /// is a condition of). All of a Rule's conditions must hold.
    engine::logic::BlockId add_logic_condition(std::string_view path, engine::logic::BlockId block,
                                               const LogicAction& condition, std::string& error);
    /// Adds a Sequence named "sequence_1" (or the next number free).
    engine::logic::BlockId add_logic_sequence(std::string_view path, std::string& error);
    /// Adds a Brain named "brain_1" (or the next free) with one state, "idle".
    engine::logic::BlockId add_logic_brain(std::string_view path, std::string& error);
    /// Adds a state "state_1" (or the next free) to the Brain `block` is, or is in.
    engine::logic::BlockId add_logic_state(std::string_view path, engine::logic::BlockId block,
                                           std::string& error);
    /// Adds a "Go to" out of the state `block` is (or leads out of), towards the next
    /// state, after a second.
    engine::logic::BlockId add_logic_transition(std::string_view path, engine::logic::BlockId block,
                                                std::string& error);
    /// Deletes any block: a step, a condition, a Rule, a Sequence, a Brain, a state or a
    /// "Go to", with everything inside it.
    bool remove_logic_block(std::string_view path, engine::logic::BlockId block);
    /// True if `block` is a block of the file that remove_logic_block() can delete.
    [[nodiscard]] bool is_logic_block(std::string_view path, engine::logic::BlockId block) const;
    /// Creates an empty Rules file at `path` in assets/ (such as "logic/quests.drules"),
    /// written when the project is saved. Refused, with `error`, if the name is not
    /// "<folder/>name.drules" with a valid id as its name, or the file exists.
    bool create_logic_file(std::string_view path, std::string& error);
    /// Moves a step one place up (-1) or down (+1) among its neighbours.
    bool move_logic_step(std::string_view path, engine::logic::BlockId step, int by);
    /// True if `block` is a step of the logic file (one that can be deleted or moved).
    [[nodiscard]] bool is_logic_step(std::string_view path, engine::logic::BlockId block) const;
    /// What stops the logic file at `path` from running, checked by the compiler against
    /// the registry and the scene, each problem on its block. Empty if it compiles.
    [[nodiscard]] std::vector<engine::logic::Diagnostic> check_logic(std::string_view path) const;

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
    /// The slots of a block that is not a step (see logic_slots()).
    [[nodiscard]] std::vector<LogicSlot> header_slots(engine::logic::LogicAsset& asset,
                                                      engine::logic::BlockId block) const;
    /// The objects logic can name: the game's, and the scene's named entities, sorted.
    [[nodiscard]] std::vector<std::string> logic_objects() const;
    /// The functions of those objects in `role`, by object, then as they read.
    [[nodiscard]] std::vector<LogicAction>
    logic_functions(engine::reflect::FunctionRole role) const;
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
