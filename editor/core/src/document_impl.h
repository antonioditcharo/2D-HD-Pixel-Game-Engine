#pragma once

// Document's private state, shared by the files that implement it (document.cpp for the
// scene, logic_edit.cpp for logic). Not part of editor/core's interface.

#include "engine/serialize/json.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "editor/core/document.h"

namespace editor {

/// One step of undo: the project's editable state before and after it (Impl::snapshot()).
struct Step {
    std::string label;
    engine::serialize::Json before;
    engine::serialize::Json after;
    // What a merged step changes: the next change to the same thing extends it.
    engine::Guid entity;
    std::string property; ///< "component/property"; empty if the step does not merge.
};

struct Document::Impl {
    std::filesystem::path folder;
    engine::game::ProjectSettings settings;
    engine::reflect::Registry registry;
    std::unique_ptr<engine::assets::AssetDatabase> assets;
    std::unique_ptr<engine::world::World> world;
    std::string scene_path;
    /// What the entities of the project's other scenes are made of, by each name logic can
    /// use for them: Rules about them are checked and read as the game will run them.
    std::map<std::string, std::vector<const engine::reflect::TypeInfo*>, std::less<>> elsewhere;
    /// Reads `elsewhere` again from the scene files (all but the open one).
    void read_elsewhere();
    /// Loads the scene file at `path` (in assets/) into the world, replacing what is
    /// there; false with `issues` if it cannot be loaded, changing nothing.
    bool load_scene(const std::string& path, std::vector<engine::serialize::Issue>& issues);
    /// The project's logic files that could be read, sorted by path, and the problems
    /// reading the others.
    std::vector<LogicFile> logic;
    std::vector<engine::serialize::Issue> logic_issues;
    std::vector<Step> undo;
    std::vector<Step> redo;
    bool merging = false;
    /// The state as last saved, to know whether it changed and which files to write.
    engine::serialize::Json saved;

    /// Everything an edit can change: {"scene": the scene file's JSON, "logic": {path:
    /// each logic file's JSON}}.
    [[nodiscard]] engine::serialize::Json snapshot() const;
    /// Puts back a snapshot: the world and the logic files.
    void restore(const engine::serialize::Json& state);
    /// Records an edit made since `before`. Merges into the last step if it changes the
    /// same thing (`entity` and `property`) and merging has not been ended.
    void record(std::string label, engine::serialize::Json before,
                engine::Guid entity = engine::Guid{}, const std::string& property = {});

    [[nodiscard]] engine::world::Entity find(engine::Guid guid) const { return world->find(guid); }
    /// "hero", or "an entity" when it has no name.
    [[nodiscard]] std::string name_of(engine::Guid guid) const;
    /// The logic file at `path`, or null.
    [[nodiscard]] LogicFile* logic_file(std::string_view path);
    [[nodiscard]] const LogicFile* logic_file(std::string_view path) const;
};

} // namespace editor
