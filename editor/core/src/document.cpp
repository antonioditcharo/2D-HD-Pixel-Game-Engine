#include "editor/core/document.h"

#include "engine/game/behaviours.h"
#include "engine/game/logic_world.h"
#include "engine/game/save.h"
#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#include "document_impl.h"

namespace editor {

namespace fs = std::filesystem;
namespace game = engine::game;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace serialize = engine::serialize;
namespace world = engine::world;
using engine::Guid;
using serialize::Issue;

namespace {

/// Every entity under `entity`, depth first, `entity` included.
void collect(const world::World& w, world::Entity entity, std::vector<world::Entity>& out) {
    out.push_back(entity);
    for (const world::Entity child : w.children(entity)) {
        collect(w, child, out);
    }
}

/// Every logic asset in `assets` (.drules and .dsequence files), sorted by path; the
/// problems of those that cannot be read go to `issues`.
std::vector<LogicFile> read_logic_files(engine::assets::AssetDatabase& assets,
                                        std::vector<Issue>& issues) {
    std::vector<LogicFile> files;
    for (const engine::assets::AssetRecord& record : assets.assets()) {
        const std::string& path = record.path;
        if (!path.ends_with(".drules") && !path.ends_with(".dsequence")) {
            continue;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = assets.read(record.guid);
        std::vector<Issue> found;
        const std::optional<serialize::Json> json =
            bytes ? serialize::parse(
                        std::string_view(reinterpret_cast<const char*>(bytes->data()), // NOLINT
                                         bytes->size()),
                        found)
                  : std::nullopt;
        serialize::LogicLoad loaded =
            json ? serialize::logic_from_json(*json) : serialize::LogicLoad{};
        found.insert(found.end(), loaded.issues.begin(), loaded.issues.end());
        for (Issue& issue : found) {
            issue.path = "assets/" + path + issue.path;
            issues.push_back(std::move(issue));
        }
        if (std::optional<engine::logic::LogicAsset> asset = std::move(loaded.asset);
            asset.has_value()) {
            files.push_back(LogicFile{path, std::move(*asset)});
        } else if (found.empty()) {
            issues.push_back(Issue{"assets/" + path, "This logic could not be read."});
        }
    }
    std::sort(files.begin(), files.end(),
              [](const LogicFile& a, const LogicFile& b) { return a.path < b.path; });
    return files;
}

} // namespace

// ---------------------------------------------------------------------------------------
// The state behind undo
// ---------------------------------------------------------------------------------------

serialize::Json Document::Impl::snapshot() const {
    serialize::Json files = serialize::Json::object();
    for (const LogicFile& file : logic) {
        files[file.path] = serialize::logic_to_json(file.asset);
    }
    return serialize::Json{{"scene", serialize::scene_to_json(*world)},
                           {"logic", std::move(files)},
                           {"project", game::project_to_json(settings)}};
}

void Document::Impl::restore(const serialize::Json& state) {
    for (const world::Entity root : world->roots()) {
        world->destroy(root);
    }
    static_cast<void>(serialize::scene_from_json(state.at("scene"), *world));
    std::vector<Issue> ignored;
    if (std::optional<game::ProjectSettings> restored =
            game::project_from_json(state.at("project"), ignored)) {
        settings = std::move(*restored);
    }
    // The files as they were then: created ones come and go with undo too. (Keys are
    // sorted, so the files stay sorted by path.)
    logic.clear();
    for (const auto& [path, json] : state.at("logic").items()) {
        serialize::LogicLoad loaded = serialize::logic_from_json(json);
        if (loaded.asset.has_value()) {
            logic.push_back(LogicFile{path, std::move(*loaded.asset)});
        }
    }
}

void Document::Impl::record(std::string label, serialize::Json before, Guid entity,
                            const std::string& property) {
    serialize::Json after = snapshot();
    if (after == before) {
        return; // nothing changed
    }
    redo.clear();
    if (merging && !property.empty() && !undo.empty() && undo.back().entity == entity &&
        undo.back().property == property) {
        undo.back().after = std::move(after);
        return;
    }
    undo.push_back(Step{std::move(label), std::move(before), std::move(after), entity, property});
    merging = !property.empty();
}

std::string Document::Impl::name_of(Guid guid) const {
    const world::Entity entity = find(guid);
    const std::string_view name = entity.is_valid() ? world->name(entity) : "";
    return name.empty() ? std::string("an entity") : std::string(name);
}

LogicFile* Document::Impl::logic_file(std::string_view path) {
    for (LogicFile& file : logic) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

const LogicFile* Document::Impl::logic_file(std::string_view path) const {
    for (const LogicFile& file : logic) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

bool Document::Impl::load_scene(const std::string& path, std::vector<Issue>& issues) {
    const std::string file = "assets/" + path;
    std::ifstream in(folder / "assets" / path, std::ios::binary);
    if (!in) {
        issues.push_back(Issue{file, "The scene could not be opened."});
        return false;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<Issue> found;
    const std::optional<serialize::Json> json = serialize::parse(text, found);
    // Checked in a scratch world first, so a scene that cannot be loaded changes nothing.
    world::World scratch(registry);
    scene2d::register_components(scratch);
    game::register_behaviour_components(scratch);
    serialize::SceneLoad trial =
        json ? serialize::scene_from_json(*json, scratch) : serialize::SceneLoad{};
    if (!json || (trial.entities.empty() && !trial.issues.empty())) {
        for (std::vector<Issue>* list : {&found, &trial.issues}) {
            for (Issue& issue : *list) {
                issue.path = file + issue.path;
                issues.push_back(std::move(issue));
            }
        }
        return false;
    }
    for (const world::Entity root : world->roots()) {
        world->destroy(root);
    }
    serialize::SceneLoad loaded = serialize::scene_from_json(*json, *world);
    for (Issue& issue : loaded.issues) {
        issue.path = file + issue.path;
        issues.push_back(std::move(issue));
    }
    scene_path = path;
    read_elsewhere();
    return true;
}

void Document::Impl::read_elsewhere() {
    elsewhere.clear();
    for (const engine::assets::AssetRecord& record : assets->assets()) {
        if (!record.path.ends_with(".dscene") || record.path == scene_path) {
            continue;
        }
        std::ifstream in(folder / "assets" / record.path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        std::vector<Issue> ignored; // told when that scene is opened
        const std::optional<serialize::Json> json = serialize::parse(text, ignored);
        if (!json) {
            continue;
        }
        world::World scratch(registry);
        scene2d::register_components(scratch);
        game::register_behaviour_components(scratch);
        for (const world::Entity entity : serialize::scene_from_json(*json, scratch).entities) {
            for (const std::string& name : game::names_of(scratch, entity)) {
                std::vector<const reflect::TypeInfo*>& parts = elsewhere[name];
                for (const reflect::TypeInfo* type : scratch.components(entity)) {
                    if (std::find(parts.begin(), parts.end(), type) == parts.end()) {
                        parts.push_back(type);
                    }
                }
            }
        }
    }
}

Document::Document(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Document::~Document() = default;

std::unique_ptr<Document> Document::open(const fs::path& folder, std::vector<Issue>& issues) {
    auto impl = std::make_unique<Impl>();
    impl->folder = folder;
    std::optional<game::ProjectSettings> settings = game::read_project(folder, issues);
    if (!settings) {
        return nullptr;
    }
    impl->settings = std::move(*settings);
    game::register_types(impl->registry);
    impl->assets = std::make_unique<engine::assets::AssetDatabase>(folder / "assets");
    static_cast<void>(impl->assets->refresh());
    for (const engine::assets::AssetIssue& issue : impl->assets->issues()) {
        issues.push_back(Issue{"assets/" + issue.path, issue.message});
    }
    impl->world = std::make_unique<world::World>(impl->registry);
    scene2d::register_components(*impl->world);
    game::register_behaviour_components(*impl->world);

    if (!impl->load_scene(impl->settings.start_scene, issues)) {
        return nullptr;
    }
    impl->logic = read_logic_files(*impl->assets, impl->logic_issues);
    impl->saved = impl->snapshot();
    return std::unique_ptr<Document>(new Document(std::move(impl)));
}

std::vector<std::string> Document::scenes() const {
    std::vector<std::string> paths;
    for (const engine::assets::AssetRecord& record : impl_->assets->assets()) {
        if (record.path.ends_with(".dscene")) {
            paths.push_back(record.path);
        }
    }
    return paths; // sorted, as the assets are
}

std::vector<const reflect::TypeInfo*> Document::types_elsewhere(std::string_view name) const {
    const auto found = impl_->elsewhere.find(name);
    return found != impl_->elsewhere.end() ? found->second
                                           : std::vector<const reflect::TypeInfo*>{};
}

bool Document::open_scene(std::string_view path, std::string& error) {
    Impl& d = *impl_;
    if (path == d.scene_path) {
        return true;
    }
    if (dirty()) {
        error = "Save your changes before opening another scene.";
        return false;
    }
    const engine::assets::AssetRecord* record = d.assets->find(path);
    if (record == nullptr || !record->path.ends_with(".dscene")) {
        error = "There is no scene " + std::string(path) + " in assets/.";
        return false;
    }
    std::vector<Issue> issues;
    if (!d.load_scene(record->path, issues)) {
        error = issues.empty() ? std::string("The scene could not be opened.")
                               : issues.front().path + ": " + issues.front().message;
        return false;
    }
    // Undo belongs to the scene that was open: it starts again here.
    d.undo.clear();
    d.redo.clear();
    d.merging = false;
    d.saved = d.snapshot();
    return true;
}

bool Document::create_scene(std::string_view path, std::string& error) {
    Impl& d = *impl_;
    const fs::path file(path);
    if (file.extension() != ".dscene" || file.stem().empty() || file.is_absolute() ||
        std::any_of(file.begin(), file.end(), [](const fs::path& part) { return part == ".."; })) {
        error = "A scene is named like maps/cave.dscene, in assets/.";
        return false;
    }
    if (dirty()) {
        error = "Save your changes before making another scene.";
        return false;
    }
    std::error_code ignored;
    const fs::path target = d.folder / "assets" / file;
    if (fs::exists(target, ignored)) {
        error = "There is already a file " + std::string(path) + ".";
        return false;
    }
    // An empty scene, as the game reads one.
    const world::World empty(d.registry);
    if (!game::write_file_atomically(target, serialize::to_text(serialize::scene_to_json(empty)),
                                     error)) {
        return false;
    }
    static_cast<void>(d.assets->refresh()); // its .meta, and so its GUID
    return open_scene(file.generic_string(), error);
}

bool Document::set_start_scene(std::string_view path, std::string& error) {
    Impl& d = *impl_;
    const engine::assets::AssetRecord* record = d.assets->find(path);
    if (record == nullptr || !record->path.ends_with(".dscene")) {
        error = "There is no scene " + std::string(path) + " in assets/.";
        return false;
    }
    if (d.settings.start_scene == record->path) {
        return true;
    }
    serialize::Json before = d.snapshot();
    d.settings.start_scene = record->path;
    d.record("Start the game in " + record->path, std::move(before));
    return true;
}

bool Document::set_settings(const game::ProjectSettings& settings, std::string& error) {
    Impl& d = *impl_;
    game::ProjectSettings wanted = settings;
    wanted.start_scene = d.settings.start_scene;
    std::vector<Issue> issues;
    std::optional<game::ProjectSettings> checked =
        game::project_from_json(game::project_to_json(wanted), issues);
    if (!checked) {
        error =
            issues.empty() ? std::string("These settings cannot be used.") : issues.front().message;
        return false;
    }
    serialize::Json before = d.snapshot();
    d.settings = std::move(*checked);
    d.record("Change the project settings", std::move(before), Guid{}, "project/settings");
    return true;
}

const std::vector<LogicFile>& Document::logic() const noexcept {
    return impl_->logic;
}

const std::vector<Issue>& Document::logic_issues() const noexcept {
    return impl_->logic_issues;
}

const fs::path& Document::folder() const noexcept {
    return impl_->folder;
}
const game::ProjectSettings& Document::settings() const noexcept {
    return impl_->settings;
}
const reflect::Registry& Document::registry() const noexcept {
    return impl_->registry;
}
engine::assets::AssetDatabase& Document::assets() noexcept {
    return *impl_->assets;
}
world::World& Document::world() noexcept {
    return *impl_->world;
}
const world::World& Document::world() const noexcept {
    return *impl_->world;
}
const std::string& Document::scene_path() const noexcept {
    return impl_->scene_path;
}

std::vector<world::Entity> Document::entities() const {
    std::vector<world::Entity> out;
    for (const world::Entity root : impl_->world->roots()) {
        collect(*impl_->world, root, out);
    }
    return out;
}

std::vector<const reflect::TypeInfo*> Document::component_types() const {
    std::vector<const reflect::TypeInfo*> types = impl_->world->component_types();
    std::sort(types.begin(), types.end(),
              [](const reflect::TypeInfo* a, const reflect::TypeInfo* b) {
                  return a->meta.display_name < b->meta.display_name;
              });
    return types;
}

bool Document::set_property(Guid entity, const reflect::TypeInfo& component,
                            const reflect::PropertyInfo& property, const reflect::Value& value,
                            std::string& error) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    const reflect::ObjectRef object =
        e.is_valid() ? d.world->get(e, component) : reflect::ObjectRef{};
    if (object.is_null()) {
        error = "That entity has no " + component.meta.display_name + ".";
        return false;
    }
    serialize::Json before = d.snapshot();
    switch (reflect::set_property(object, property, value)) {
    case reflect::SetResult::Ok:
        break;
    case reflect::SetResult::ReadOnly:
        error = property.meta.display_name + " cannot be changed.";
        return false;
    case reflect::SetResult::OutOfRange:
        error = property.meta.display_name + " is out of its range.";
        return false;
    default:
        error = "That is not a valid " + property.meta.display_name + ".";
        return false;
    }
    d.record("Change " + property.meta.display_name + " of " + d.name_of(entity), std::move(before),
             entity, component.name + "/" + property.id);
    return true;
}

bool Document::set_property_many(const std::vector<Guid>& entities,
                                 const reflect::TypeInfo& component,
                                 const reflect::PropertyInfo& property, const reflect::Value& value,
                                 std::string& error) {
    Impl& d = *impl_;
    if (entities.size() == 1) {
        return set_property(entities.front(), component, property, value, error);
    }
    if (entities.empty()) {
        error = "Nothing is selected.";
        return false;
    }
    std::vector<reflect::ObjectRef> objects;
    std::string group; // the GUIDs changed, so the same group's typing merges
    for (const Guid guid : entities) {
        const world::Entity e = d.find(guid);
        const reflect::ObjectRef object =
            e.is_valid() ? d.world->get(e, component) : reflect::ObjectRef{};
        if (object.is_null()) {
            error = d.name_of(guid) + " has no " + component.meta.display_name + ".";
            return false;
        }
        objects.push_back(object);
        group += guid.to_string();
    }
    serialize::Json before = d.snapshot();
    for (const reflect::ObjectRef& object : objects) {
        const reflect::SetResult result = reflect::set_property(object, property, value);
        if (result == reflect::SetResult::Ok) {
            continue;
        }
        d.restore(before); // all or none
        error = result == reflect::SetResult::ReadOnly
                    ? property.meta.display_name + " cannot be changed."
                : result == reflect::SetResult::OutOfRange
                    ? property.meta.display_name + " is out of its range."
                    : "That is not a valid " + property.meta.display_name + ".";
        return false;
    }
    d.record("Change " + property.meta.display_name + " of " + std::to_string(objects.size()) +
                 " entities",
             std::move(before), Guid{}, component.name + "/" + property.id + ":" + group);
    return true;
}

bool Document::move_entity(Guid entity, double x, double y) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    auto* transform = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
    if (transform == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    serialize::Json before = d.snapshot();
    transform->x = x;
    transform->y = y;
    d.record("Move " + d.name_of(entity), std::move(before), entity, "transform_2d/position");
    return true;
}

bool Document::move_entities(const std::vector<std::pair<Guid, std::array<double, 2>>>& to) {
    Impl& d = *impl_;
    std::vector<std::pair<scene2d::Transform2D*, std::array<double, 2>>> moves;
    std::string group; // the GUIDs moved, so the same group's moves merge
    for (const auto& [guid, point] : to) {
        const world::Entity e = d.find(guid);
        auto* transform = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
        if (transform != nullptr && std::isfinite(point[0]) && std::isfinite(point[1])) {
            moves.emplace_back(transform, point);
            group += guid.to_string();
        }
    }
    if (moves.empty()) {
        return false;
    }
    if (moves.size() == 1) {
        return move_entity(to.front().first, moves.front().second[0], moves.front().second[1]);
    }
    serialize::Json before = d.snapshot();
    for (const auto& [transform, point] : moves) {
        transform->x = point[0];
        transform->y = point[1];
    }
    d.record("Move " + std::to_string(moves.size()) + " entities", std::move(before), Guid{},
             "transform_2d/position:" + group);
    return true;
}

bool Document::paint_tile(Guid tilemap, double x, double y, std::int32_t tile) {
    Impl& d = *impl_;
    const world::Entity e = d.find(tilemap);
    auto* map = e.is_valid() ? d.world->get<scene2d::Tilemap>(e) : nullptr;
    const auto* transform = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
    if (map == nullptr || transform == nullptr || tile < -1 || !std::isfinite(x) ||
        !std::isfinite(y)) {
        return false;
    }
    std::string error;
    const scene2d::TileGrid* parsed = map->grid(error);
    if (parsed == nullptr) {
        return false; // broken text: fix it first, rather than lose it
    }
    const double units = static_cast<double>(std::max<std::uint32_t>(d.settings.ppu, 1));
    const double tile_size =
        (map->tile_size == 0 ? units : static_cast<double>(map->tile_size)) / units;
    const double column = std::floor((x - transform->x) / tile_size);
    const double row = std::floor((y - transform->y) / tile_size);
    const auto side = static_cast<double>(scene2d::max_tilemap_side);
    if (column < 0.0 || row < 0.0 || column >= side || row >= side) {
        return false;
    }
    const auto cx = static_cast<std::uint32_t>(column);
    const auto cy = static_cast<std::uint32_t>(row);
    if (parsed->at(cx, cy) == tile && cx < parsed->width && cy < parsed->height) {
        return true; // already so
    }
    if (tile < 0 && (cx >= parsed->width || cy >= parsed->height)) {
        return true; // erasing outside the map: nothing to do
    }
    // A copy, grown as needed.
    scene2d::TileGrid grid;
    grid.width = std::max(parsed->width, cx + 1);
    grid.height = std::max(parsed->height, cy + 1);
    grid.cells.assign(std::size_t{grid.width} * grid.height, -1);
    for (std::uint32_t r = 0; r < parsed->height; ++r) {
        for (std::uint32_t c = 0; c < parsed->width; ++c) {
            grid.cells[(std::size_t{r} * grid.width) + c] = parsed->at(c, r);
        }
    }
    grid.cells[(std::size_t{cy} * grid.width) + cx] = tile;
    serialize::Json before = d.snapshot();
    map->tiles = scene2d::format_tiles(grid);
    d.record("Paint " + d.name_of(tilemap), std::move(before), tilemap, "tilemap/paint");
    return true;
}

namespace {

/// The cell of `map` (at `transform`) under map point (x, y), as signed whole numbers;
/// nothing if the point is not a finite number.
std::optional<std::array<double, 2>> cell_at(const scene2d::Tilemap& map,
                                             const scene2d::Transform2D& transform,
                                             std::uint32_t ppu, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return std::nullopt;
    }
    const double units = static_cast<double>(std::max<std::uint32_t>(ppu, 1));
    const double tile_size =
        (map.tile_size == 0 ? units : static_cast<double>(map.tile_size)) / units;
    return std::array<double, 2>{std::floor((x - transform.x) / tile_size),
                                 std::floor((y - transform.y) / tile_size)};
}

/// A copy of `from`, at least `width` by `height`, new cells empty.
scene2d::TileGrid grown(const scene2d::TileGrid& from, std::uint32_t width, std::uint32_t height) {
    scene2d::TileGrid grid;
    grid.width = std::max(from.width, width);
    grid.height = std::max(from.height, height);
    grid.cells.assign(std::size_t{grid.width} * grid.height, -1);
    for (std::uint32_t r = 0; r < from.height; ++r) {
        for (std::uint32_t c = 0; c < from.width; ++c) {
            grid.cells[(std::size_t{r} * grid.width) + c] = from.at(c, r);
        }
    }
    return grid;
}

} // namespace

bool Document::paint_tile_rect(Guid tilemap, double x0, double y0, double x1, double y1,
                               std::int32_t tile) {
    Impl& d = *impl_;
    const world::Entity e = d.find(tilemap);
    auto* map = e.is_valid() ? d.world->get<scene2d::Tilemap>(e) : nullptr;
    const auto* transform = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
    if (map == nullptr || transform == nullptr || tile < -1) {
        return false;
    }
    std::string error;
    const scene2d::TileGrid* parsed = map->grid(error);
    const auto a =
        parsed != nullptr ? cell_at(*map, *transform, d.settings.ppu, x0, y0) : std::nullopt;
    const auto b =
        parsed != nullptr ? cell_at(*map, *transform, d.settings.ppu, x1, y1) : std::nullopt;
    if (!a || !b) {
        return false;
    }
    // The part of the rectangle the map can hold: nothing above or left of its corner.
    const auto side = static_cast<double>(scene2d::max_tilemap_side);
    const double left = std::max(std::min((*a)[0], (*b)[0]), 0.0);
    const double top = std::max(std::min((*a)[1], (*b)[1]), 0.0);
    double right = std::min(std::max((*a)[0], (*b)[0]), side - 1.0);
    double bottom = std::min(std::max((*a)[1], (*b)[1]), side - 1.0);
    if (tile < 0) {
        // Erasing never grows the map.
        right = std::min(right, static_cast<double>(parsed->width) - 1.0);
        bottom = std::min(bottom, static_cast<double>(parsed->height) - 1.0);
    }
    if (right < left || bottom < top) {
        return tile < 0 && std::max((*a)[0], (*b)[0]) >= 0.0 && std::max((*a)[1], (*b)[1]) >= 0.0;
    }
    const auto c0 = static_cast<std::uint32_t>(left);
    const auto r0 = static_cast<std::uint32_t>(top);
    const auto c1 = static_cast<std::uint32_t>(right);
    const auto r1 = static_cast<std::uint32_t>(bottom);
    scene2d::TileGrid grid = grown(*parsed, c1 + 1, r1 + 1);
    bool changed = grid.width != parsed->width || grid.height != parsed->height;
    for (std::uint32_t r = r0; r <= r1; ++r) {
        for (std::uint32_t c = c0; c <= c1; ++c) {
            std::int32_t& cell = grid.cells[(std::size_t{r} * grid.width) + c];
            changed = changed || cell != tile;
            cell = tile;
        }
    }
    if (!changed) {
        return true; // already so
    }
    serialize::Json before = d.snapshot();
    map->tiles = scene2d::format_tiles(grid);
    d.record("Paint a rectangle of " + d.name_of(tilemap), std::move(before));
    return true;
}

bool Document::fill_tiles(Guid tilemap, double x, double y, std::int32_t tile) {
    Impl& d = *impl_;
    const world::Entity e = d.find(tilemap);
    auto* map = e.is_valid() ? d.world->get<scene2d::Tilemap>(e) : nullptr;
    const auto* transform = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
    if (map == nullptr || transform == nullptr || tile < -1) {
        return false;
    }
    std::string error;
    const scene2d::TileGrid* parsed = map->grid(error);
    const auto at =
        parsed != nullptr ? cell_at(*map, *transform, d.settings.ppu, x, y) : std::nullopt;
    // Only inside the map: a fill has edges to stop at there.
    if (!at || (*at)[0] < 0.0 || (*at)[1] < 0.0 || (*at)[0] >= static_cast<double>(parsed->width) ||
        (*at)[1] >= static_cast<double>(parsed->height)) {
        return false;
    }
    const auto cx = static_cast<std::uint32_t>((*at)[0]);
    const auto cy = static_cast<std::uint32_t>((*at)[1]);
    const std::int32_t old = parsed->at(cx, cy);
    if (old == tile) {
        return true; // already so
    }
    scene2d::TileGrid grid = grown(*parsed, parsed->width, parsed->height);
    // Every cell joined to the first by its sides through cells of the same tile.
    std::vector<std::array<std::uint32_t, 2>> open{{cx, cy}};
    grid.cells[(std::size_t{cy} * grid.width) + cx] = tile;
    while (!open.empty()) {
        const std::array<std::uint32_t, 2> cell = open.back();
        open.pop_back();
        const auto visit = [&](std::uint32_t c, std::uint32_t r) {
            std::int32_t& next = grid.cells[(std::size_t{r} * grid.width) + c];
            if (next == old) {
                next = tile;
                open.push_back({c, r});
            }
        };
        if (cell[0] > 0) {
            visit(cell[0] - 1, cell[1]);
        }
        if (cell[0] + 1 < grid.width) {
            visit(cell[0] + 1, cell[1]);
        }
        if (cell[1] > 0) {
            visit(cell[0], cell[1] - 1);
        }
        if (cell[1] + 1 < grid.height) {
            visit(cell[0], cell[1] + 1);
        }
    }
    serialize::Json before = d.snapshot();
    map->tiles = scene2d::format_tiles(grid);
    d.record("Fill " + d.name_of(tilemap), std::move(before));
    return true;
}

Guid Document::create_entity(std::string_view name, double x, double y) {
    Impl& d = *impl_;
    serialize::Json before = d.snapshot();
    const world::Entity e = d.world->create(name);
    if (auto* transform = d.world->add<scene2d::Transform2D>(e)) {
        transform->x = x;
        transform->y = y;
    }
    const Guid guid = d.world->guid(e);
    d.record("Create " + d.name_of(guid), std::move(before));
    return guid;
}

Guid Document::place_asset(std::string_view asset_path, double x, double y, std::string& error) {
    Impl& d = *impl_;
    const engine::assets::AssetRecord* record = d.assets->find(asset_path);
    const fs::path path(asset_path);
    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (record == nullptr) {
        error = "There is no asset " + std::string(asset_path) + " in assets/.";
        return Guid{};
    }
    const bool picture = extension == ".png";
    const bool sound =
        extension == ".wav" || extension == ".ogg" || extension == ".flac" || extension == ".mp3";
    if (!picture && !sound) {
        error = "Only pictures (PNG) and sounds (WAV, Ogg, FLAC, MP3) can be placed in the "
                "scene; " +
                std::string(asset_path) + " is neither.";
        return Guid{};
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        error = "That point is not on the map.";
        return Guid{};
    }
    const double ppu = std::max(1U, d.settings.ppu);
    serialize::Json before = d.snapshot();
    const world::Entity e = d.world->create(path.stem().string());
    if (auto* transform = d.world->add<scene2d::Transform2D>(e)) {
        transform->x = std::round(x * ppu) / ppu;
        transform->y = std::round(y * ppu) / ppu;
    }
    if (picture) {
        if (auto* sprite = d.world->add<scene2d::Sprite>(e)) {
            sprite->image = record->guid.to_string();
        }
    } else if (auto* source = d.world->add<game::SoundSource>(e)) {
        source->sound = record->guid.to_string();
    }
    const Guid guid = d.world->guid(e);
    d.record("Place " + d.name_of(guid), std::move(before));
    return guid;
}

bool Document::delete_entity(Guid entity) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    if (!e.is_valid()) {
        return false;
    }
    const std::string label = "Delete " + d.name_of(entity);
    serialize::Json before = d.snapshot();
    d.world->destroy(e);
    d.record(label, std::move(before));
    return true;
}

bool Document::delete_entities(const std::vector<Guid>& entities) {
    Impl& d = *impl_;
    if (entities.size() == 1) {
        return delete_entity(entities.front());
    }
    serialize::Json before = d.snapshot();
    std::size_t deleted = 0;
    for (const Guid guid : entities) {
        if (const world::Entity e = d.find(guid); e.is_valid()) {
            d.world->destroy(e);
            ++deleted;
        }
    }
    if (deleted == 0) {
        return false;
    }
    d.record("Delete " + std::to_string(deleted) + " entities", std::move(before));
    return true;
}

bool Document::rename(Guid entity, std::string_view name) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    if (!e.is_valid()) {
        return false;
    }
    const std::string label = "Rename " + d.name_of(entity);
    serialize::Json before = d.snapshot();
    if (!d.world->set_name(e, name)) {
        return false;
    }
    d.record(label, std::move(before));
    return true;
}

bool Document::add_component(Guid entity, const reflect::TypeInfo& component) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    if (!e.is_valid() || !d.world->get(e, component).is_null()) {
        return false;
    }
    serialize::Json before = d.snapshot();
    if (d.world->add(e, component).is_null()) {
        return false;
    }
    d.record("Add " + component.meta.display_name + " to " + d.name_of(entity), std::move(before));
    return true;
}

bool Document::remove_component(Guid entity, const reflect::TypeInfo& component) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    if (!e.is_valid()) {
        return false;
    }
    serialize::Json before = d.snapshot();
    if (!d.world->remove(e, component)) {
        return false;
    }
    d.record("Remove " + component.meta.display_name + " from " + d.name_of(entity),
             std::move(before));
    return true;
}

bool Document::add_component_many(const std::vector<Guid>& entities,
                                  const reflect::TypeInfo& component) {
    Impl& d = *impl_;
    serialize::Json before = d.snapshot();
    std::vector<Guid> changed;
    for (const Guid guid : entities) {
        const world::Entity e = d.find(guid);
        if (e.is_valid() && d.world->get(e, component).is_null() &&
            !d.world->add(e, component).is_null()) {
            changed.push_back(guid);
        }
    }
    if (changed.empty()) {
        return false;
    }
    d.record("Add " + component.meta.display_name + " to " +
                 (changed.size() == 1 ? d.name_of(changed.front())
                                      : std::to_string(changed.size()) + " entities"),
             std::move(before));
    return true;
}

bool Document::remove_component_many(const std::vector<Guid>& entities,
                                     const reflect::TypeInfo& component) {
    Impl& d = *impl_;
    serialize::Json before = d.snapshot();
    std::vector<Guid> changed;
    for (const Guid guid : entities) {
        const world::Entity e = d.find(guid);
        if (e.is_valid() && d.world->remove(e, component)) {
            changed.push_back(guid);
        }
    }
    if (changed.empty()) {
        return false;
    }
    d.record("Remove " + component.meta.display_name + " from " +
                 (changed.size() == 1 ? d.name_of(changed.front())
                                      : std::to_string(changed.size()) + " entities"),
             std::move(before));
    return true;
}

bool Document::can_undo() const noexcept {
    return !impl_->undo.empty();
}
bool Document::can_redo() const noexcept {
    return !impl_->redo.empty();
}
std::string Document::undo_label() const {
    return impl_->undo.empty() ? std::string() : impl_->undo.back().label;
}
std::string Document::redo_label() const {
    return impl_->redo.empty() ? std::string() : impl_->redo.back().label;
}

bool Document::undo() {
    Impl& d = *impl_;
    if (d.undo.empty()) {
        return false;
    }
    Step step = std::move(d.undo.back());
    d.undo.pop_back();
    d.restore(step.before);
    d.redo.push_back(std::move(step));
    d.merging = false;
    return true;
}

bool Document::redo() {
    Impl& d = *impl_;
    if (d.redo.empty()) {
        return false;
    }
    Step step = std::move(d.redo.back());
    d.redo.pop_back();
    d.restore(step.after);
    d.undo.push_back(std::move(step));
    d.merging = false;
    return true;
}

void Document::end_merge() noexcept {
    impl_->merging = false;
}

bool Document::dirty() const noexcept {
    return impl_->snapshot() != impl_->saved;
}

bool Document::save(std::string& error) {
    Impl& d = *impl_;
    serialize::Json state = d.snapshot();
    if (!game::write_file_atomically(d.folder / "assets" / d.scene_path,
                                     serialize::to_text(state.at("scene")), error)) {
        return false;
    }
    d.saved["scene"] = state.at("scene");
    if (state.at("project") != d.saved["project"]) {
        if (!game::write_file_atomically(d.folder / game::project_file,
                                         serialize::to_text(state.at("project")), error)) {
            error.insert(0, std::string(game::project_file) + ": ");
            return false;
        }
        d.saved["project"] = state.at("project");
    }
    // The logic files that changed since they were last saved.
    bool written_logic = false;
    for (const auto& [path, json] : state.at("logic").items()) {
        serialize::Json& saved_file = d.saved["logic"][path];
        if (json == saved_file) {
            continue;
        }
        if (!game::write_file_atomically(d.folder / "assets" / path, serialize::to_text(json),
                                         error)) {
            error.insert(0, "assets/" + path + ": ");
            return false;
        }
        saved_file = json;
        written_logic = true;
    }
    if (written_logic) {
        // A new file gets its .meta, and so its GUID, as any asset does.
        static_cast<void>(d.assets->refresh());
    }
    d.merging = false;
    return true;
}

// ---------------------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------------------

std::optional<scene2d::Box> sprite_bounds(const world::World& w,
                                          const engine::assets::AssetDatabase& assets,
                                          world::Entity entity, std::uint32_t ppu) {
    const scene2d::Sprite* sprite = w.get<scene2d::Sprite>(entity);
    const scene2d::Transform2D* t = w.get<scene2d::Transform2D>(entity);
    const std::optional<Guid> image = sprite != nullptr ? Guid::parse(sprite->image) : std::nullopt;
    if (t == nullptr || !image) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::uint8_t>> bytes = assets.read(*image);
    std::string error;
    const std::optional<engine::assets::Image> picture =
        bytes ? engine::assets::decode_png(*bytes, error) : std::nullopt;
    if (!picture) {
        return std::nullopt;
    }
    const double units = static_cast<double>(std::max<std::uint32_t>(ppu, 1));
    const double width =
        static_cast<double>(sprite->frame_width > 0 ? sprite->frame_width : picture->width) / units;
    const double height =
        static_cast<double>(sprite->frame_height > 0 ? sprite->frame_height : picture->height) /
        units;
    const double left = t->x - (sprite->pivot_x * width);
    const double top = t->y - (sprite->pivot_y * height);
    return scene2d::Box{left, top, left + width, top + height};
}

namespace {

/// pick(), optionally leaving sprites out (the Diorama style picks those through the
/// camera instead).
world::Entity pick_at(const world::World& w, const engine::assets::AssetDatabase& assets, double x,
                      double y, std::uint32_t ppu, bool with_sprites) {
    const double units = static_cast<double>(std::max<std::uint32_t>(ppu, 1));
    struct Candidate {
        std::int32_t layer;
        double y;
        Guid guid;
        world::Entity entity;
    };
    std::vector<Candidate> sprites;
    std::vector<Candidate> maps;
    const reflect::Registry& registry = w.registry();
    if (const reflect::TypeInfo* type = registry.find_type<scene2d::Sprite>();
        type != nullptr && with_sprites) {
        w.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const scene2d::Sprite* sprite = ref.as<scene2d::Sprite>();
            const scene2d::Transform2D* t = w.get<scene2d::Transform2D>(entity);
            if (sprite == nullptr || t == nullptr || !sprite->visible) {
                return;
            }
            const std::optional<scene2d::Box> box = sprite_bounds(w, assets, entity, ppu);
            if (box && x >= box->left && x < box->right && y >= box->top && y < box->bottom) {
                sprites.push_back(Candidate{t->layer, t->y, w.guid(entity), entity});
            }
        });
    }
    if (const reflect::TypeInfo* type = registry.find_type<scene2d::Tilemap>()) {
        w.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const scene2d::Tilemap* map = ref.as<scene2d::Tilemap>();
            const scene2d::Transform2D* t = w.get<scene2d::Transform2D>(entity);
            std::string error;
            const scene2d::TileGrid* grid = map != nullptr ? map->grid(error) : nullptr;
            if (t == nullptr || grid == nullptr || !map->visible) {
                return;
            }
            const double tile =
                (map->tile_size == 0 ? units : static_cast<double>(map->tile_size)) / units;
            const double column = std::floor((x - t->x) / tile);
            const double row = std::floor((y - t->y) / tile);
            if (column >= 0 && row >= 0 && column < static_cast<double>(grid->width) &&
                row < static_cast<double>(grid->height) &&
                grid->at(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row)) >=
                    0) {
                maps.push_back(Candidate{t->layer, t->y, w.guid(entity), entity});
            }
        });
    }
    // The draw order: layer, then y, then GUID; the last drawn is on top.
    const auto drawn_later = [](const Candidate& a, const Candidate& b) {
        if (a.layer != b.layer) {
            return a.layer > b.layer;
        }
        if (a.y != b.y) {
            return a.y > b.y;
        }
        return b.guid < a.guid;
    };
    for (std::vector<Candidate>* list : {&sprites, &maps}) {
        if (!list->empty()) {
            return std::min_element(list->begin(), list->end(), drawn_later)->entity;
        }
    }
    return world::Entity{};
}

} // namespace

world::Entity pick(const world::World& w, const engine::assets::AssetDatabase& assets, double x,
                   double y, std::uint32_t ppu) {
    return pick_at(w, assets, x, y, ppu, true);
}

// ---------------------------------------------------------------------------------------
// The Diorama style
// ---------------------------------------------------------------------------------------

scene2d::DioramaCamera diorama_camera(const game::ProjectSettings& settings, double x, double y) {
    scene2d::DioramaCamera camera;
    camera.x = x;
    camera.y = y;
    camera.pitch = settings.camera_pitch;
    camera.distance = settings.camera_distance;
    camera.fov = settings.camera_fov;
    camera.ppu = settings.ppu;
    return camera;
}

std::optional<std::array<double, 4>>
diorama_sprite_rect(const world::World& w, const engine::assets::AssetDatabase& assets,
                    world::Entity entity, const scene2d::DioramaCamera& camera, float aspect) {
    const scene2d::Sprite* sprite = w.get<scene2d::Sprite>(entity);
    const scene2d::Transform2D* t = w.get<scene2d::Transform2D>(entity);
    // The picture's size in tiles, as Classic 2D sizes it (the same in 3D).
    const std::optional<scene2d::Box> flat = sprite_bounds(w, assets, entity, camera.ppu);
    if (sprite == nullptr || t == nullptr || !flat) {
        return std::nullopt;
    }
    const double width = flat->right - flat->left;
    const double height = flat->bottom - flat->top;
    // As draw_diorama() stands it: upright, facing the camera, pivot on the map point.
    const double yaw =
        (std::isfinite(camera.yaw) ? camera.yaw : 0.0) * 3.14159265358979323846 / 180.0;
    const double rx = std::cos(yaw);
    const double rz = -std::sin(yaw);
    const double lift = static_cast<double>(t->layer) * 0.002;
    const double left = -sprite->pivot_x * width;
    const double right = (1.0 - sprite->pivot_x) * width;
    const double top = sprite->pivot_y * height;
    const double bottom = -(1.0 - sprite->pivot_y) * height;
    const engine::render::Mat4 vp = scene2d::diorama_view_projection(camera, aspect);
    std::array<double, 4> rect{1e9, 1e9, -1e9, -1e9};
    for (const auto& [along, up] : {std::pair{left, top}, std::pair{right, top},
                                    std::pair{right, bottom}, std::pair{left, bottom}}) {
        const auto clip = vp.transform(engine::render::Vec3{
            static_cast<float>(t->x + (rx * along)), static_cast<float>(lift + up),
            static_cast<float>(t->y + (rz * along))});
        if (!(clip[3] > 0.0F)) {
            return std::nullopt;
        }
        const double x = clip[0] / clip[3];
        const double y = clip[1] / clip[3];
        rect[0] = std::min(rect[0], x);
        rect[1] = std::min(rect[1], y);
        rect[2] = std::max(rect[2], x);
        rect[3] = std::max(rect[3], y);
    }
    return rect;
}

world::Entity pick_diorama(const world::World& w, const engine::assets::AssetDatabase& assets,
                           const scene2d::DioramaCamera& camera, float aspect, double ndc_x,
                           double ndc_y) {
    // Sprites: the nearest billboard covering the point (the depth of its feet).
    const engine::render::Mat4 vp = scene2d::diorama_view_projection(camera, aspect);
    world::Entity best;
    float best_depth = std::numeric_limits<float>::infinity();
    if (const reflect::TypeInfo* type = w.registry().find_type<scene2d::Sprite>()) {
        w.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const scene2d::Sprite* sprite = ref.as<scene2d::Sprite>();
            const scene2d::Transform2D* t = w.get<scene2d::Transform2D>(entity);
            if (sprite == nullptr || t == nullptr || !sprite->visible) {
                return;
            }
            const std::optional<std::array<double, 4>> rect =
                diorama_sprite_rect(w, assets, entity, camera, aspect);
            if (!rect || ndc_x < (*rect)[0] || ndc_x > (*rect)[2] || ndc_y < (*rect)[1] ||
                ndc_y > (*rect)[3]) {
                return;
            }
            const auto clip = vp.transform(
                engine::render::Vec3{static_cast<float>(t->x), 0.0F, static_cast<float>(t->y)});
            const float depth = clip[2] / clip[3];
            if (depth < best_depth) {
                best_depth = depth;
                best = entity;
            }
        });
    }
    if (best.is_valid()) {
        return best;
    }
    // Maps: the tile where the ray meets the ground.
    const std::optional<std::array<double, 2>> ground =
        scene2d::ground_hit(scene2d::diorama_ray(camera, aspect, ndc_x, ndc_y));
    if (!ground) {
        return world::Entity{};
    }
    return pick_at(w, assets, (*ground)[0], (*ground)[1], camera.ppu, false);
}

} // namespace editor
