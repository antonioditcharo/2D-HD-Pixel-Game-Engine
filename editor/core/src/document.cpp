#include "editor/core/document.h"

#include "engine/assets/aseprite.h"
#include "engine/assets/autotile.h"
#include "engine/game/behaviours.h"
#include "engine/game/conversation.h"
#include "engine/game/localization.h"
#include "engine/game/logic_world.h"
#include "engine/game/menus.h"
#include "engine/game/save.h"
#include "engine/game/tables.h"
#include "engine/scene2d/scene2d.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <variant>

#include "document_impl.h"

namespace editor {

namespace fs = std::filesystem;
namespace game = engine::game;
namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
namespace assets = engine::assets;
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
                           {"project", game::project_to_json(settings)},
                           {"others", others}};
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
    // Other scenes as they were then, and so what logic can name in them.
    if (state.at("others") != others) {
        others = state.at("others");
        read_elsewhere();
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

std::optional<serialize::Json> Document::Impl::other_scene(const std::string& path) const {
    if (others.contains(path)) {
        return std::optional<serialize::Json>(std::in_place, others.at(path));
    }
    std::ifstream in(folder / "assets" / path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<Issue> ignored; // told when that scene is opened
    std::optional<serialize::Json> json = serialize::parse(text, ignored);
    if (json && !json->is_object()) {
        return std::nullopt;
    }
    return json;
}

void Document::Impl::read_elsewhere() {
    elsewhere.clear();
    for (const engine::assets::AssetRecord& record : assets->assets()) {
        if (!record.path.ends_with(".dscene") || record.path == scene_path) {
            continue;
        }
        const std::optional<serialize::Json> json = other_scene(record.path);
        if (!json) {
            continue;
        }
        world::World scratch(registry);
        scene2d::register_components(scratch);
        game::register_behaviour_components(scratch);
        for (const world::Entity entity : serialize::scene_from_json(*json, scratch).entities) {
            for (const std::string& name : game::logic_names(scratch, entity)) {
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
    serialize::Json others = std::move(d.others); // saved already (not dirty)
    d.others = serialize::Json::object();
    if (!d.load_scene(record->path, issues)) {
        d.others = std::move(others);
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

void Document::set_tile_variants(std::vector<std::int32_t> tiles) {
    impl_->variants = std::move(tiles);
}

const std::vector<std::int32_t>& Document::tile_variants() const noexcept {
    return impl_->variants;
}

std::string Document::import_aseprite(std::string_view path, std::string& error) {
    const Impl& d = *impl_;
    const fs::path file(path);
    std::string extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const engine::assets::AssetRecord* record = d.assets->find(path);
    if (record == nullptr || (extension != ".aseprite" && extension != ".ase")) {
        error = "There is no Aseprite file " + std::string(path) + " in assets/.";
        return {};
    }
    const std::optional<std::vector<std::uint8_t>> bytes = d.assets->read(record->guid);
    const std::optional<engine::assets::AsepriteSheet> read =
        bytes ? engine::assets::read_aseprite(*bytes, error) : std::nullopt;
    if (!read) {
        if (error.empty()) {
            error = std::string(path) + " cannot be read.";
        }
        return {};
    }
    const fs::path picture = file.parent_path() / (file.stem().string() + ".png");
    const std::vector<std::uint8_t> png = engine::assets::encode_png(read->sheet);
    scene2d::SheetInfo sheet;
    sheet.frame_width = read->frame_width;
    sheet.frame_height = read->frame_height;
    for (const std::uint32_t ms : read->durations_ms) {
        sheet.durations_ms.push_back(std::clamp<std::uint32_t>(ms, 1, 65535));
    }
    for (const engine::assets::AsepriteTag& tag : read->tags) {
        // Aseprite's "ping-pong, backwards first" plays as ping-pong.
        sheet.tags.push_back(scene2d::SheetTag{tag.name, tag.from, tag.to,
                                               tag.direction == 1 ? scene2d::TagDirection::Reverse
                                               : tag.direction >= 2
                                                   ? scene2d::TagDirection::PingPong
                                                   : scene2d::TagDirection::Forward});
    }
    if (png.empty() ||
        !game::write_file_atomically(
            d.folder / "assets" / picture,
            std::string_view(reinterpret_cast<const char*>(png.data()), png.size()), // NOLINT
            error) ||
        !game::write_file_atomically(d.folder / "assets" /
                                         fs::path(scene2d::sheet_path(picture.generic_string())),
                                     scene2d::sheet_to_text(sheet), error)) {
        if (error.empty()) {
            error = "The sprite sheet could not be written.";
        }
        return {};
    }
    static_cast<void>(d.assets->refresh());
    return picture.generic_string();
}

std::string Document::make_autotiles(std::string_view path, std::string& error) {
    const Impl& d = *impl_;
    const fs::path file(path);
    const engine::assets::AssetRecord* record = d.assets->find(path);
    if (record == nullptr || file.extension() != ".png") {
        error = "There is no picture " + std::string(path) + " in assets/.";
        return {};
    }
    const std::optional<std::vector<std::uint8_t>> bytes = d.assets->read(record->guid);
    std::optional<engine::assets::Image> image =
        bytes ? engine::assets::decode_png(*bytes, error) : std::nullopt;
    if (!image) {
        if (error.empty()) {
            error = std::string(path) + " cannot be read.";
        }
        return {};
    }
    const std::optional<engine::assets::Image> tiles =
        assets::expand_autotiles(*image, std::max<std::uint32_t>(d.settings.ppu, 1), error);
    if (!tiles) {
        return {};
    }
    const std::vector<std::uint8_t> png = engine::assets::encode_png(*tiles);
    const fs::path made = file.parent_path() / (file.stem().string() + "_auto.png");
    if (png.empty() ||
        !game::write_file_atomically(
            d.folder / "assets" / made,
            std::string_view(reinterpret_cast<const char*>(png.data()), png.size()), // NOLINT
            error)) {
        if (error.empty()) {
            error = "The auto-tiles could not be written.";
        }
        return {};
    }
    static_cast<void>(d.assets->refresh()); // its .meta, and so its GUID
    return made.generic_string();
}

std::vector<std::string> Document::texts_to_translate() const {
    const Impl& d = *impl_;
    std::vector<std::string> out;
    std::set<std::string, std::less<>> seen;
    const auto add = [&](std::string text) {
        const std::size_t a = text.find_first_not_of(' ');
        if (a == std::string::npos) {
            return;
        }
        text = text.substr(a, text.find_last_not_of(' ') - a + 1);
        if (seen.insert(text).second) {
            out.push_back(std::move(text));
        }
    };
    const auto answers = [&](const std::string& list) {
        std::size_t start = 0;
        while (start <= list.size()) {
            const std::size_t bar = std::min(list.find('|', start), list.size());
            add(list.substr(start, bar - start));
            start = bar + 1;
        }
    };
    // The conversations, in the order of their files.
    for (const engine::assets::AssetRecord& record : d.assets->assets()) {
        if (!record.path.ends_with(".dtalk")) {
            continue;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = d.assets->read(record.guid);
        std::string ignored;
        const std::optional<game::Conversation> talk =
            bytes ? game::parse_conversation(
                        std::string_view(reinterpret_cast<const char*>(bytes->data()), // NOLINT
                                         bytes->size()),
                        ignored)
                  : std::nullopt;
        if (!talk) {
            continue;
        }
        for (const game::Speaker& speaker : talk->speakers) {
            add(speaker.name);
        }
        for (const game::TalkNode& node : talk->nodes) {
            add(node.text);
            for (const game::TalkChoice& choice : node.choices) {
                add(choice.text);
            }
        }
    }
    // What logic says and asks, written as text in its steps.
    const auto literal = [](const std::vector<logic::Expr>& arguments, std::size_t i) {
        if (i >= arguments.size() || arguments[i].kind != logic::ExprKind::Literal) {
            return std::string();
        }
        const std::string* words = std::get_if<std::string>(&arguments[i].literal);
        return words != nullptr ? *words : std::string();
    };
    std::function<void(const std::vector<logic::Stmt>&)> steps =
        [&](const std::vector<logic::Stmt>& list) {
            for (const logic::Stmt& step : list) {
                if (step.kind == logic::StmtKind::Do && step.object == "dialogue") {
                    const bool speaker = step.member.ends_with("_as");
                    const std::size_t first = speaker ? 1 : 0;
                    if (speaker) {
                        add(literal(step.arguments, 0));
                    }
                    if (step.member.starts_with("say") || step.member.starts_with("ask")) {
                        add(literal(step.arguments, first));
                    }
                    if (step.member.starts_with("ask")) {
                        answers(literal(step.arguments, first + 1));
                    }
                }
                steps(step.body);
                steps(step.otherwise);
            }
        };
    for (const LogicFile& file : d.logic) {
        for (const logic::Rule& rule : file.asset.rules) {
            steps(rule.actions);
            steps(rule.otherwise);
        }
        for (const logic::Sequence& sequence : file.asset.sequences) {
            steps(sequence.steps);
        }
        for (const logic::Brain& brain : file.asset.brains) {
            for (const logic::BrainState& state : brain.states) {
                steps(state.on_enter);
                steps(state.while_in);
                steps(state.on_exit);
            }
        }
    }
    for (std::string& word : game::menu_words()) {
        add(std::move(word));
    }
    return out;
}

std::int64_t Document::collect_translations(std::string& error) {
    const Impl& d = *impl_;
    const std::string path = std::string(game::translations_table) + ".dtable";
    game::Table table;
    if (const engine::assets::AssetRecord* record = d.assets->find(path)) {
        const std::optional<std::vector<std::uint8_t>> bytes = d.assets->read(record->guid);
        std::optional<game::Table> read =
            bytes ? game::parse_table(
                        std::string_view(reinterpret_cast<const char*>(bytes->data()), // NOLINT
                                         bytes->size()),
                        error)
                  : std::nullopt;
        if (!read) {
            error = path + " cannot be read" + (error.empty() ? "." : ": " + error);
            return -1;
        }
        table = std::move(*read);
    }
    std::vector<std::string> languages;
    std::string language;
    for (const char c : d.settings.languages + ",") {
        if (c == ',' || c == ' ') {
            if (!language.empty()) {
                languages.push_back(std::move(language));
                language.clear();
            }
        } else {
            language += c;
        }
    }
    const std::size_t added = game::add_translation_rows(table, texts_to_translate(), languages);
    if (!game::write_file_atomically(d.folder / "assets" / path, game::table_to_text(table),
                                     error)) {
        return -1;
    }
    static_cast<void>(d.assets->refresh());
    return static_cast<std::int64_t>(added);
}

bool Document::create_scene(std::string_view path, std::string& error) {
    const Impl& d = *impl_;
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
const engine::assets::AssetDatabase& Document::assets() const noexcept {
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

bool Document::place_on_screen(Guid entity, const reflect::TypeInfo& component, double x, double y,
                               std::string& error) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    const reflect::ObjectRef object =
        e.is_valid() ? d.world->get(e, component) : reflect::ObjectRef{};
    const reflect::PropertyInfo* across = component.find_property("x");
    const reflect::PropertyInfo* down = component.find_property("y");
    if (object.is_null() || across == nullptr || down == nullptr) {
        error = "That entity has no " + component.meta.display_name + " to move.";
        return false;
    }
    const std::optional<reflect::Value> was_x = reflect::get_property(object, *across);
    serialize::Json before = d.snapshot();
    if (reflect::set_property(object, *across, reflect::Value{x}) != reflect::SetResult::Ok) {
        error = "That is too far for " + component.meta.display_name + ".";
        return false;
    }
    if (reflect::set_property(object, *down, reflect::Value{y}) != reflect::SetResult::Ok) {
        if (was_x) {
            static_cast<void>(reflect::set_property(object, *across, *was_x));
        }
        error = "That is too far for " + component.meta.display_name + ".";
        return false;
    }
    d.record("Move " + d.name_of(entity) + " on the screen", std::move(before), entity,
             "screen_place/" + component.name);
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

namespace {

/// The name place_door() gives a door to the scene named `stem`: door_to_ and the name,
/// in lower-case letters, digits and _.
std::string door_name(std::string_view stem) {
    std::string name = "door_to_";
    for (const char c : stem) {
        const auto u = static_cast<unsigned char>(c);
        name += std::isalnum(u) != 0 ? static_cast<char>(std::tolower(u)) : '_';
    }
    return name;
}

/// Whether the scene in `json` has an entity named `name`.
bool scene_has_entity(const serialize::Json& json, std::string_view name) {
    if (!json.is_object() || !json.contains("entities") || !json["entities"].is_array()) {
        return false;
    }
    return std::any_of(
        json["entities"].begin(), json["entities"].end(), [name](const serialize::Json& entity) {
            return entity.is_object() && entity.contains("name") && entity["name"].is_string() &&
                   entity["name"].get_ref<const std::string&>() == name;
        });
}

/// The scene in `json` with a door added: an entity `name` with a Trigger Area where the
/// player starts there (the first Top-down Mover), or else at (x, y). Nothing if the scene
/// cannot be loaded.
std::optional<serialize::Json> with_door(const serialize::Json& json, reflect::Registry& registry,
                                         const std::string& name, double x, double y) {
    world::World scratch(registry);
    scene2d::register_components(scratch);
    game::register_behaviour_components(scratch);
    const serialize::SceneLoad loaded = serialize::scene_from_json(json, scratch);
    if (loaded.entities.empty() && !loaded.issues.empty()) {
        return std::nullopt;
    }
    if (const reflect::TypeInfo* mover = registry.find_type<game::TopDownMover>()) {
        bool found = false;
        scratch.each(*mover, [&](world::Entity entity, reflect::ObjectRef) {
            const auto* feet = scratch.get<scene2d::Transform2D>(entity);
            if (!found && feet != nullptr) {
                x = feet->x;
                y = feet->y;
                found = true;
            }
        });
    }
    const world::Entity door = scratch.create(name);
    if (scratch.add<scene2d::Transform2D>(door) == nullptr ||
        scratch.add<game::Trigger>(door) == nullptr) {
        return std::nullopt;
    }
    // Asked for after both are added: adding one moves the others in memory.
    auto* at = scratch.get<scene2d::Transform2D>(door);
    at->x = x;
    at->y = y;
    return std::optional<serialize::Json>(std::in_place, serialize::scene_to_json(scratch));
}

} // namespace

Guid Document::place_door(std::string_view scene, double x, double y, std::string& error) {
    Impl& d = *impl_;
    const std::vector<std::string> all = scenes();
    if (std::find(all.begin(), all.end(), scene) == all.end()) {
        error = "There is no scene " + std::string(scene) + " in this project.";
        return Guid{};
    }
    if (scene == d.scene_path) {
        error = "That is the scene open here: a door leads to another one.";
        return Guid{};
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        error = "That is not a place on the map.";
        return Guid{};
    }
    // A name logic can use: door_to_ and the scene's name.
    const std::string stem = std::filesystem::path(scene).stem().string();
    const std::string base = door_name(stem);
    std::string name = base;
    for (int n = 2; !d.world->find_named(name).empty(); ++n) {
        name = base + "_" + std::to_string(n);
    }
    // The other scene as it is now (with a door back made earlier and not yet saved).
    const std::string there(scene);
    const std::optional<serialize::Json> other = d.other_scene(there);
    // A door back from there (placed earlier) is where the player arrives, and through it
    // arrives at this one. Arriving in a trigger area does not count as walking in, so
    // nobody is sent straight back (game.md, scenes). With none, one is made there.
    const std::string back = door_name(fs::path(d.scene_path).stem().string());
    const bool had_back = other && scene_has_entity(*other, back);
    const double units = static_cast<double>(std::max<std::uint32_t>(d.settings.ppu, 1));
    const double at_x = std::round(x * units) / units;
    const double at_y = std::round(y * units) / units;
    std::optional<serialize::Json> with_back =
        other && !had_back ? with_door(*other, d.registry, back, at_x, at_y) : std::nullopt;
    if (with_back && !d.others.contains(there)) {
        // That scene as its file has it, in every state undo and redo can go back to and
        // in the saved one: whichever is saved, its file is then written as it should be.
        d.others[there] = *other;
        for (std::vector<Step>* list : {&d.undo, &d.redo}) {
            for (Step& step : *list) {
                for (serialize::Json* state : {&step.before, &step.after}) {
                    (*state)["others"].emplace(there, *other);
                }
            }
        }
        d.saved["others"].emplace(there, *other);
    }
    // Made of ordinary edits, then folded into one step; undone at once if one fails.
    end_merge();
    const std::size_t steps = d.undo.size();
    const std::vector<Step> redo = d.redo;
    serialize::Json before = d.snapshot();
    const auto fail = [&] {
        d.restore(before);
        d.undo.resize(steps);
        d.redo = redo;
        d.merging = false;
        return Guid{};
    };
    const Guid door = create_entity(name, at_x, at_y);
    const reflect::TypeInfo* trigger = d.registry.find_type<game::Trigger>();
    if (!door.is_valid() || trigger == nullptr || !add_component(door, *trigger)) {
        error = "The door could not be made.";
        return fail();
    }
    const std::string doors = "logic/doors.drules";
    if (d.logic_file(doors) == nullptr && !create_logic_file(doors, error)) {
        return fail();
    }
    const bool made_back = with_back.has_value();
    if (made_back) {
        d.others[there] = std::move(*with_back);
        d.read_elsewhere(); // so Rules can name the door back
    }
    const bool has_back = had_back || made_back;
    const logic::BlockId rule = add_logic_rule(doors, LogicEvent{name, "entered", {}}, error);
    const logic::BlockId go =
        rule.is_valid()
            ? add_logic_step(doors, rule,
                             LogicAction{"game", has_back ? "go_to_scene_at" : "go_to_scene", {}},
                             error)
            : logic::BlockId{};
    if (!go.is_valid() ||
        !set_logic_slot(doors, go, 0, reflect::Value{std::string(scene)}, error) ||
        (has_back && !set_logic_slot(doors, go, 1, reflect::Value{back}, error))) {
        return fail();
    }
    if (made_back) {
        // The new door back's own Rule: here, arriving at this door.
        const logic::BlockId back_rule =
            add_logic_rule(doors, LogicEvent{back, "entered", {}}, error);
        const logic::BlockId back_go =
            back_rule.is_valid()
                ? add_logic_step(doors, back_rule, LogicAction{"game", "go_to_scene_at", {}}, error)
                : logic::BlockId{};
        if (!back_go.is_valid() ||
            !set_logic_slot(doors, back_go, 0, reflect::Value{d.scene_path}, error) ||
            !set_logic_slot(doors, back_go, 1, reflect::Value{name}, error)) {
            return fail();
        }
    } else if (had_back) {
        // The door back's own Rule, if it simply goes here, now arrives at this door.
        for (LogicFile& file : d.logic) {
            for (logic::Rule& other_rule : file.asset.rules) {
                if (other_rule.object != back || other_rule.event != "entered") {
                    continue;
                }
                for (logic::Stmt& step : other_rule.actions) {
                    const auto* to = step.arguments.size() == 1
                                         ? std::get_if<std::string>(&step.arguments[0].literal)
                                         : nullptr;
                    if (step.kind == logic::StmtKind::Do && step.object == "game" &&
                        step.member == "go_to_scene" &&
                        step.arguments[0].kind == logic::ExprKind::Literal && to != nullptr &&
                        *to == d.scene_path) {
                        step.member = "go_to_scene_at";
                        step.arguments.push_back(logic::ir::literal(reflect::Value{name}));
                    }
                }
            }
        }
    }
    d.undo.resize(steps);
    d.undo.push_back(Step{"Place a door to " + stem, std::move(before), d.snapshot(), {}, {}});
    d.redo.clear();
    d.merging = false;
    return door;
}

namespace {

/// An angle brought into -180 to 180, never -0.
double within_half_turn(double degrees) noexcept {
    double turn = std::fmod(degrees, 360.0); // -360 to 360
    if (turn > 180.0) {
        turn -= 360.0;
    } else if (turn <= -180.0) {
        turn += 360.0;
    }
    return turn == 0.0 ? 0.0 : turn;
}

} // namespace

bool Document::turn_sprite(Guid entity, double degrees) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    auto* sprite = e.is_valid() ? d.world->get<scene2d::Sprite>(e) : nullptr;
    if (sprite == nullptr || !std::isfinite(degrees)) {
        return false;
    }
    serialize::Json before = d.snapshot();
    sprite->rotation = within_half_turn(degrees);
    d.record("Turn " + d.name_of(entity), std::move(before), entity, "sprite/rotation");
    return true;
}

bool Document::scale_sprite(Guid entity, double scale_x, double scale_y) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    auto* sprite = e.is_valid() ? d.world->get<scene2d::Sprite>(e) : nullptr;
    if (sprite == nullptr || !std::isfinite(scale_x) || !std::isfinite(scale_y)) {
        return false;
    }
    serialize::Json before = d.snapshot();
    sprite->scale_x = std::clamp(scale_x, -16.0, 16.0);
    sprite->scale_y = std::clamp(scale_y, -16.0, 16.0);
    d.record("Scale " + d.name_of(entity), std::move(before), entity, "sprite/scale");
    return true;
}

bool Document::shape_sprites(const std::vector<SpriteShape>& to, bool turning) {
    Impl& d = *impl_;
    struct Target {
        scene2d::Transform2D* at;
        scene2d::Sprite* sprite;
        const SpriteShape* shape;
    };
    std::vector<Target> targets;
    targets.reserve(to.size());
    std::string group; // the GUIDs shaped, so the same group's changes merge
    for (const SpriteShape& shape : to) {
        const world::Entity e = d.find(shape.entity);
        auto* at = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
        auto* sprite = e.is_valid() ? d.world->get<scene2d::Sprite>(e) : nullptr;
        const bool finite = std::isfinite(shape.x) && std::isfinite(shape.y) &&
                            std::isfinite(shape.rotation) && std::isfinite(shape.scale_x) &&
                            std::isfinite(shape.scale_y);
        if (at == nullptr || sprite == nullptr || !finite) {
            return false; // all or none
        }
        targets.push_back(Target{at, sprite, &shape});
        group += shape.entity.to_string();
    }
    if (targets.empty()) {
        return false;
    }
    serialize::Json before = d.snapshot();
    for (const Target& t : targets) {
        t.at->x = t.shape->x;
        t.at->y = t.shape->y;
        t.sprite->rotation = within_half_turn(t.shape->rotation);
        t.sprite->scale_x = std::clamp(t.shape->scale_x, -16.0, 16.0);
        t.sprite->scale_y = std::clamp(t.shape->scale_y, -16.0, 16.0);
    }
    d.record(std::string(turning ? "Turn " : "Scale ") + std::to_string(targets.size()) +
                 " entities",
             std::move(before), Guid{},
             (turning ? "sprite/group-turn:" : "sprite/group-scale:") + group);
    return true;
}

bool Document::raise_tile(Guid tilemap, double x, double y, double by) {
    Impl& d = *impl_;
    const world::Entity e = d.find(tilemap);
    auto* map = e.is_valid() ? d.world->get<scene2d::Tilemap>(e) : nullptr;
    const auto* transform = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
    if (map == nullptr || transform == nullptr || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(by)) {
        return false;
    }
    std::string error;
    const scene2d::TileGrid* parsed = map->grid(error);
    if (parsed == nullptr) {
        return false;
    }
    const double units = static_cast<double>(std::max<std::uint32_t>(d.settings.ppu, 1));
    const double tile_size =
        (map->tile_size == 0 ? units : static_cast<double>(map->tile_size)) / units;
    const double column = std::floor((x - transform->x) / tile_size);
    const double row = std::floor((y - transform->y) / tile_size);
    if (column < 0.0 || row < 0.0 || column >= static_cast<double>(parsed->width) ||
        row >= static_cast<double>(parsed->height)) {
        return false;
    }
    const auto cx = static_cast<std::uint32_t>(column);
    const auto cy = static_cast<std::uint32_t>(row);
    const double now = map->height_at(cx, cy);
    const double next = std::clamp(now + by, 0.0, 16.0);
    if (next == now) {
        return true; // already as high (or low) as it goes
    }
    std::vector<std::vector<double>> heights = scene2d::parse_heights(map->heights);
    if (heights.size() <= cy) {
        heights.resize(cy + 1);
    }
    if (heights[cy].size() <= cx) {
        heights[cy].resize(cx + 1, std::numeric_limits<double>::quiet_NaN());
    }
    heights[cy][cx] = next;
    serialize::Json before = d.snapshot();
    map->heights = scene2d::format_heights(heights);
    d.record((by > 0 ? "Raise " : "Lower ") + d.name_of(tilemap), std::move(before), tilemap,
             "tilemap/height");
    return true;
}

namespace {

/// The tile to paint into cell (c, r): `tile`, or, when it is one of `variants`, one of
/// them picked by a hash of the cell and the round, so the same stroke is the same.
std::int32_t variant_for(std::int32_t tile, const std::vector<std::int32_t>& variants,
                         std::uint32_t c, std::uint32_t r, std::uint32_t round) {
    if (tile < 0 || variants.size() < 2 ||
        std::find(variants.begin(), variants.end(), tile) == variants.end()) {
        return tile;
    }
    std::uint32_t hash = (c * 73856093U) ^ (r * 19349663U) ^ (round * 83492791U);
    hash ^= hash >> 13U;
    hash *= 0x5bd1e995U;
    hash ^= hash >> 15U;
    return variants[hash % variants.size()];
}

/// On an auto-tile map, the terrain a tile is part of; -1 for none.
std::int32_t terrain_of(std::int32_t tile) {
    return tile < 0 ? -1 : tile / static_cast<std::int32_t>(assets::autotile_shapes);
}

/// On an auto-tile map, gives every painted cell from (c0, r0) to (c1, r1), and those around
/// them, the shape its neighbours of the same terrain make. Past the map's edges is empty,
/// as the cells a growing map will add are.
void autotile_cells(scene2d::TileGrid& grid, std::uint32_t c0, std::uint32_t r0, std::uint32_t c1,
                    std::uint32_t r1) {
    if (grid.width == 0 || grid.height == 0) {
        return;
    }
    c0 = c0 > 0 ? c0 - 1 : 0;
    r0 = r0 > 0 ? r0 - 1 : 0;
    c1 = std::min(c1 + 1, grid.width - 1);
    r1 = std::min(r1 + 1, grid.height - 1);
    const scene2d::TileGrid before = grid;
    for (std::uint32_t r = r0; r <= r1; ++r) {
        for (std::uint32_t c = c0; c <= c1; ++c) {
            const std::int32_t terrain = terrain_of(before.at(c, r));
            if (terrain < 0) {
                continue;
            }
            const auto same = [&](std::int64_t dc, std::int64_t dr) {
                const std::int64_t x = std::int64_t{c} + dc;
                const std::int64_t y = std::int64_t{r} + dr;
                if (x < 0 || y < 0 || x >= std::int64_t{before.width} ||
                    y >= std::int64_t{before.height}) {
                    return false;
                }
                return terrain_of(before.at(static_cast<std::uint32_t>(x),
                                            static_cast<std::uint32_t>(y))) == terrain;
            };
            std::uint8_t m = 0;
            const std::array<std::tuple<int, int, std::uint8_t>, 8> around{
                {{0, -1, assets::north},
                 {1, 0, assets::east},
                 {0, 1, assets::south},
                 {-1, 0, assets::west},
                 {1, -1, assets::north_east},
                 {1, 1, assets::south_east},
                 {-1, 1, assets::south_west},
                 {-1, -1, assets::north_west}}};
            for (const auto& [dc, dr, bit] : around) {
                if (same(dc, dr)) {
                    m = static_cast<std::uint8_t>(m | bit);
                }
            }
            grid.cells[(std::size_t{r} * grid.width) + c] =
                (terrain * static_cast<std::int32_t>(assets::autotile_shapes)) +
                static_cast<std::int32_t>(assets::autotile_shape(m));
        }
    }
}

} // namespace

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
    const bool varied = !map->autotile && tile >= 0 && d.variants.size() > 1 &&
                        std::find(d.variants.begin(), d.variants.end(), tile) != d.variants.end();
    if (cx < parsed->width && cy < parsed->height &&
        (parsed->at(cx, cy) == tile ||
         (map->autotile && terrain_of(parsed->at(cx, cy)) == terrain_of(tile)) ||
         (varied && std::find(d.variants.begin(), d.variants.end(), parsed->at(cx, cy)) !=
                        d.variants.end()))) {
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
    grid.cells[(std::size_t{cy} * grid.width) + cx] =
        map->autotile ? tile : variant_for(tile, d.variants, cx, cy, ++d.variant_round);
    if (map->autotile) {
        autotile_cells(grid, cx, cy, cx, cy);
    }
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
    const std::uint32_t round = ++d.variant_round;
    for (std::uint32_t r = r0; r <= r1; ++r) {
        for (std::uint32_t c = c0; c <= c1; ++c) {
            std::int32_t& cell = grid.cells[(std::size_t{r} * grid.width) + c];
            const std::int32_t next =
                map->autotile ? tile : variant_for(tile, d.variants, c, r, round);
            changed = changed || cell != next;
            cell = next;
        }
    }
    if (!changed) {
        return true; // already so
    }
    if (map->autotile) {
        autotile_cells(grid, c0, r0, c1, r1);
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
    // On an auto-tile map, every shape of a terrain is that terrain.
    const auto alike = [&](std::int32_t a, std::int32_t b) {
        return map->autotile ? terrain_of(a) == terrain_of(b) : a == b;
    };
    if (alike(old, tile)) {
        return true; // already so
    }
    scene2d::TileGrid grid = grown(*parsed, parsed->width, parsed->height);
    // Every cell joined to the first by its sides through cells of the same tile.
    std::vector<std::array<std::uint32_t, 2>> open{{cx, cy}};
    const std::uint32_t round = ++d.variant_round;
    const auto pick = [&](std::uint32_t c, std::uint32_t r) {
        return map->autotile ? tile : variant_for(tile, d.variants, c, r, round);
    };
    // Cells already painted this fill, so variants (which differ from the tile) are not
    // filled again.
    std::vector<bool> done(grid.cells.size(), false);
    done[(std::size_t{cy} * grid.width) + cx] = true;
    grid.cells[(std::size_t{cy} * grid.width) + cx] = pick(cx, cy);
    while (!open.empty()) {
        const std::array<std::uint32_t, 2> cell = open.back();
        open.pop_back();
        const auto visit = [&](std::uint32_t c, std::uint32_t r) {
            const std::size_t index = (std::size_t{r} * grid.width) + c;
            std::int32_t& next = grid.cells[index];
            if (!done[index] && next != tile && alike(next, old)) {
                done[index] = true;
                next = pick(c, r);
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
    if (map->autotile) {
        autotile_cells(grid, 0, 0, grid.width - 1, grid.height - 1);
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

bool Document::make_prefab(Guid entity, std::string_view name, std::string& error) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    const std::string trimmed(name);
    if (!e.is_valid()) {
        error = "There is no such entity.";
        return false;
    }
    if (trimmed.empty() || trimmed.find_first_of("/\\:") != std::string::npos ||
        trimmed.front() == '.') {
        error = "A prefab needs a name without slashes, such as house.";
        return false;
    }
    const std::string path = "prefabs/" + trimmed + ".dprefab";
    const fs::path file = d.folder / "assets" / path;
    std::error_code ignored;
    if (fs::exists(file, ignored)) {
        error = "There is a prefab called " + trimmed + " already.";
        return false;
    }
    serialize::Json json = serialize::subtree_to_json(*d.world, e);
    // The root's place belongs to each copy, not to the prefab.
    if (!json["entities"].empty()) {
        serialize::Json& root = json["entities"][0];
        if (root.contains("components") && root["components"].contains("transform_2d")) {
            root["components"]["transform_2d"]["x"] = 0.0;
            root["components"]["transform_2d"]["y"] = 0.0;
        }
        if (root.contains("components")) {
            root["components"].erase("prefab_link");
        }
    }
    if (!game::write_file_atomically(file, serialize::to_text(json), error)) {
        return false;
    }
    static_cast<void>(d.assets->refresh());
    const engine::assets::AssetRecord* record = d.assets->find(path);
    if (record == nullptr) {
        error = "The prefab was written but could not be read back.";
        return false;
    }
    serialize::Json before = d.snapshot();
    if (auto* link = d.world->add<game::PrefabLink>(e)) {
        link->prefab = record->guid.to_string();
    }
    d.record("Make prefab " + trimmed, std::move(before));
    return true;
}

std::string Document::prefab_of(Guid entity) const {
    const Impl& d = *impl_;
    const world::Entity e = d.world->find(entity);
    const auto* link = e.is_valid() ? d.world->get<game::PrefabLink>(e) : nullptr;
    if (link == nullptr) {
        return {};
    }
    const std::optional<Guid> guid = Guid::parse(link->prefab);
    const engine::assets::AssetRecord* record = guid ? d.assets->find(*guid) : nullptr;
    return record != nullptr ? record->path : std::string();
}

namespace {

/// A prefab file's scene document, or nothing with `error`.
std::optional<serialize::Json>
read_prefab(const fs::path& folder, const engine::assets::AssetRecord* record, std::string& error) {
    if (record == nullptr || !record->path.ends_with(".dprefab")) {
        error = "That is not one of the project's prefabs.";
        return std::nullopt;
    }
    std::ifstream in(folder / "assets" / record->path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<Issue> issues;
    std::optional<serialize::Json> json = serialize::parse(text, issues);
    if (!json || !json->contains("entities") || !(*json)["entities"].is_array() ||
        (*json)["entities"].empty()) {
        error = record->path + " cannot be read as a prefab.";
        return std::nullopt;
    }
    return json;
}

} // namespace

std::string Document::copy_entities(const std::vector<Guid>& entities) const {
    const Impl& d = *impl_;
    serialize::Json all = serialize::subtree_to_json(*d.world, world::Entity{});
    all["entities"] = serialize::Json::array();
    for (const Guid guid : entities) {
        const world::Entity e = d.find(guid);
        if (!e.is_valid()) {
            continue;
        }
        // Under another of them: it comes with that one.
        bool inside = false;
        for (world::Entity up = d.world->parent(e); up.is_valid() && !inside;
             up = d.world->parent(up)) {
            inside =
                std::find(entities.begin(), entities.end(), d.world->guid(up)) != entities.end();
        }
        if (inside) {
            continue;
        }
        // Kept whole while its entities are taken: a temporary's member would not outlive
        // the loop's start.
        serialize::Json subtree = serialize::subtree_to_json(*d.world, e);
        for (serialize::Json& item : subtree["entities"]) {
            all["entities"].push_back(std::move(item));
        }
    }
    return serialize::to_text(all);
}

std::vector<Guid> Document::paste_entities(std::string_view text, double dx, double dy,
                                           std::string& error) {
    Impl& d = *impl_;
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> json = serialize::parse(text, issues);
    if (!json || !json->is_object() || !json->contains("entities") || (*json)["entities"].empty() ||
        !std::isfinite(dx) || !std::isfinite(dy)) {
        error = "There are no entities to paste.";
        return {};
    }
    const serialize::Json copy = serialize::with_new_guids(*json);
    serialize::Json before = d.snapshot();
    const serialize::SceneLoad loaded = serialize::scene_from_json(copy, *d.world);
    if (loaded.entities.empty()) {
        error = loaded.issues.empty() ? std::string("There are no entities to paste.")
                                      : loaded.issues.front().message;
        return {};
    }
    std::vector<Guid> roots;
    for (const world::Entity e : loaded.entities) {
        // A name no other entity has: hero, hero_2, hero_3...
        const std::string name(d.world->name(e));
        if (!name.empty() && d.world->find_named(name).size() > 1) {
            std::string base = name;
            if (const std::size_t under = base.find_last_of('_');
                under != std::string::npos && under + 1 < base.size() &&
                std::all_of(base.begin() + static_cast<std::ptrdiff_t>(under) + 1, base.end(),
                            [](unsigned char c) { return std::isdigit(c) != 0; })) {
                base.erase(under);
            }
            for (int n = 2;; ++n) {
                const std::string candidate = base + "_" + std::to_string(n);
                if (d.world->find_named(candidate).empty()) {
                    d.world->set_name(e, candidate);
                    break;
                }
            }
        }
        if (d.world->parent(e).is_valid()) {
            continue;
        }
        if (auto* at = d.world->get<scene2d::Transform2D>(e)) {
            at->x += dx;
            at->y += dy;
        }
        roots.push_back(d.world->guid(e));
    }
    const std::string label = roots.size() == 1
                                  ? "Paste " + d.name_of(roots.front())
                                  : "Paste " + std::to_string(roots.size()) + " entities";
    d.record(label, std::move(before));
    return roots;
}

std::vector<Guid> Document::duplicate(const std::vector<Guid>& entities, std::string& error) {
    Impl& d = *impl_;
    std::vector<Guid> kept;
    for (const Guid guid : entities) {
        if (const world::Entity e = d.find(guid); e.is_valid()) {
            kept.push_back(guid);
        }
    }
    if (kept.empty()) {
        error = "Nothing is selected.";
        return {};
    }
    const std::string label = kept.size() == 1
                                  ? "Duplicate " + d.name_of(kept.front())
                                  : "Duplicate " + std::to_string(kept.size()) + " entities";
    serialize::Json before = d.snapshot();
    std::vector<Guid> copies = paste_entities(copy_entities(kept), 1.0, 1.0, error);
    if (copies.empty()) {
        return copies;
    }
    // Each beside its original, under the same parent. (The paste's own step is replaced.)
    std::size_t i = 0;
    for (const Guid original : kept) {
        const world::Entity e = d.find(original);
        bool inside = false;
        for (world::Entity up = d.world->parent(e); up.is_valid() && !inside;
             up = d.world->parent(up)) {
            inside = std::find(kept.begin(), kept.end(), d.world->guid(up)) != kept.end();
        }
        if (inside || i >= copies.size()) {
            continue;
        }
        if (const world::Entity parent = d.world->parent(e); parent.is_valid()) {
            d.world->set_parent(d.find(copies[i]), parent);
        }
        ++i;
    }
    // One step for all of it: the paste's own step gives way to this one.
    if (!d.undo.empty()) {
        d.undo.pop_back();
    }
    d.record(label, std::move(before));
    return copies;
}

Guid Document::place_prefab(std::string_view prefab, double x, double y, std::string& error) {
    Impl& d = *impl_;
    const engine::assets::AssetRecord* record = d.assets->find(prefab);
    std::optional<serialize::Json> json = read_prefab(d.folder, record, error);
    if (!json) {
        return Guid{};
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        error = "That point is not on the map.";
        return Guid{};
    }
    const serialize::Json copy = serialize::with_new_guids(*json);
    serialize::Json before = d.snapshot();
    const serialize::SceneLoad loaded = serialize::scene_from_json(copy, *d.world);
    if (loaded.entities.empty()) {
        error = loaded.issues.empty() ? std::string("The prefab is empty.")
                                      : loaded.issues.front().message;
        return Guid{};
    }
    const world::Entity root = loaded.entities.front();
    const double ppu = std::max(1U, d.settings.ppu);
    auto* at = d.world->get<scene2d::Transform2D>(root);
    if (at == nullptr) {
        at = d.world->add<scene2d::Transform2D>(root);
    }
    if (at != nullptr) {
        at->x = std::round(x * ppu) / ppu;
        at->y = std::round(y * ppu) / ppu;
    }
    if (auto* link = d.world->add<game::PrefabLink>(root)) {
        link->prefab = record->guid.to_string();
    }
    const Guid guid = d.world->guid(root);
    d.record("Place " + fs::path(record->path).stem().string(), std::move(before));
    return guid;
}

std::size_t Document::update_prefab_copies(std::string_view prefab, std::string& error) {
    Impl& d = *impl_;
    const engine::assets::AssetRecord* record = d.assets->find(prefab);
    std::optional<serialize::Json> json = read_prefab(d.folder, record, error);
    if (!json) {
        return 0;
    }
    const std::string link_guid = record->guid.to_string();
    // The copies, by their root's GUID, name and place.
    struct Copy {
        Guid guid;
        std::string name;
        std::optional<std::array<double, 2>> place;
        Guid parent;
    };
    std::vector<Copy> copies;
    for (const world::Entity e : entities()) {
        const auto* link = d.world->get<game::PrefabLink>(e);
        if (link != nullptr && link->prefab == link_guid) {
            const auto* at = d.world->get<scene2d::Transform2D>(e);
            const world::Entity parent = d.world->parent(e);
            copies.push_back(Copy{d.world->guid(e), std::string(d.world->name(e)),
                                  at != nullptr ? std::optional<std::array<double, 2>>(
                                                      std::array<double, 2>{at->x, at->y})
                                                : std::nullopt,
                                  parent.is_valid() ? d.world->guid(parent) : Guid{}});
        }
    }
    if (copies.empty()) {
        return 0;
    }
    serialize::Json before = d.snapshot();
    for (const Copy& copy : copies) {
        d.world->destroy(d.world->find(copy.guid));
        serialize::Json fresh = serialize::with_new_guids(*json);
        serialize::Json& root = fresh["entities"][0];
        const serialize::Json made_root = root["guid"];
        root["guid"] = copy.guid.to_string(); // the copy keeps its own
        root["name"] = copy.name;
        if (copy.parent.is_valid()) {
            root["parent"] = copy.parent.to_string();
        }
        // Its children name the root by the GUID just made: point them at the kept one.
        for (serialize::Json& item : fresh["entities"]) {
            if (item.contains("parent") && item["parent"] == made_root) {
                item["parent"] = copy.guid.to_string();
            }
        }
        const serialize::SceneLoad loaded = serialize::scene_from_json(fresh, *d.world);
        if (loaded.entities.empty()) {
            continue;
        }
        const world::Entity e = loaded.entities.front();
        if (const std::optional<std::array<double, 2>> where = copy.place; where.has_value()) {
            const std::array<double, 2> place = *where;
            auto* at = d.world->get<scene2d::Transform2D>(e);
            if (at == nullptr) {
                at = d.world->add<scene2d::Transform2D>(e);
            }
            if (at != nullptr) {
                at->x = place[0];
                at->y = place[1];
            }
        }
        if (auto* link = d.world->add<game::PrefabLink>(e)) {
            link->prefab = link_guid;
        }
    }
    d.record("Update " + std::to_string(copies.size()) + " copies of " +
                 fs::path(record->path).stem().string(),
             std::move(before));
    return copies.size();
}

Guid Document::place_asset(std::string_view asset_path, double x, double y, std::string& error) {
    Impl& d = *impl_;
    if (asset_path.ends_with(".dprefab")) {
        return place_prefab(asset_path, x, y, error);
    }
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

bool Document::set_parent(Guid entity, Guid parent) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    const world::Entity p = parent.is_valid() ? d.find(parent) : world::Entity{};
    if (!e.is_valid() || (parent.is_valid() && !p.is_valid()) || d.world->parent(e) == p) {
        return false;
    }
    const std::string label = parent.is_valid()
                                  ? "Put " + d.name_of(entity) + " under " + d.name_of(parent)
                                  : "Put " + d.name_of(entity) + " at the top";
    serialize::Json before = d.snapshot();
    if (!d.world->set_parent(e, p)) {
        return false;
    }
    d.record(label, std::move(before));
    return true;
}

bool Document::rename_many(const std::vector<Guid>& entities, std::string_view base) {
    Impl& d = *impl_;
    if (base.empty()) {
        return false;
    }
    std::vector<world::Entity> named;
    for (const Guid guid : entities) {
        if (const world::Entity e = d.find(guid); e.is_valid()) {
            named.push_back(e);
        }
    }
    if (named.empty()) {
        return false;
    }
    // Names taken by entities not being renamed.
    const auto taken = [&](const std::string& name) {
        for (const world::Entity other : d.world->find_named(name)) {
            if (std::find(named.begin(), named.end(), other) == named.end()) {
                return true;
            }
        }
        return false;
    };
    serialize::Json before = d.snapshot();
    std::size_t number = 1;
    for (const world::Entity e : named) {
        std::string name;
        do {
            name = std::string(base) + "_" + std::to_string(number++);
        } while (taken(name));
        if (!d.world->set_name(e, name)) {
            d.restore(before);
            return false;
        }
    }
    d.record("Rename " + std::to_string(named.size()) + " entities", std::move(before));
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

// ---------------------------------------------------------------------------------------
// Autosave and crash recovery
// ---------------------------------------------------------------------------------------

namespace {

constexpr std::string_view recovery_format = "sparxel.recovery";

std::optional<serialize::Json> read_recovery(const fs::path& folder, std::string& error) {
    std::ifstream in(folder / recovery_file, std::ios::binary);
    if (!in) {
        error = "There is no unsaved work to restore.";
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<Issue> issues;
    std::optional<serialize::Json> json = serialize::parse(text, issues);
    if (!json || !json->is_object() || json->value("format", std::string()) != recovery_format ||
        !json->contains("scene_path") || !(*json)["scene_path"].is_string() ||
        !json->contains("state") || !(*json)["state"].is_object()) {
        error = "The unsaved work could not be read: its file is damaged.";
        return std::nullopt;
    }
    const serialize::Json& state = (*json)["state"];
    for (const char* key : {"scene", "logic", "project", "others"}) {
        if (!state.contains(key)) {
            error = "The unsaved work could not be read: its file is damaged.";
            return std::nullopt;
        }
    }
    return json;
}

} // namespace

std::optional<std::string> recovery_scene(const fs::path& folder) {
    std::string ignored;
    const std::optional<serialize::Json> json = read_recovery(folder, ignored);
    if (!json) {
        return std::nullopt;
    }
    return (*json)["scene_path"].get<std::string>();
}

bool Document::write_recovery(std::string& error) {
    const Impl& d = *impl_;
    if (!dirty()) {
        discard_recovery();
        return true;
    }
    std::error_code made;
    fs::create_directories((d.folder / recovery_file).parent_path(), made);
    const serialize::Json json{{"format", recovery_format},
                               {"version", 1},
                               {"scene_path", d.scene_path},
                               {"state", d.snapshot()}};
    if (made ||
        !game::write_file_atomically(d.folder / recovery_file, serialize::to_text(json), error)) {
        if (error.empty()) {
            error = made.message();
        }
        error.insert(0, "The unsaved work could not be kept: ");
        return false;
    }
    return true;
}

void Document::discard_recovery() {
    std::error_code ignored;
    fs::remove(impl_->folder / recovery_file, ignored);
}

bool Document::restore_recovery(std::string& error) {
    Impl& d = *impl_;
    const std::optional<serialize::Json> json = read_recovery(d.folder, error);
    if (!json) {
        return false;
    }
    const std::string scene = (*json)["scene_path"].get<std::string>();
    if (scene != d.scene_path && !open_scene(scene, error)) {
        error.insert(0, "The unsaved work belongs to " + scene + ": ");
        return false;
    }
    serialize::Json before = d.snapshot();
    try {
        d.restore((*json)["state"]);
    } catch (const std::exception&) {
        d.restore(before);
        error = "The unsaved work could not be read: its file is damaged.";
        return false;
    }
    d.merging = false;
    d.record("Restore unsaved work", std::move(before));
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
    // Files made here and then undone away (a door's logic/doors.drules) go again, with
    // their .meta, so the game does not run Rules about what is no longer there.
    std::vector<std::string> gone;
    for (const auto& [path, json] : d.saved["logic"].items()) {
        if (!state.at("logic").contains(path)) {
            gone.push_back(path);
        }
    }
    for (const std::string& path : gone) {
        std::error_code failed;
        const fs::path file = d.folder / "assets" / path;
        fs::remove(file, failed);
        if (!failed) {
            fs::remove(fs::path(file.string() + ".meta"), failed);
        }
        if (failed) {
            error = "assets/" + path + ": it could not be removed (" + failed.message() + ").";
            return false;
        }
        d.saved["logic"].erase(path);
        written_logic = true;
    }
    if (written_logic) {
        // A new file gets its .meta, and so its GUID, as any asset does.
        static_cast<void>(d.assets->refresh());
    }
    // Other scenes changed here (a door back), or put back by undo.
    for (const auto& [path, json] : state.at("others").items()) {
        serialize::Json& saved_file = d.saved["others"][path];
        if (json == saved_file) {
            continue;
        }
        if (!game::write_file_atomically(d.folder / "assets" / path, serialize::to_text(json),
                                         error)) {
            error.insert(0, "assets/" + path + ": ");
            return false;
        }
        saved_file = json;
    }
    d.merging = false;
    discard_recovery(); // the work is safe in the project's files
    return true;
}

// ---------------------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------------------

namespace {

/// The size in tiles of the frame `entity`'s Sprite shows, as the game draws it (a frame
/// larger than the picture is the whole picture). Nothing without a readable picture.
std::optional<std::array<double, 2>> sprite_size(const world::World& w,
                                                 const engine::assets::AssetDatabase& assets,
                                                 world::Entity entity, std::uint32_t ppu) {
    const scene2d::Sprite* sprite = w.get<scene2d::Sprite>(entity);
    const std::optional<Guid> image = sprite != nullptr ? Guid::parse(sprite->image) : std::nullopt;
    if (!image) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::uint8_t>> bytes = assets.read(*image);
    std::string error;
    const std::optional<engine::assets::Image> picture =
        bytes ? engine::assets::decode_png(*bytes, error) : std::nullopt;
    if (!picture) {
        return std::nullopt;
    }
    const auto side = [](std::uint32_t frame, std::uint32_t whole) {
        return frame == 0 || frame > whole ? whole : frame;
    };
    const double units = static_cast<double>(std::max<std::uint32_t>(ppu, 1));
    return std::array<double, 2>{
        static_cast<double>(side(sprite->frame_width, picture->width)) / units,
        static_cast<double>(side(sprite->frame_height, picture->height)) / units};
}

} // namespace

std::optional<std::array<std::array<double, 2>, 4>>
sprite_corners_on_map(const world::World& w, const engine::assets::AssetDatabase& assets,
                      world::Entity entity, std::uint32_t ppu) {
    const scene2d::Sprite* sprite = w.get<scene2d::Sprite>(entity);
    const scene2d::Transform2D* t = w.get<scene2d::Transform2D>(entity);
    const std::optional<std::array<double, 2>> size = sprite_size(w, assets, entity, ppu);
    if (sprite == nullptr || t == nullptr || !size) {
        return std::nullopt;
    }
    // Scaled, flipped and turned about its pivot, which is on the position.
    std::array<std::array<double, 2>, 4> corners =
        scene2d::sprite_corners(*sprite, (*size)[0], (*size)[1]);
    for (auto& [x, y] : corners) {
        x += t->x;
        y += t->y;
    }
    return corners;
}

std::optional<scene2d::Box> sprite_bounds(const world::World& w,
                                          const engine::assets::AssetDatabase& assets,
                                          world::Entity entity, std::uint32_t ppu) {
    const auto corners = sprite_corners_on_map(w, assets, entity, ppu);
    if (!corners) {
        return std::nullopt;
    }
    // The box around the picture as drawn.
    const auto& first = (*corners)[0];
    scene2d::Box box{first[0], first[1], first[0], first[1]};
    for (const auto& [x, y] : *corners) {
        box.left = std::min(box.left, x);
        box.top = std::min(box.top, y);
        box.right = std::max(box.right, x);
        box.bottom = std::max(box.bottom, y);
    }
    return box;
}

bool Document::set_sprite_pivot(Guid entity, double pivot_x, double pivot_y) {
    Impl& d = *impl_;
    const world::Entity e = d.find(entity);
    auto* sprite = e.is_valid() ? d.world->get<scene2d::Sprite>(e) : nullptr;
    auto* at = e.is_valid() ? d.world->get<scene2d::Transform2D>(e) : nullptr;
    const std::optional<std::array<double, 2>> size =
        sprite_size(*d.world, *d.assets, e, d.settings.ppu);
    if (sprite == nullptr || at == nullptr || !size || !std::isfinite(pivot_x) ||
        !std::isfinite(pivot_y)) {
        return false;
    }
    // The picture stays where it is: its top-left corner on the same map point.
    scene2d::Sprite moved = *sprite;
    moved.pivot_x = std::clamp(pivot_x, 0.0, 1.0);
    moved.pivot_y = std::clamp(pivot_y, 0.0, 1.0);
    const auto before_corners = scene2d::sprite_corners(*sprite, (*size)[0], (*size)[1]);
    const auto after_corners = scene2d::sprite_corners(moved, (*size)[0], (*size)[1]);
    serialize::Json before = d.snapshot();
    at->x += before_corners[0][0] - after_corners[0][0];
    at->y += before_corners[0][1] - after_corners[0][1];
    sprite->pivot_x = moved.pivot_x;
    sprite->pivot_y = moved.pivot_y;
    d.record("Move the pivot of " + d.name_of(entity), std::move(before), entity, "sprite/pivot");
    return true;
}

namespace {

/// Whether map point (x, y) is inside `entity`'s turned picture, for a point already in
/// the box around it (which is the picture itself when it is not turned).
bool inside_turned(const world::World& w, const engine::assets::AssetDatabase& assets,
                   world::Entity entity, double x, double y, std::uint32_t ppu) {
    const scene2d::Sprite* sprite = w.get<scene2d::Sprite>(entity);
    const scene2d::Transform2D* t = w.get<scene2d::Transform2D>(entity);
    if (sprite == nullptr || t == nullptr || !std::isfinite(sprite->rotation) ||
        std::fmod(sprite->rotation, 90.0) == 0.0) {
        return true; // a quarter turn, or none: the box is the picture
    }
    const std::optional<std::array<double, 2>> size = sprite_size(w, assets, entity, ppu);
    if (!size) {
        return false;
    }
    const std::array<std::array<double, 2>, 4> c =
        scene2d::sprite_corners(*sprite, (*size)[0], (*size)[1]);
    // Inside a convex four-sided shape: on the same side of every edge.
    bool before = false;
    bool after = false;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& [ax, ay] = c[i];
        const auto& [bx, by] = c[(i + 1) % 4];
        const double side = ((bx - ax) * (y - t->y - ay)) - ((by - ay) * (x - t->x - ax));
        before = before || side < 0.0;
        after = after || side > 0.0;
    }
    return !(before && after);
}

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
            if (box && x >= box->left && x < box->right && y >= box->top && y < box->bottom &&
                inside_turned(w, assets, entity, x, y, ppu)) {
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
    const std::optional<std::array<double, 2>> size = sprite_size(w, assets, entity, camera.ppu);
    if (sprite == nullptr || t == nullptr || !size) {
        return std::nullopt;
    }
    const auto [width, height] = *size;
    // As draw_diorama() stands it: upright, facing the camera, pivot on the map point.
    const double yaw =
        (std::isfinite(camera.yaw) ? camera.yaw : 0.0) * 3.14159265358979323846 / 180.0;
    const double rx = std::cos(yaw);
    const double rz = -std::sin(yaw);
    const double lift = static_cast<double>(t->layer) * 0.002;
    const engine::render::Mat4 vp = scene2d::diorama_view_projection(camera, aspect);
    std::array<double, 4> rect{1e9, 1e9, -1e9, -1e9};
    // Scaled and turned in the billboard's plane, as draw_diorama() does: y down there is
    // down the card.
    for (const auto& [along, down] : scene2d::sprite_corners(*sprite, width, height)) {
        const double up = -down;
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
