#include "editor/core/doctor.h"

#include "engine/game/behaviours.h"
#include "engine/game/conversation.h"
#include "engine/game/tables.h"
#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"
#include "engine/ui/text.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <variant>

#include "editor/core/sprite.h"

namespace editor {

namespace fs = std::filesystem;
namespace reflect = engine::reflect;
namespace serialize = engine::serialize;
namespace world = engine::world;
using engine::Guid;

namespace {

/// The GUID a text property holds, if it is one: pictures, tilesets and sounds are named
/// by the 32 hexadecimal digits of their asset's GUID.
std::optional<Guid> asset_reference(const reflect::Value& value) {
    const std::string* text = std::get_if<std::string>(&value);
    if (text == nullptr || text->size() != 32) {
        return std::nullopt;
    }
    return Guid::parse(*text);
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}

/// Checks one scene's entities for assets that are gone, and counts what they use.
void check_scene(const world::World& scene, const std::string& path,
                 const engine::assets::AssetDatabase& assets, std::vector<Finding>& found,
                 std::set<Guid>& used) {
    for (const world::Entity e : scene.roots()) {
        // Every entity, depth first.
        std::vector<world::Entity> todo{e};
        while (!todo.empty()) {
            const world::Entity entity = todo.back();
            todo.pop_back();
            for (const world::Entity child : scene.children(entity)) {
                todo.push_back(child);
            }
            const std::string_view name = scene.name(entity);
            const std::string who =
                name.empty() ? std::string("an unnamed entity") : "\"" + std::string(name) + "\"";
            for (const reflect::TypeInfo* type : scene.components(entity)) {
                const reflect::ObjectRef object = scene.get(entity, *type);
                for (const reflect::PropertyInfo& property : type->properties) {
                    if (property.kind != reflect::ValueKind::Text) {
                        continue;
                    }
                    const std::optional<reflect::Value> value =
                        reflect::get_property(object, property);
                    const std::optional<Guid> guid = value ? asset_reference(*value) : std::nullopt;
                    if (!guid) {
                        continue;
                    }
                    used.insert(*guid);
                    if (assets.find(*guid) == nullptr) {
                        Finding f;
                        f.severity = Finding::Severity::Problem;
                        f.message = path;
                        f.message += ": the ";
                        f.message += type->meta.display_name;
                        f.message += " of ";
                        f.message += who;
                        f.message += " uses a file that is no longer in assets/ (its ";
                        f.message += property.meta.display_name;
                        f.message += "). Choose another in the inspector.";
                        f.scene = path;
                        f.entity = scene.guid(entity);
                        found.push_back(std::move(f));
                    }
                }
            }
        }
    }
}

} // namespace

namespace {

/// A note for each picture using colours outside `palette` (its see-through pixels aside).
void check_palette(const engine::assets::AssetDatabase& assets,
                   const std::vector<sprite::Color>& palette, std::vector<Finding>& found) {
    for (const engine::assets::AssetRecord& record : assets.assets()) {
        if (record.importer != "image") {
            continue;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = assets.read(record.guid);
        if (!bytes.has_value()) {
            continue;
        }
        std::string error;
        const std::optional<engine::assets::Image> image =
            engine::assets::decode_png(bytes.value(), error);
        if (!image.has_value()) {
            continue;
        }
        const std::vector<std::uint8_t>& rgba = image.value().rgba;
        std::vector<sprite::Color> outside;
        for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
            if (rgba[i + 3] == 0) {
                continue;
            }
            const sprite::Color c{rgba[i], rgba[i + 1], rgba[i + 2], 255};
            if (std::ranges::find(palette, c) == palette.end() &&
                std::ranges::find(outside, c) == outside.end()) {
                outside.push_back(c);
            }
        }
        if (outside.empty()) {
            continue;
        }
        const std::size_t shown = std::min<std::size_t>(outside.size(), 4);
        std::string message =
            record.path + " uses " + std::to_string(outside.size()) +
            (outside.size() == 1 ? " colour" : " colours") + " outside the project's palette: " +
            sprite::palette_text(
                {outside.begin(), outside.begin() + static_cast<std::ptrdiff_t>(shown)});
        if (outside.size() > shown) {
            message += " and " + std::to_string(outside.size() - shown) + " more";
        }
        message += ". Recolour them in Sprite Studio, or add them to the palette (Project).";
        Finding f;
        f.severity = Finding::Severity::Note;
        f.message = std::move(message);
        f.asset = record.path;
        found.push_back(std::move(f));
    }
}

} // namespace

std::vector<Finding> examine(const Document& document) {
    std::vector<Finding> found;
    const engine::assets::AssetDatabase& assets = document.assets();
    const engine::game::ProjectSettings& settings = document.settings();
    std::set<Guid> used;

    // The start scene.
    const engine::assets::AssetRecord* start = assets.find(settings.start_scene);
    if (start == nullptr || !start->path.ends_with(".dscene")) {
        Finding f;
        f.message = "The game starts in \"" + settings.start_scene +
                    "\", which is not a scene in assets/. Open a scene and choose File > Start "
                    "the game in this scene.";
        f.asset = settings.start_scene;
        found.push_back(std::move(f));
    }

    // The style guide's font: a picture of letters that matches its characters.
    if (!settings.font.empty()) {
        const engine::assets::AssetRecord* record = assets.find(settings.font);
        const std::optional<std::vector<std::uint8_t>> bytes =
            record != nullptr ? assets.read(record->guid) : std::nullopt;
        std::string problem;
        bool usable = false;
        if (!bytes.has_value()) {
            problem = "it is not a picture in assets/";
        } else if (const std::optional<engine::assets::Image> strip =
                       engine::assets::decode_png(bytes.value(), problem);
                   strip.has_value()) {
            usable = engine::ui::PixelFont::from_strip(strip.value().rgba, strip.value().width,
                                                       strip.value().height,
                                                       settings.font_characters, problem)
                         .has_value();
        }
        if (!usable) {
            Finding f;
            f.severity = Finding::Severity::Warning;
            f.message = "The game's font " + settings.font + " cannot be used (" + problem +
                        "), so the built-in font is shown. Choose another in Project > Style "
                        "guide.";
            f.asset = settings.font;
            found.push_back(std::move(f));
        }
    }

    // The style guide: pictures that use colours outside the project's palette.
    if (const std::vector<sprite::Color> palette = sprite::parse_palette(settings.palette);
        !palette.empty()) {
        check_palette(assets, palette, found);
    }

    // Every scene: the open one as it is now, the others as saved.
    for (const std::string& path : document.scenes()) {
        if (path == document.scene_path()) {
            check_scene(document.world(), path, assets, found, used);
            continue;
        }
        world::World scratch(document.registry());
        engine::scene2d::register_components(scratch);
        engine::game::register_behaviour_components(scratch);
        std::vector<serialize::Issue> issues;
        const std::optional<serialize::Json> json =
            serialize::parse(read_text(document.folder() / "assets" / path), issues);
        const serialize::SceneLoad loaded =
            json ? serialize::scene_from_json(*json, scratch) : serialize::SceneLoad{};
        if (!json || (loaded.entities.empty() && !loaded.issues.empty())) {
            Finding f;
            f.message = path + ": the scene cannot be read" +
                        (issues.empty() && loaded.issues.empty()
                             ? std::string(".")
                             : ": " + (issues.empty() ? loaded.issues.front().message
                                                      : issues.front().message));
            f.scene = path;
            found.push_back(std::move(f));
            continue;
        }
        check_scene(scratch, path, assets, found, used);
    }

    // Logic: every problem the compiler finds, on its block.
    std::string all_logic;
    for (const LogicFile& file : document.logic()) {
        for (const engine::logic::Diagnostic& d : document.check_logic(file.path)) {
            Finding f;
            f.message = file.path + ": " + d.message;
            f.logic = file.path;
            f.block = d.block;
            found.push_back(std::move(f));
        }
        all_logic += serialize::to_text(serialize::logic_to_json(file.asset));
    }
    for (const serialize::Issue& issue : document.logic_issues()) {
        Finding f;
        f.message = issue.path + ": " + issue.message;
        f.logic = issue.path;
        found.push_back(std::move(f));
    }

    // Conversations: ways that lead nowhere, and logic naming conversations that are gone.
    for (const engine::assets::AssetRecord& record : assets.assets()) {
        if (!record.path.ends_with(".dtalk")) {
            continue;
        }
        std::string error;
        const std::optional<engine::game::Conversation> talk = engine::game::parse_conversation(
            read_text(document.folder() / "assets" / record.path), error);
        const std::vector<std::string> problems =
            talk ? engine::game::check_conversation(*talk) : std::vector<std::string>{error};
        for (const std::string& problem : problems) {
            Finding f;
            f.severity = talk ? Finding::Severity::Warning : Finding::Severity::Problem;
            f.message = record.path + ": " + problem;
            f.asset = record.path;
            found.push_back(std::move(f));
        }
    }

    // Tables: cells that do not fit their columns, formulas that do not work out.
    for (const engine::assets::AssetRecord& record : assets.assets()) {
        if (!record.path.ends_with(".dtable")) {
            continue;
        }
        std::string error;
        const std::optional<engine::game::Table> table =
            engine::game::parse_table(read_text(document.folder() / "assets" / record.path), error);
        const std::vector<std::string> problems =
            table ? engine::game::check_table(*table) : std::vector<std::string>{error};
        for (const std::string& problem : problems) {
            Finding f;
            f.severity = table ? Finding::Severity::Warning : Finding::Severity::Problem;
            f.message = record.path + ": " + problem;
            f.asset = record.path;
            found.push_back(std::move(f));
        }
    }

    // Scenes that nothing leads to: not the start, and no logic or door names them.
    for (const std::string& path : document.scenes()) {
        if (path == settings.start_scene || all_logic.find(path) != std::string::npos) {
            continue;
        }
        Finding f;
        f.severity = Finding::Severity::Warning;
        f.message = path + " cannot be reached: the game does not start there, and no logic goes "
                           "there. Drag it into another scene to make a door to it.";
        f.scene = path;
        found.push_back(std::move(f));
    }

    // Pictures and sounds nothing uses (logic names some by their path or GUID).
    for (const engine::assets::AssetRecord& record : assets.assets()) {
        const bool media = record.path.ends_with(".png") || record.path.ends_with(".wav") ||
                           record.path.ends_with(".ogg") || record.path.ends_with(".mp3") ||
                           record.path.ends_with(".flac");
        if (!media || used.contains(record.guid) ||
            all_logic.find(record.guid.to_string()) != std::string::npos ||
            all_logic.find(record.path) != std::string::npos) {
            continue;
        }
        Finding f;
        f.severity = Finding::Severity::Note;
        f.message = record.path + " is not used by any scene or logic. It still goes into "
                                  "exported games; delete it if it is not needed.";
        f.asset = record.path;
        found.push_back(std::move(f));
    }

    std::stable_sort(found.begin(), found.end(), [](const Finding& a, const Finding& b) {
        return static_cast<int>(a.severity) < static_cast<int>(b.severity);
    });
    return found;
}

} // namespace editor
