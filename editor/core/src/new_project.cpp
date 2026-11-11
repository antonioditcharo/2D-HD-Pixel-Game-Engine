#include "editor/core/new_project.h"

#include "engine/assets/assets.h"
#include "engine/game/runtime.h"
#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <system_error>
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

} // namespace

bool create_project(const fs::path& folder, std::string_view name, std::string& error) {
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
    const std::string tileset_id = tiles_png->guid.to_string();
    const auto entity = [](std::string_view entity_name, serialize::Json components) {
        return serialize::Json{{"guid", Guid::generate().to_string()},
                               {"name", std::string(entity_name)},
                               {"components", std::move(components)}};
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
    entities.push_back(entity("hero", {{"sprite", {{"image", hero_png->guid.to_string()}}},
                                       {"top_down_mover", {{"speed", 4.0}}},
                                       {"transform_2d", {{"x", 10.0}, {"y", 6.0}}}}));
    const serialize::Json scene{
        {"format", "sparxel.scene"}, {"version", 1}, {"entities", std::move(entities)}};
    engine::game::ProjectSettings settings;
    settings.name = std::string(name);
    settings.start_scene = "maps/start.dscene";
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
