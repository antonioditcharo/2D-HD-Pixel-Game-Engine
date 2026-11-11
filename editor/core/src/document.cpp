#include "editor/core/document.h"

#include "engine/game/behaviours.h"
#include "engine/game/save.h"
#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

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

/// One step of undo: the scene before and after it.
struct Step {
    std::string label;
    serialize::Json before;
    serialize::Json after;
    // What a merged step changes: the next change to the same property extends it.
    Guid entity;
    std::string property; ///< "component/property"; empty if the step does not merge.
};

/// Every entity under `entity`, depth first, `entity` included.
void collect(const world::World& w, world::Entity entity, std::vector<world::Entity>& out) {
    out.push_back(entity);
    for (const world::Entity child : w.children(entity)) {
        collect(w, child, out);
    }
}

} // namespace

struct Document::Impl {
    fs::path folder;
    game::ProjectSettings settings;
    reflect::Registry registry;
    std::unique_ptr<engine::assets::AssetDatabase> assets;
    std::unique_ptr<world::World> world;
    std::string scene_path;
    std::vector<Step> undo;
    std::vector<Step> redo;
    bool merging = false;
    /// The scene as last saved, to know whether it changed.
    serialize::Json saved;

    [[nodiscard]] serialize::Json snapshot() const { return serialize::scene_to_json(*world); }

    /// Replaces the world with `scene`.
    void restore(const serialize::Json& scene) {
        for (const world::Entity root : world->roots()) {
            world->destroy(root);
        }
        static_cast<void>(serialize::scene_from_json(scene, *world));
    }

    /// Records an edit made since `before`. Merges into the last step if it changes the
    /// same property of the same entity and merging has not been ended.
    void record(std::string label, serialize::Json before, Guid entity = Guid{},
                const std::string& property = {}) {
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
        undo.push_back(
            Step{std::move(label), std::move(before), std::move(after), entity, property});
        merging = !property.empty();
    }

    [[nodiscard]] world::Entity find(Guid guid) const { return world->find(guid); }

    /// "hero", or "an entity" when it has no name.
    [[nodiscard]] std::string name_of(Guid guid) const {
        const world::Entity entity = find(guid);
        const std::string_view name = entity.is_valid() ? world->name(entity) : "";
        return name.empty() ? std::string("an entity") : std::string(name);
    }
};

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

    impl->scene_path = impl->settings.start_scene;
    const fs::path scene_file = folder / "assets" / impl->scene_path;
    std::ifstream in(scene_file, std::ios::binary);
    if (!in) {
        issues.push_back(
            Issue{"assets/" + impl->scene_path, "The start scene could not be opened."});
        return nullptr;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<Issue> parse_issues;
    const std::optional<serialize::Json> json = serialize::parse(text, parse_issues);
    if (!json) {
        for (Issue& issue : parse_issues) {
            issue.path = "assets/" + impl->scene_path + issue.path;
            issues.push_back(std::move(issue));
        }
        return nullptr;
    }
    serialize::SceneLoad loaded = serialize::scene_from_json(*json, *impl->world);
    for (Issue& issue : loaded.issues) {
        issue.path = "assets/" + impl->scene_path + issue.path;
        issues.push_back(std::move(issue));
    }
    if (loaded.entities.empty() && !loaded.issues.empty()) {
        return nullptr;
    }
    impl->saved = impl->snapshot();
    return std::unique_ptr<Document>(new Document(std::move(impl)));
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
    serialize::Json scene = d.snapshot();
    if (!game::write_file_atomically(d.folder / "assets" / d.scene_path, serialize::to_text(scene),
                                     error)) {
        return false;
    }
    d.saved = std::move(scene);
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

world::Entity pick(const world::World& w, const engine::assets::AssetDatabase& assets, double x,
                   double y, std::uint32_t ppu) {
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
    if (const reflect::TypeInfo* type = registry.find_type<scene2d::Sprite>()) {
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

} // namespace editor
