#include "editor/core/new_project.h"

#include "engine/assets/assets.h"
#include "engine/game/conversation.h"
#include "engine/game/runtime.h"
#include "engine/game/tables.h"
#include "engine/logic/ir.h"
#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace editor {

namespace fs = std::filesystem;
namespace assets = engine::assets;
namespace scene2d = engine::scene2d;
namespace serialize = engine::serialize;
using engine::Guid;

namespace {

constexpr std::uint32_t tile = 16;
constexpr std::uint32_t map_width = 20;
constexpr std::uint32_t map_height = 12;

using Rgb = std::array<std::uint8_t, 3>;

void put(assets::Image& image, std::uint32_t x, std::uint32_t y, Rgb colour) {
    const std::size_t at = ((std::size_t{y} * image.width) + x) * 4;
    image.rgba[at] = colour[0];
    image.rgba[at + 1] = colour[1];
    image.rgba[at + 2] = colour[2];
    image.rgba[at + 3] = 255;
}

/// Three tiles side by side: grass, path and stone, each with a little texture.
assets::Image tileset() {
    assets::Image image;
    image.width = tile * 3;
    image.height = tile;
    image.rgba.assign(std::size_t{image.width} * image.height * 4, 0);
    constexpr std::array<std::array<Rgb, 2>, 3> colours{{
        {Rgb{72, 148, 76}, Rgb{60, 130, 66}},    // grass
        {Rgb{196, 168, 112}, Rgb{178, 150, 98}}, // path
        {Rgb{120, 122, 132}, Rgb{96, 98, 108}},  // stone
    }};
    for (std::uint32_t t = 0; t < 3; ++t) {
        for (std::uint32_t y = 0; y < tile; ++y) {
            for (std::uint32_t x = 0; x < tile; ++x) {
                // Stone: blocks with dark joints. Grass and path: scattered darker flecks.
                const bool dark = t == 2 ? (y % 8 == 7 || (x + (y / 8) * 4) % 8 == 7)
                                         : ((x * 7 + y * 13 + t * 5) % 11 == 0);
                put(image, (t * tile) + x, y, colours.at(t).at(dark ? 1 : 0));
            }
        }
    }
    return image;
}

/// A small figure facing the camera: hair, face, tunic and boots, on a clear background.
assets::Image hero() {
    assets::Image image;
    image.width = tile;
    image.height = tile;
    image.rgba.assign(std::size_t{tile} * tile * 4, 0);
    constexpr Rgb hair{84, 52, 36};
    constexpr Rgb skin{238, 196, 160};
    constexpr Rgb tunic{52, 96, 180};
    constexpr Rgb boots{60, 44, 36};
    const auto block = [&image](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                std::uint32_t y1, Rgb colour) {
        for (std::uint32_t y = y0; y <= y1; ++y) {
            for (std::uint32_t x = x0; x <= x1; ++x) {
                put(image, x, y, colour);
            }
        }
    };
    block(5, 1, 10, 3, hair);
    block(5, 4, 10, 7, skin);
    block(6, 5, 6, 5, hair); // eyes
    block(9, 5, 9, 5, hair);
    block(4, 8, 11, 12, tunic);
    block(5, 13, 6, 15, boots);
    block(9, 13, 10, 15, boots);
    return image;
}

/// The hero's figure in other colours: a villager.
assets::Image figure(Rgb hair, Rgb tunic) {
    assets::Image image = hero();
    for (std::size_t at = 0; at + 3 < image.rgba.size(); at += 4) {
        const Rgb now{image.rgba[at], image.rgba[at + 1], image.rgba[at + 2]};
        const Rgb with = now == Rgb{84, 52, 36} ? hair : now == Rgb{52, 96, 180} ? tunic : now;
        std::copy(with.begin(), with.end(), image.rgba.begin() + static_cast<std::ptrdiff_t>(at));
    }
    return image;
}

/// A panel's frame, 8 x 8, for a 9-slice edge of 2: a light rim round a dark, slightly
/// see-through middle.
assets::Image panel_frame() {
    assets::Image image;
    image.width = 8;
    image.height = 8;
    image.rgba.reserve(std::size_t{8} * 8 * 4);
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            const std::uint32_t edge = std::min({x, y, 7 - x, 7 - y});
            const bool rim = edge == 0;
            const bool corner = (x == 0 || x == 7) && (y == 0 || y == 7);
            const std::uint8_t alpha = corner ? 0 : rim ? 255 : edge == 1 ? 255 : 210;
            const std::uint8_t shade = rim ? 230 : 18;
            image.rgba.insert(image.rgba.end(), {shade, static_cast<std::uint8_t>(rim ? 232 : 20),
                                                 static_cast<std::uint8_t>(rim ? 240 : 40), alpha});
        }
    }
    return image;
}

/// A green slime: a dome with a lighter shine and two eyes.
assets::Image slime() {
    assets::Image image;
    image.width = tile;
    image.height = tile;
    image.rgba.assign(std::size_t{tile} * tile * 4, 0);
    for (std::uint32_t y = 6; y < tile; ++y) {
        for (std::uint32_t x = 0; x < tile; ++x) {
            const double dx = (static_cast<double>(x) + 0.5 - 8.0) / 7.5;
            const double dy = (static_cast<double>(y) + 0.5 - 15.0) / 9.0;
            if ((dx * dx) + (dy * dy) <= 1.0) {
                put(image, x, y,
                    (x == 5 || x == 6) && y == 8 ? Rgb{170, 240, 160} : Rgb{70, 180, 80});
            }
        }
    }
    put(image, 6, 11, Rgb{20, 40, 20});
    put(image, 10, 11, Rgb{20, 40, 20});
    return image;
}

/// The platformer's level, seen from the side: grass over stone along the bottom two rows,
/// stone walls at both ends, and three stone ledges rising to the right, each 2 tiles
/// above the last (a jump reaches 3).
std::string side_tiles() {
    scene2d::TileGrid grid;
    grid.width = map_width;
    grid.height = map_height;
    grid.cells.assign(std::size_t{map_width} * map_height, -1);
    const auto set = [&grid](std::uint32_t x0, std::uint32_t x1, std::uint32_t y, std::int32_t t) {
        for (std::uint32_t x = x0; x <= x1; ++x) {
            grid.cells[(std::size_t{y} * map_width) + x] = t;
        }
    };
    set(0, map_width - 1, map_height - 2, 0); // grass
    set(0, map_width - 1, map_height - 1, 2); // stone under it
    for (std::uint32_t y = 0; y + 1 < map_height; ++y) {
        set(0, 0, y, 2); // the walls
        set(map_width - 1, map_width - 1, y, 2);
    }
    set(6, 9, 8, 2); // the ledges
    set(11, 14, 6, 2);
    set(15, 18, 4, 2);
    return scene2d::format_tiles(grid);
}

/// A gold coin standing on the bottom of its tile, lit from the top left.
assets::Image coin() {
    assets::Image image;
    image.width = tile;
    image.height = tile;
    image.rgba.assign(std::size_t{tile} * tile * 4, 0);
    constexpr Rgb gold{240, 196, 64};
    constexpr Rgb shade{196, 144, 36};
    constexpr Rgb shine{255, 240, 160};
    // A disc of radius 5 around (8, 10), its lower right edge darker, a glint near the top.
    for (std::uint32_t y = 0; y < tile; ++y) {
        for (std::uint32_t x = 0; x < tile; ++x) {
            const int dx = static_cast<int>(x) - 8;
            const int dy = static_cast<int>(y) - 10;
            const int distance = (dx * dx) + (dy * dy);
            if (distance <= 25) {
                put(image, x, y, distance >= 13 && dx + dy > 0 ? shade : gold);
            }
        }
    }
    put(image, 6, 7, shine);
    put(image, 7, 7, shine);
    put(image, 6, 8, shine);
    return image;
}

/// Where the platformer's coins are: one on each ledge.
constexpr std::array<std::array<double, 2>, 3> coin_places{{{7.5, 8.0}, {13.0, 6.0}, {17.0, 4.0}}};

/// The platformer's logic: walking into a coin gives the player one and takes it away
/// (hidden, its area off); with all three, a message says so.
engine::serialize::Json coin_rules() {
    namespace logic = engine::logic;
    namespace ir = engine::logic::ir;
    using engine::reflect::Value;
    logic::LogicAsset coins;
    coins.name = "coins";
    std::uint64_t block = 1;
    for (std::size_t i = 1; i <= coin_places.size(); ++i) {
        const std::string name = "coin_" + std::to_string(i);
        logic::Rule rule;
        rule.id = logic::BlockId{block++};
        rule.object = name;
        rule.event = "entered";
        rule.actions.push_back(ir::do_action(
            block++, "inventory", "give",
            {ir::literal(Value{std::string("coin")}), ir::literal(Value{std::int64_t{1}})}));
        rule.actions.push_back(ir::set(block++, name, "visible", ir::literal(Value{false})));
        rule.actions.push_back(ir::set(block++, name, "enabled", ir::literal(Value{false})));
        const std::uint64_t test = block++;
        rule.actions.push_back(ir::if_then(
            test,
            ir::call("inventory", "has_at_least",
                     {ir::literal(Value{std::string("coin")}),
                      ir::literal(Value{static_cast<std::int64_t>(coin_places.size())})}),
            {ir::do_action(block++, "dialogue", "say",
                           {ir::literal(Value{std::string("You found all the coins!")})})},
            {}));
        coins.rules.push_back(std::move(rule));
    }
    return serialize::logic_to_json(coins);
}

/// The visual novel's backdrop, the size of the screen: a sky growing paler towards the
/// horizon, green hills, and a meadow.
assets::Image backdrop() {
    assets::Image image;
    image.width = 320;
    image.height = 180;
    image.rgba.assign(std::size_t{image.width} * image.height * 4, 0);
    constexpr std::array<Rgb, 4> sky{{
        {112, 160, 224}, // high
        {136, 180, 232},
        {164, 200, 238},
        {196, 220, 242}, // at the horizon
    }};
    constexpr Rgb hills{88, 140, 104};
    constexpr Rgb meadow{116, 176, 96};
    constexpr Rgb flowers{236, 220, 120};
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            // The hills' top: a gentle rise and fall between rows 92 and 116.
            const std::uint32_t ridge =
                104 + ((x / 4) % 24 < 12 ? (x / 4) % 12 : 11 - (x / 4) % 12);
            Rgb colour = sky.at(std::min<std::uint32_t>(y / 30, 3));
            if (y >= 128) {
                colour = (x * 7 + y * 11) % 53 == 0 ? flowers : meadow;
            } else if (y >= ridge - 12) {
                colour = hills;
            }
            put(image, x, y, colour);
        }
    }
    return image;
}

/// Mira, the visual novel's character: 2 tiles wide and 4 tall, standing, on a clear
/// background.
assets::Image mira() {
    assets::Image image;
    image.width = tile * 2;
    image.height = tile * 4;
    image.rgba.assign(std::size_t{image.width} * image.height * 4, 0);
    constexpr Rgb hair{150, 64, 52};
    constexpr Rgb skin{240, 202, 170};
    constexpr Rgb eyes{48, 40, 60};
    constexpr Rgb dress{124, 84, 168};
    constexpr Rgb trim{236, 228, 214};
    constexpr Rgb shoes{70, 50, 44};
    const auto block = [&image](std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                                std::uint32_t y1, Rgb colour) {
        for (std::uint32_t y = y0; y <= y1; ++y) {
            for (std::uint32_t x = x0; x <= x1; ++x) {
                put(image, x, y, colour);
            }
        }
    };
    block(9, 2, 22, 7, hair);
    block(8, 8, 23, 18, hair); // hair falling to the shoulders
    block(11, 8, 20, 17, skin);
    block(13, 11, 14, 12, eyes);
    block(17, 11, 18, 12, eyes);
    block(14, 18, 17, 20, skin); // neck
    block(9, 21, 22, 30, dress);
    block(10, 21, 21, 22, trim); // collar
    block(6, 23, 8, 36, dress);  // sleeves
    block(23, 23, 25, 36, dress);
    block(6, 37, 8, 39, skin); // hands
    block(23, 37, 25, 39, skin);
    block(8, 31, 23, 56, dress); // skirt
    block(8, 55, 23, 56, trim);
    block(11, 57, 13, 60, skin);
    block(18, 57, 20, 60, skin);
    block(10, 61, 14, 63, shoes);
    block(17, 61, 21, 63, shoes);
    return image;
}

/// A step that says `text`, as the Logic view writes it.
engine::serialize::Json say(int block, std::string_view text) {
    using engine::serialize::Json;
    return Json{{"block", block},
                {"kind", "do"},
                {"member", "say"},
                {"object", "dialogue"},
                {"values", Json::array({Json{{"kind", "literal"}, {"value", std::string(text)}}})}};
}

/// The visual novel's story: Mira greets the player and asks a question; her reply
/// depends on the answer.
engine::serialize::Json story() {
    using engine::serialize::Json;
    const auto literal = [](Json value) { return Json{{"kind", "literal"}, {"value", value}}; };
    Json ask{{"block", 3},
             {"kind", "do"},
             {"member", "ask"},
             {"object", "dialogue"},
             {"values", Json::array({literal("Mira: Will you walk with me to the lake?"),
                                     literal("Yes, let's go|Not today")})}};
    Json wait{
        {"block", 4},
        {"kind", "wait_until"},
        {"values", Json::array({Json{{"kind", "not"},
                                     {"operands", Json::array({Json{{"kind", "call"},
                                                                    {"member", "is_open"},
                                                                    {"object", "dialogue"}}})}}})}};
    Json reply{
        {"block", 5},
        {"kind", "if"},
        {"values",
         Json::array({Json{
             {"kind", "compare"},
             {"op", "equal"},
             {"operands",
              Json::array({Json{{"kind", "call"}, {"member", "answer"}, {"object", "dialogue"}},
                           literal(1)})}}})},
        {"body", Json::array({say(6, "Mira: Wonderful! I know a shortcut through the meadow.")})},
        {"otherwise", Json::array({say(7, "Mira: Another time, then. I'll be here.")})}};
    Json rule{{"block", 1},
              {"event", "game_start"},
              {"do", Json::array({say(2, "Mira: Oh! You came after all."), std::move(ask),
                                  std::move(wait), std::move(reply)})}};
    return Json{{"format", "sparxel.logic"},
                {"version", 1},
                {"name", "story"},
                {"rules", Json::array({std::move(rule)})}};
}

bool write(const fs::path& path, const char* bytes, std::size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes, static_cast<std::streamsize>(size));
    return out.good();
}

bool write_text(const fs::path& path, const std::string& text) {
    return write(path, text.data(), text.size());
}

bool write_png(const fs::path& path, const assets::Image& image) {
    const std::vector<std::uint8_t> bytes = assets::encode_png(image);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes as chars to write
    return !bytes.empty() && write(path, reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

/// A map of `fill`, with `row` (if any) of `line` instead; or only a border of `fill`.
std::string tiles(std::int32_t fill, bool border_only, std::int32_t line_row, std::int32_t line) {
    scene2d::TileGrid grid;
    grid.width = map_width;
    grid.height = map_height;
    grid.cells.assign(std::size_t{map_width} * map_height, border_only ? -1 : fill);
    for (std::uint32_t y = 0; y < map_height; ++y) {
        for (std::uint32_t x = 0; x < map_width; ++x) {
            std::int32_t& cell = grid.cells[(std::size_t{y} * map_width) + x];
            if (border_only && (x == 0 || y == 0 || x + 1 == map_width || y + 1 == map_height)) {
                cell = fill;
            }
            if (line_row >= 0 && y == static_cast<std::uint32_t>(line_row)) {
                cell = line;
            }
        }
    }
    return scene2d::format_tiles(grid);
}

/// A scene's entity, with a new GUID.
serialize::Json entity(std::string_view entity_name, serialize::Json components) {
    return serialize::Json{{"guid", Guid::generate().to_string()},
                           {"name", std::string(entity_name)},
                           {"components", std::move(components)}};
}

/// create_project() for a platformer, into its folders, made already, with its pictures.
bool create_platformer(const fs::path& folder, std::string_view name, std::string& error,
                       const std::string& tileset_id, const std::string& hero_id) {
    // Its coins: their picture and their logic.
    const fs::path assets_folder = folder / "assets";
    std::error_code failed;
    fs::create_directories(assets_folder / "logic", failed);
    if (failed || !write_png(assets_folder / "art" / "coin.png", coin()) ||
        !write_text(assets_folder / "logic" / "coins.drules", serialize::to_text(coin_rules()))) {
        error = "The project's folder could not be written to.";
        return false;
    }
    assets::AssetDatabase database(assets_folder);
    database.refresh();
    const assets::AssetRecord* coin_png = database.find("art/coin.png");
    if (coin_png == nullptr) {
        error = "The project's pictures could not be read back.";
        return false;
    }
    serialize::Json entities = serialize::Json::array();
    entities.push_back(entity(
        "level", {{"tilemap", {{"tiles", side_tiles()}, {"tileset", tileset_id}, {"solid", true}}},
                  {"transform_2d", {{"layer", -1}, {"x", 0.0}, {"y", 0.0}}}}));
    // Standing on the grass, as tall as its hitbox, walking and jumping.
    entities.push_back(entity("hero", {{"sprite", {{"image", hero_id}}},
                                       {"top_down_mover", {{"speed", 5.0}, {"hitbox_height", 1.0}}},
                                       {"jumping", serialize::Json::object()},
                                       {"transform_2d", {{"x", 3.0}, {"y", 10.0}}}}));
    // A coin on each ledge, picked up by walking into it.
    for (std::size_t i = 0; i < coin_places.size(); ++i) {
        entities.push_back(
            entity("coin_" + std::to_string(i + 1),
                   {{"sprite", {{"image", coin_png->guid.to_string()}}},
                    {"trigger", serialize::Json::object()},
                    {"transform_2d", {{"x", coin_places.at(i)[0]}, {"y", coin_places.at(i)[1]}}}}));
    }
    const serialize::Json scene{
        {"format", "sparxel.scene"}, {"version", 1}, {"entities", std::move(entities)}};
    engine::game::ProjectSettings settings;
    settings.name = std::string(name);
    settings.start_scene = "maps/start.dscene";
    settings.background = engine::render::Color{132, 188, 236, 255}; // a clear sky
    if (!write_text(folder / "assets" / "maps" / "start.dscene", serialize::to_text(scene)) ||
        !write_text(folder / "project.sparxel.json",
                    serialize::to_text(engine::game::project_to_json(settings)))) {
        error = "The project's files could not be written.";
        return false;
    }
    database.refresh(); // the scene's ".meta"
    return true;
}

/// create_project() for a visual novel, into its folders, made already.
bool create_visual_novel(const fs::path& folder, std::string_view name, std::string& error) {
    const fs::path assets_folder = folder / "assets";
    std::error_code failed;
    fs::create_directories(assets_folder / "logic", failed);
    if (failed || !write_png(assets_folder / "art" / "backdrop.png", backdrop()) ||
        !write_png(assets_folder / "art" / "mira.png", mira()) ||
        !write_text(assets_folder / "logic" / "story.drules", serialize::to_text(story()))) {
        error = "The project's folder could not be written to.";
        return false;
    }
    assets::AssetDatabase database(assets_folder);
    database.refresh();
    const assets::AssetRecord* backdrop_png = database.find("art/backdrop.png");
    const assets::AssetRecord* mira_png = database.find("art/mira.png");
    if (backdrop_png == nullptr || mira_png == nullptr) {
        error = "The project's pictures could not be read back.";
        return false;
    }
    // With no one to follow, the camera looks at the map's origin: the backdrop's middle.
    serialize::Json entities = serialize::Json::array();
    entities.push_back(
        entity("backdrop",
               {{"sprite",
                 {{"image", backdrop_png->guid.to_string()}, {"pivot_x", 0.5}, {"pivot_y", 0.5}}},
                {"transform_2d", {{"layer", -1}, {"x", 0.0}, {"y", 0.0}}}}));
    // Standing right of the middle, her feet above the message box.
    entities.push_back(entity("mira", {{"sprite", {{"image", mira_png->guid.to_string()}}},
                                       {"transform_2d", {{"x", 4.5}, {"y", 2.0}}}}));
    const serialize::Json scene{
        {"format", "sparxel.scene"}, {"version", 1}, {"entities", std::move(entities)}};
    engine::game::ProjectSettings settings;
    settings.name = std::string(name);
    settings.start_scene = "maps/start.dscene";
    if (!write_text(assets_folder / "maps" / "start.dscene", serialize::to_text(scene)) ||
        !write_text(folder / "project.sparxel.json",
                    serialize::to_text(engine::game::project_to_json(settings)))) {
        error = "The project's files could not be written.";
        return false;
    }
    database.refresh(); // the scene's and the story's ".meta"
    return true;
}

/// The RPG template's data, logic and conversation, written into `assets_folder`.
bool write_rpg_files(const fs::path& assets_folder) {
    namespace game = engine::game;
    namespace logic = engine::logic;
    namespace ir = engine::logic::ir;
    using engine::reflect::Value;
    const auto table = [](std::vector<game::TableColumn> columns,
                          std::vector<game::TableRow> rows) {
        game::Table t;
        t.columns = std::move(columns);
        t.rows = std::move(rows);
        return game::table_to_text(t);
    };
    using K = game::ColumnKind;
    const std::string party = table({{"name", K::Text},
                                     {"max_hp", K::Whole},
                                     {"max_mp", K::Whole},
                                     {"attack", K::Whole},
                                     {"defense", K::Whole},
                                     {"speed", K::Whole},
                                     {"skills", K::Text},
                                     {"max_hp_growth", K::Whole},
                                     {"attack_growth", K::Whole}},
                                    {{"hero",
                                      {{"name", "Hero"},
                                       {"max_hp", "30"},
                                       {"max_mp", "6"},
                                       {"attack", "6"},
                                       {"defense", "2"},
                                       {"speed", "5"},
                                       {"skills", "heal@2"},
                                       {"max_hp_growth", "6"},
                                       {"attack_growth", "2"}}}});
    const std::string enemies = table({{"name", K::Text},
                                       {"max_hp", K::Whole},
                                       {"attack", K::Whole},
                                       {"defense", K::Whole},
                                       {"speed", K::Whole},
                                       {"xp", K::Whole},
                                       {"gold", K::Whole},
                                       {"image", K::Text}},
                                      {{"slime",
                                        {{"name", "Slime"},
                                         {"max_hp", "14"},
                                         {"attack", "4"},
                                         {"defense", "0"},
                                         {"speed", "3"},
                                         {"xp", "10"},
                                         {"gold", "8"},
                                         {"image", "art/slime.png"}}}});
    const std::string skills =
        table({{"name", K::Text}, {"mp_cost", K::Whole}, {"power", K::Whole}, {"heals", K::YesNo}},
              {{"heal", {{"name", "Heal"}, {"mp_cost", "3"}, {"power", "15"}, {"heals", "yes"}}}});
    const std::string items = table(
        {{"name", K::Text},
         {"description", K::Text},
         {"heal", K::Whole},
         {"price", K::Whole},
         {"slot", K::Text},
         {"attack", K::Whole}},
        {{"potion",
          {{"name", "Potion"}, {"description", "Heals 15 HP."}, {"heal", "15"}, {"price", "10"}}},
         {"sword",
          {{"name", "Bronze sword"},
           {"description", "Attack +3."},
           {"price", "30"},
           {"slot", "weapon"},
           {"attack", "3"}}}});

    // The elder's conversation: a greeting, a question, and an answer to each choice.
    game::Conversation talk;
    talk.start = "start";
    talk.speakers.push_back(game::Speaker{"Elder", "#ffd27a"});
    game::TalkNode start;
    start.id = "start";
    start.speaker = "Elder";
    start.text = "Welcome, traveller. A slime has been bothering the village.";
    start.next = "ask";
    start.x = 0.0;
    game::TalkNode ask;
    ask.id = "ask";
    ask.speaker = "Elder";
    ask.text = "Will you deal with it?";
    ask.choices = {{"Yes", "yes", {}}, {"Not yet", "later", {}}};
    ask.x = 260.0;
    game::TalkNode yes;
    yes.id = "yes";
    yes.speaker = "Elder";
    yes.text = "Bless you! Take this potion. The shop to the east sells more.";
    yes.effects.push_back({game::TalkEffect::Kind::GiveItem, "potion", "1"});
    yes.effects.push_back({game::TalkEffect::Kind::StartQuest, "slime", "The village slime"});
    yes.x = 520.0;
    game::TalkNode later;
    later.id = "later";
    later.speaker = "Elder";
    later.text = "Come back when you are ready.";
    later.x = 520.0;
    later.y = 140.0;
    talk.nodes = {start, ask, yes, later};

    logic::LogicAsset rules;
    rules.name = "village";
    std::uint64_t block = 1;
    const auto text = [](const char* words) { return ir::literal(Value{std::string(words)}); };
    rules.rules.push_back(
        logic::Rule{logic::BlockId{block++},
                    "",
                    "game_start",
                    {},
                    {ir::do_action(block++, "party", "add_member", {text("hero")}),
                     ir::do_action(block++, "inventory", "give",
                                   {text("gold"), ir::literal(Value{std::int64_t{20}})})},
                    {}});
    block += 2;
    rules.rules.push_back(logic::Rule{
        logic::BlockId{block++},
        "elder",
        "interacted",
        {},
        {ir::do_action(block++, "dialogue", "start_conversation", {text("talk/elder.dtalk")})},
        {}});
    rules.rules.push_back(
        logic::Rule{logic::BlockId{block++},
                    "shopkeeper",
                    "interacted",
                    {},
                    {ir::do_action(block++, "inventory", "open_shop", {text("potion, sword")})},
                    {}});
    rules.rules.push_back(
        logic::Rule{logic::BlockId{block++},
                    "slime",
                    "defeated",
                    {},
                    {ir::do_action(block++, "quests", "complete", {text("slime")}),
                     ir::do_action(block++, "dialogue", "say",
                                   {text("The slime is gone! The elder will be glad.")})},
                    {}});
    std::error_code failed;
    for (const char* sub : {"data", "talk", "logic"}) {
        fs::create_directories(assets_folder / sub, failed);
    }
    return !failed && write_text(assets_folder / "data" / "party.dtable", party) &&
           write_text(assets_folder / "data" / "enemies.dtable", enemies) &&
           write_text(assets_folder / "data" / "skills.dtable", skills) &&
           write_text(assets_folder / "data" / "items.dtable", items) &&
           write_text(assets_folder / "talk" / "elder.dtalk", game::conversation_to_text(talk)) &&
           write_text(assets_folder / "logic" / "village.drules",
                      serialize::to_text(serialize::logic_to_json(rules))) &&
           write_text(assets_folder / "credits.txt", "A Sparxel RPG\n\nMade with Sparxel\n") &&
           write_png(assets_folder / "art" / "elder.png",
                     figure({220, 220, 220}, {120, 60, 140})) &&
           write_png(assets_folder / "art" / "shopkeeper.png",
                     figure({40, 30, 20}, {190, 120, 40})) &&
           write_png(assets_folder / "art" / "slime.png", slime()) &&
           write_png(assets_folder / "art" / "panel.png", panel_frame());
}

} // namespace

bool create_project(const fs::path& folder, std::string_view name, std::string& error,
                    ProjectTemplate kind) {
    if (name.empty()) {
        error = "Give the game a name.";
        return false;
    }
    std::error_code failed;
    if (fs::exists(folder, failed) &&
        (!fs::is_directory(folder, failed) ||
         fs::directory_iterator(folder, failed) != fs::directory_iterator())) {
        error = "That folder already has something in it: choose an empty or a new one.";
        return false;
    }
    const fs::path art = folder / "assets" / "art";
    const fs::path maps = folder / "assets" / "maps";
    fs::create_directories(art, failed);
    if (!failed) {
        fs::create_directories(maps, failed);
    }
    if (kind == ProjectTemplate::VisualNovel && !failed) {
        return create_visual_novel(folder, name, error);
    }
    if (failed || !write_png(art / "tiles.png", tileset()) ||
        !write_png(art / "hero.png", hero())) {
        error = "The project's folder could not be written to.";
        return false;
    }
    // The pictures get their GUIDs (in ".meta" files), which the scene refers to.
    assets::AssetDatabase database(folder / "assets");
    database.refresh();
    const assets::AssetRecord* tiles_png = database.find("art/tiles.png");
    const assets::AssetRecord* hero_png = database.find("art/hero.png");
    if (tiles_png == nullptr || hero_png == nullptr) {
        error = "The project's pictures could not be read back.";
        return false;
    }
    // Kept as text: refreshing the database below replaces its records.
    const std::string tileset_id = tiles_png->guid.to_string();
    const std::string hero_id = hero_png->guid.to_string();
    if (kind == ProjectTemplate::Platformer) {
        return create_platformer(folder, name, error, tileset_id, hero_id);
    }
    const bool rpg = kind == ProjectTemplate::Rpg;
    if (rpg && !write_rpg_files(folder / "assets")) {
        error = "The project's folder could not be written to.";
        return false;
    }
    database.refresh();
    const auto picture = [&database](const char* path) {
        const assets::AssetRecord* record = database.find(path);
        return record != nullptr ? record->guid.to_string() : std::string();
    };
    // The ground under the walls, both under the hero.
    const serialize::Json under{{"layer", -2}, {"x", 0.0}, {"y", 0.0}};
    const serialize::Json flat{{"layer", -1}, {"x", 0.0}, {"y", 0.0}};
    serialize::Json entities = serialize::Json::array();
    entities.push_back(
        entity("ground", {{"tilemap", {{"tiles", tiles(0, false, 6, 1)}, {"tileset", tileset_id}}},
                          {"transform_2d", under}}));
    entities.push_back(entity("walls", {{"tilemap",
                                         {{"tiles", tiles(2, true, -1, 0)},
                                          {"tileset", tileset_id},
                                          {"solid", true},
                                          {"height", 1.0}}},
                                        {"transform_2d", flat}}));
    entities.push_back(entity("hero", {{"sprite", {{"image", hero_id}}},
                                       {"top_down_mover", {{"speed", 4.0}}},
                                       {"transform_2d", {{"x", 10.0}, {"y", 6.0}}}}));
    if (rpg) {
        // The village: an elder to talk to, a shopkeeper, and a slime on the road.
        entities.push_back(entity("elder", {{"sprite", {{"image", picture("art/elder.png")}}},
                                            {"interactable", serialize::Json::object()},
                                            {"body", serialize::Json::object()},
                                            {"transform_2d", {{"x", 5.5}, {"y", 4.0}}}}));
        entities.push_back(
            entity("shopkeeper", {{"sprite", {{"image", picture("art/shopkeeper.png")}}},
                                  {"interactable", serialize::Json::object()},
                                  {"body", serialize::Json::object()},
                                  {"transform_2d", {{"x", 15.5}, {"y", 4.0}}}}));
        entities.push_back(entity("slime", {{"sprite", {{"image", picture("art/slime.png")}}},
                                            {"enemy_on_map", {{"enemies", "slime"}}},
                                            {"transform_2d", {{"x", 10.5}, {"y", 9.0}}}}));
        // The gold carried, on a framed panel stretched from an 8-pixel picture (9-slice).
        entities.push_back(entity("gold_panel", {{"screen_picture",
                                                  {{"image", picture("art/panel.png")},
                                                   {"anchor", "top_right"},
                                                   {"x", 1.0},
                                                   {"y", 1.0},
                                                   {"width", 60.0},
                                                   {"height", 14.0},
                                                   {"border", 2.0}}}}));
        entities.push_back(
            entity("gold", {{"screen_text", {{"text", "Gold {gold}"}, {"anchor", "top_right"}}}}));
        entities.push_back(
            entity("health", {{"screen_text", {{"text", "HP {hp:hero}/{max_hp:hero}"}}}}));
    }
    const serialize::Json scene{
        {"format", "sparxel.scene"}, {"version", 1}, {"entities", std::move(entities)}};
    engine::game::ProjectSettings settings;
    settings.name = std::string(name);
    settings.start_scene = "maps/start.dscene";
    if (kind == ProjectTemplate::Diorama) {
        settings.style = engine::game::RenderStyle::Diorama;
        settings.look.tilt_shift = true;
    }
    if (rpg) {
        // Its boxes in the same frame as the gold's panel.
        settings.box_frame = "art/panel.png";
        settings.box_frame_edge = 2.0;
        settings.title_screen = true;
        settings.pause_menu = true;
        settings.text_speed = 40.0;
    }
    if (!write_text(maps / "start.dscene", serialize::to_text(scene)) ||
        !write_text(folder / "project.sparxel.json",
                    serialize::to_text(engine::game::project_to_json(settings)))) {
        error = "The project's files could not be written.";
        return false;
    }
    database.refresh(); // the scene's ".meta"
    return true;
}

} // namespace editor
