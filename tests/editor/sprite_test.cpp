// Sprite Studio's model: drawing tools, layers, frames, tags, the palette, undo, and the
// .dsprite file with the sprite sheet beside it.

#include "engine/assets/assets.h"
#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "editor/core/sfx.h"
#include "editor/core/sprite.h"

namespace fs = std::filesystem;
namespace sprite = editor::sprite;
using sprite::Color;
using sprite::Tool;

namespace {

constexpr Color red{255, 0, 0, 255};
constexpr Color blue{0, 0, 255, 255};
constexpr Color clear{};

/// How many pixels of the current layer and frame are not transparent.
int drawn(const sprite::Editor& editor) {
    int count = 0;
    for (int y = 0; y < editor.sprite().height; ++y) {
        for (int x = 0; x < editor.sprite().width; ++x) {
            count += editor.sprite().pixel(editor.layer(), editor.frame(), x, y).a != 0 ? 1 : 0;
        }
    }
    return count;
}

/// A drag of `tool` through `points`.
bool stroke(sprite::Editor& editor, Tool tool, Color color,
            const std::vector<std::array<int, 2>>& points, const sprite::Brush& brush = {}) {
    std::string error;
    REQUIRE(editor.begin(tool, color, brush, points.front()[0], points.front()[1], error));
    for (std::size_t i = 1; i < points.size(); ++i) {
        editor.drag(points[i][0], points[i][1]);
    }
    return editor.end();
}

struct Folder {
    Folder()
        : path(fs::temp_directory_path() /
               ("sparxel-sprite-" + engine::Guid::generate().to_string())) {
        fs::create_directories(path);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    Folder(Folder&&) = delete;
    Folder& operator=(Folder&&) = delete;
    ~Folder() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

} // namespace

TEST_CASE("A blank sprite has one empty layer and frame, and the default palette") {
    const sprite::Sprite s = sprite::Sprite::blank(16, 24);
    CHECK(s.width == 16);
    CHECK(s.height == 24);
    REQUIRE(s.layers.size() == 1);
    CHECK(s.layers[0].name == "Layer 1");
    CHECK(s.frames.size() == 1);
    CHECK(s.frames[0].duration_ms == 100);
    CHECK(s.palette.size() == 32);
    CHECK(s.palette.front() == Color{0, 0, 0, 255});
    CHECK(s.pixel(0, 0, 3, 3) == clear);
    // Sizes are kept within what a sprite can be.
    CHECK(sprite::Sprite::blank(0, 5000).width == 1);
    CHECK(sprite::Sprite::blank(0, 5000).height == sprite::max_side);
}

TEST_CASE("Colours are read and written as hex") {
    CHECK(sprite::to_hex(red) == "#ff0000");
    CHECK(sprite::to_hex(Color{1, 2, 3, 4}) == "#01020304");
    CHECK(sprite::from_hex("#ff0000") == red);
    CHECK(sprite::from_hex("f00") == red);
    CHECK(sprite::from_hex("#01020304") == Color{1, 2, 3, 4});
    CHECK_FALSE(sprite::from_hex("#12345").has_value());
    CHECK_FALSE(sprite::from_hex("#gg0000").has_value());
}

TEST_CASE("A pencil stroke is one undo step") {
    sprite::Editor editor(sprite::Sprite::blank(8, 8));
    CHECK_FALSE(editor.modified());
    CHECK(stroke(editor, Tool::Pencil, red, {{0, 0}, {3, 0}}));
    CHECK(drawn(editor) == 4);
    CHECK(editor.modified());
    CHECK(editor.undo_text() == "Draw with the pencil");
    CHECK(editor.undo());
    CHECK(drawn(editor) == 0);
    CHECK_FALSE(editor.modified());
    CHECK(editor.redo());
    CHECK(drawn(editor) == 4);
    editor.mark_saved();
    CHECK_FALSE(editor.modified());
    // The eraser clears what it passes over.
    CHECK(stroke(editor, Tool::Eraser, red, {{1, 0}, {2, 0}}));
    CHECK(drawn(editor) == 2);
    CHECK(editor.undo_text() == "Erase");
    // A stroke that changes nothing is no step.
    CHECK_FALSE(stroke(editor, Tool::Eraser, red, {{5, 5}}));
    CHECK(editor.undo_text() == "Erase");
}

TEST_CASE("Pixel-perfect strokes leave out the corners of L shapes") {
    const std::vector<std::array<int, 2>> stairs{{0, 0}, {1, 0}, {1, 1}, {2, 1}, {2, 2}};
    sprite::Editor perfect(sprite::Sprite::blank(4, 4));
    stroke(perfect, Tool::Pencil, red, stairs);
    CHECK(drawn(perfect) == 3);
    CHECK(perfect.sprite().pixel(0, 0, 1, 1) == red);
    CHECK(perfect.sprite().pixel(0, 0, 1, 0) == clear);

    sprite::Editor rough(sprite::Sprite::blank(4, 4));
    sprite::Brush brush;
    brush.pixel_perfect = false;
    stroke(rough, Tool::Pencil, red, stairs, brush);
    CHECK(drawn(rough) == 5);
}

TEST_CASE("Strokes join the points a fast drag skips") {
    sprite::Editor editor(sprite::Sprite::blank(8, 8));
    stroke(editor, Tool::Pencil, red, {{0, 4}, {7, 4}});
    CHECK(drawn(editor) == 8);
}

TEST_CASE("Lines, rectangles and ellipses are redrawn as they are dragged") {
    sprite::Editor editor(sprite::Sprite::blank(16, 16));
    std::string error;
    REQUIRE(editor.begin(Tool::Line, red, {}, 0, 0, error));
    editor.drag(15, 0);
    CHECK(drawn(editor) == 16);
    editor.drag(0, 5); // The line follows the cursor: only the last one is kept.
    CHECK(drawn(editor) == 6);
    CHECK(editor.end());
    CHECK(editor.undo_text() == "Draw a line");
    CHECK(editor.undo());

    stroke(editor, Tool::Rectangle, red, {{2, 2}, {5, 4}});
    CHECK(drawn(editor) == 10); // The edge of a 4 × 3 rectangle.
    CHECK(editor.undo());
    stroke(editor, Tool::FilledRectangle, red, {{5, 4}, {2, 2}});
    CHECK(drawn(editor) == 12);
    CHECK(editor.undo());

    stroke(editor, Tool::Ellipse, red, {{0, 0}, {10, 6}});
    CHECK(editor.sprite().pixel(0, 0, 5, 3) == clear); // The middle stays empty.
    CHECK(editor.sprite().pixel(0, 0, 0, 3) == red);   // Its left and right tips.
    CHECK(editor.sprite().pixel(0, 0, 10, 3) == red);
    CHECK(editor.sprite().pixel(0, 0, 5, 0) == red); // Its top and bottom.
    CHECK(editor.sprite().pixel(0, 0, 5, 6) == red);
    CHECK(editor.sprite().pixel(0, 0, 0, 0) == clear); // Not the corners.
    const int outline = drawn(editor);
    CHECK(editor.undo());
    stroke(editor, Tool::FilledEllipse, red, {{0, 0}, {10, 6}});
    CHECK(editor.sprite().pixel(0, 0, 5, 3) == red);
    CHECK(drawn(editor) > outline);
    CHECK(editor.sprite().pixel(0, 0, 0, 0) == clear);
}

TEST_CASE("Fill paints the joined pixels of a colour, or all of them") {
    sprite::Editor editor(sprite::Sprite::blank(9, 4));
    stroke(editor, Tool::Pencil, blue, {{4, 0}, {4, 3}}); // A wall down the middle.
    stroke(editor, Tool::Fill, red, {{0, 0}});
    CHECK(editor.undo_text() == "Fill");
    CHECK(editor.sprite().pixel(0, 0, 3, 3) == red);
    CHECK(editor.sprite().pixel(0, 0, 5, 0) == clear);
    CHECK(editor.sprite().pixel(0, 0, 4, 2) == blue);
    CHECK(editor.undo());

    sprite::Brush all;
    all.fill_all = true;
    stroke(editor, Tool::Fill, red, {{0, 0}}, all);
    CHECK(editor.sprite().pixel(0, 0, 8, 3) == red);
    CHECK(editor.sprite().pixel(0, 0, 4, 2) == blue);
    // Filling with the colour already there changes nothing.
    CHECK_FALSE(stroke(editor, Tool::Fill, red, {{0, 0}}));
}

TEST_CASE("Mirrored drawing paints both halves") {
    sprite::Editor editor(sprite::Sprite::blank(8, 8));
    sprite::Brush brush;
    brush.mirror_x = true;
    brush.mirror_y = true;
    stroke(editor, Tool::Pencil, red, {{1, 2}}, brush);
    CHECK(editor.sprite().pixel(0, 0, 1, 2) == red);
    CHECK(editor.sprite().pixel(0, 0, 6, 2) == red);
    CHECK(editor.sprite().pixel(0, 0, 1, 5) == red);
    CHECK(editor.sprite().pixel(0, 0, 6, 5) == red);
    CHECK(drawn(editor) == 4);
    // A bigger brush is a square around the point.
    sprite::Editor big(sprite::Sprite::blank(8, 8));
    sprite::Brush three;
    three.size = 3;
    stroke(big, Tool::Pencil, red, {{4, 4}}, three);
    CHECK(drawn(big) == 9);
}

TEST_CASE("Hidden and locked layers refuse drawing, saying why") {
    sprite::Editor editor(sprite::Sprite::blank(4, 4));
    std::string error;
    CHECK(editor.set_layer_visible(0, false));
    CHECK(editor.undo_text() == "Hide Layer 1");
    CHECK_FALSE(editor.begin(Tool::Pencil, red, {}, 0, 0, error));
    CHECK(error == "\"Layer 1\" is hidden. Show it to draw on it.");
    CHECK(editor.set_layer_visible(0, true));
    CHECK(editor.set_layer_locked(0, true));
    CHECK_FALSE(editor.begin(Tool::Fill, red, {}, 0, 0, error));
    CHECK(error == "\"Layer 1\" is locked. Unlock it to draw on it.");
    // Selecting still works.
    CHECK(editor.begin(Tool::Select, red, {}, 0, 0, error));
    editor.drag(1, 1);
    CHECK_FALSE(editor.end());
    CHECK(editor.selection() == sprite::Rect{0, 0, 2, 2});
}

TEST_CASE("A selection keeps drawing inside it, and moves its pixels") {
    sprite::Editor editor(sprite::Sprite::blank(8, 8));
    editor.select({2, 2, 3, 3});
    stroke(editor, Tool::Pencil, red, {{0, 3}, {7, 3}});
    CHECK(drawn(editor) == 3);
    CHECK(editor.sprite().pixel(0, 0, 1, 3) == clear);

    // Moving: the selected pixels go with the drag, and the selection with them.
    stroke(editor, Tool::Move, red, {{3, 3}, {4, 5}});
    CHECK(editor.undo_text() == "Move pixels");
    CHECK(editor.selection() == sprite::Rect{3, 4, 3, 3});
    CHECK(editor.sprite().pixel(0, 0, 2, 3) == clear);
    CHECK(editor.sprite().pixel(0, 0, 3, 5) == red);
    CHECK(editor.sprite().pixel(0, 0, 5, 5) == red);
    CHECK(drawn(editor) == 3);
    CHECK(editor.undo());
    CHECK(editor.selection() == sprite::Rect{2, 2, 3, 3});
    CHECK(editor.sprite().pixel(0, 0, 2, 3) == red);

    // Without a selection, Move takes the whole layer.
    editor.deselect();
    stroke(editor, Tool::Move, red, {{0, 0}, {0, -3}});
    CHECK(editor.sprite().pixel(0, 0, 2, 0) == red);

    // Clear, copy and paste.
    editor.select({2, 0, 1, 1});
    std::string error;
    editor.copy();
    CHECK(editor.clear(error));
    CHECK(editor.sprite().pixel(0, 0, 2, 0) == clear);
    editor.select({6, 6, 1, 1});
    CHECK(editor.paste(error));
    CHECK(editor.undo_text() == "Paste");
    CHECK(editor.sprite().pixel(0, 0, 6, 6) == red);
    CHECK(editor.selection() == sprite::Rect{6, 6, 1, 1});
}

TEST_CASE("Flipping mirrors the layer or the selection") {
    sprite::Editor editor(sprite::Sprite::blank(4, 2));
    stroke(editor, Tool::Pencil, red, {{0, 0}});
    std::string error;
    CHECK(editor.flip(true, error));
    CHECK(editor.sprite().pixel(0, 0, 3, 0) == red);
    CHECK(editor.sprite().pixel(0, 0, 0, 0) == clear);
    CHECK(editor.flip(false, error));
    CHECK(editor.sprite().pixel(0, 0, 3, 1) == red);
    CHECK(editor.undo_text() == "Flip vertically");
}

TEST_CASE("Layers are added, ordered, merged and blended") {
    sprite::Editor editor(sprite::Sprite::blank(2, 1));
    stroke(editor, Tool::Pencil, blue, {{0, 0}, {1, 0}});
    std::string error;
    CHECK(editor.add_layer(error));
    CHECK(editor.sprite().layers.size() == 2);
    CHECK(editor.layer() == 1);
    CHECK(editor.sprite().layers[1].name == "Layer 2");
    CHECK(editor.undo_text() == "Add Layer 2");
    stroke(editor, Tool::Pencil, red, {{0, 0}});

    // Opacity: dragging the slider is one step.
    CHECK(editor.set_layer_opacity(1, 200));
    CHECK(editor.set_layer_opacity(1, 128));
    CHECK(editor.undo_text() == "Change opacity of Layer 2");
    const Color half = editor.sprite().shown(0, 0, 0);
    CHECK(half.r == 128);
    CHECK(half.b == 127);
    CHECK(half.a == 255);
    CHECK(editor.sprite().shown(0, 1, 0) == blue);
    CHECK(editor.undo());
    CHECK(editor.sprite().layers[1].opacity == 255);
    editor.end_merge();

    CHECK(editor.rename_layer(1, "Eyes", error));
    CHECK_FALSE(editor.rename_layer(1, "", error));
    CHECK(editor.move_layer(false, error));
    CHECK(editor.layer() == 0);
    CHECK(editor.sprite().layers[0].name == "Eyes");
    CHECK(editor.sprite().shown(0, 0, 0) == blue); // Now below the blue.
    CHECK_FALSE(editor.move_layer(false, error));
    CHECK(error == "It is the bottom layer already.");
    CHECK(editor.move_layer(true, error));
    CHECK(editor.merge_down(error));
    CHECK(editor.sprite().layers.size() == 1);
    CHECK(editor.sprite().pixel(0, 0, 0, 0) == red);
    CHECK(editor.undo_text() == "Merge Eyes down");
    CHECK(editor.undo());
    CHECK(editor.sprite().layers.size() == 2);

    CHECK(editor.duplicate_layer(error));
    CHECK(editor.sprite().layers[2].name == "Eyes copy");
    CHECK(editor.sprite().pixel(2, 0, 0, 0) == red);
    CHECK(editor.remove_layer(error));
    CHECK(editor.remove_layer(error));
    CHECK_FALSE(editor.remove_layer(error));
    CHECK(error == "A sprite needs at least one layer.");
}

TEST_CASE("Frames are added, copied, moved and deleted, and tags follow them") {
    sprite::Editor editor(sprite::Sprite::blank(2, 2));
    stroke(editor, Tool::Pencil, red, {{0, 0}});
    std::string error;
    CHECK(editor.add_frame(true, error));
    CHECK(editor.frame() == 1);
    CHECK(editor.sprite().pixel(0, 1, 0, 0) == red);
    CHECK(editor.undo_text() == "Duplicate frame");
    // A copied frame shares its pixels until one is drawn on.
    stroke(editor, Tool::Pencil, blue, {{1, 1}});
    CHECK(editor.sprite().pixel(0, 0, 1, 1) == clear);
    CHECK(editor.add_frame(false, error));
    CHECK(editor.sprite().frames.size() == 3);
    CHECK(editor.sprite().pixel(0, 2, 0, 0) == clear);

    const std::optional<std::size_t> walk = editor.add_tag("walk", 0, 1, error);
    REQUIRE(walk.has_value());
    CHECK(editor.add_tag("walk", 2, 2, error) == 1U);
    CHECK(editor.sprite().tags[1].name == "walk_2");
    CHECK_FALSE(editor.add_tag("run", 1, 5, error).has_value());

    // A frame added inside a tag makes it longer; tags after it move along.
    editor.set_frame(1);
    CHECK(editor.add_frame(false, error));
    CHECK(editor.sprite().tags[0].to == 2);
    CHECK(editor.sprite().tags[1].from == 3);
    // Deleting a tag's only frame deletes the tag.
    editor.set_frame(3);
    CHECK(editor.remove_frame(error));
    CHECK(editor.sprite().tags.size() == 1);
    editor.set_frame(2);
    CHECK(editor.remove_frame(error));
    CHECK(editor.sprite().tags[0].to == 1);

    CHECK(editor.set_frame_duration(0, 250, error));
    CHECK(editor.set_frame_duration(0, 300, error));
    CHECK(editor.undo_text() == "Change duration of frame 1");
    CHECK_FALSE(editor.set_frame_duration(0, 0, error));
    CHECK(editor.set_frame_duration(0, 80, error, true));
    CHECK(editor.sprite().frames[1].duration_ms == 80);

    editor.set_frame(0);
    CHECK(editor.move_frame(false, error));
    CHECK(editor.frame() == 1);
    CHECK(editor.sprite().pixel(0, 1, 0, 0) == red);
    CHECK(editor.sprite().pixel(0, 0, 1, 1) == blue);
}

TEST_CASE("The palette is edited, and can come from the sprite") {
    sprite::Editor editor(sprite::Sprite::blank(2, 2));
    CHECK(editor.add_color(Color{1, 2, 3, 255}));
    CHECK_FALSE(editor.add_color(Color{1, 2, 3, 255}));
    CHECK(editor.set_color(0, Color{9, 9, 9, 255}));
    CHECK(editor.set_color(0, Color{8, 8, 8, 255}));
    CHECK(editor.undo_text() == "Change a colour");
    std::string error;
    CHECK_FALSE(editor.palette_from_sprite(error));
    CHECK(error == "Nothing is drawn yet.");
    stroke(editor, Tool::Pencil, red, {{0, 0}});
    stroke(editor, Tool::Pencil, blue, {{1, 1}});
    CHECK(editor.palette_from_sprite(error));
    CHECK(editor.sprite().palette == std::vector<Color>{red, blue});
}

TEST_CASE("Resizing the canvas keeps the pixels at its corner or middle") {
    sprite::Editor editor(sprite::Sprite::blank(2, 2));
    stroke(editor, Tool::Pencil, red, {{0, 0}});
    std::string error;
    CHECK(editor.resize(6, 4, sprite::Anchor::Center, error));
    CHECK(editor.sprite().width == 6);
    CHECK(editor.sprite().pixel(0, 0, 2, 1) == red);
    CHECK(editor.undo());
    CHECK(editor.resize(1, 1, sprite::Anchor::TopLeft, error));
    CHECK(editor.sprite().pixel(0, 0, 0, 0) == red);
    CHECK_FALSE(editor.resize(0, 4, sprite::Anchor::TopLeft, error));
}

TEST_CASE("The sprite sheet holds every frame, left to right then down") {
    sprite::Editor editor(sprite::Sprite::blank(2, 2));
    std::string error;
    stroke(editor, Tool::Pencil, red, {{0, 0}});
    editor.add_frame(false, error);
    stroke(editor, Tool::Pencil, blue, {{1, 1}});
    editor.add_frame(false, error);
    stroke(editor, Tool::Pencil, red, {{1, 0}});

    sprite::Image sheet = editor.sprite().sheet();
    CHECK(sheet.width == 6);
    CHECK(sheet.height == 2);
    CHECK(editor.set_sheet_columns(2, error));
    sheet = editor.sprite().sheet();
    CHECK(sheet.width == 4);
    CHECK(sheet.height == 4);
    // Frame 3 starts the second row.
    const auto at = [&](int x, int y) {
        const std::size_t i =
            (static_cast<std::size_t>(y) * sheet.width + static_cast<std::size_t>(x)) * 4;
        return Color{sheet.rgba[i], sheet.rgba[i + 1], sheet.rgba[i + 2], sheet.rgba[i + 3]};
    };
    CHECK(at(0, 0) == red);
    CHECK(at(3, 1) == blue);
    CHECK(at(1, 2) == red);

    // A sheet read back is the same frames.
    const std::optional<sprite::Sprite> read = sprite::import_sheet(sheet, 2, 2, error);
    REQUIRE(read.has_value());
    const sprite::Sprite back = read.value_or(sprite::Sprite{});
    CHECK(back.frames.size() == 3); // The empty fourth cell is left out.
    CHECK(back.sheet_columns == 2);
    CHECK(back.pixel(0, 1, 1, 1) == blue);
    CHECK_FALSE(sprite::import_sheet(sheet, 3, 2, error).has_value());
    CHECK(error == "The picture (4 × 4) does not divide into frames of 3 × 2.");
}

TEST_CASE("A sprite is saved with its sheet, and read back the same") {
    const Folder folder;
    sprite::Editor editor(sprite::Sprite::blank(3, 2));
    std::string error;
    stroke(editor, Tool::Pencil, red, {{0, 0}});
    editor.add_layer(error);
    editor.set_layer_opacity(1, 100);
    stroke(editor, Tool::Pencil, Color{0, 255, 0, 128}, {{2, 1}});
    editor.add_frame(true, error);
    editor.set_frame_duration(1, 40, error);
    editor.add_tag("blink", 0, 1, error);
    sprite::Tag tag = editor.sprite().tags[0];
    tag.direction = sprite::Direction::PingPong;
    CHECK(editor.set_tag(0, tag, error));
    editor.set_layer_locked(0, true);

    const fs::path file = folder.path / "hero.dsprite";
    REQUIRE(sprite::save(editor.sprite(), file, error));
    CHECK(fs::exists(folder.path / "hero.png"));
    // The game's description of the sheet: frame size, times and tags.
    std::ifstream described(folder.path / "hero.sheet", std::ios::binary);
    const std::string sheet_text((std::istreambuf_iterator<char>(described)),
                                 std::istreambuf_iterator<char>());
    const std::optional<engine::scene2d::SheetInfo> info =
        engine::scene2d::parse_sheet(sheet_text, error);
    REQUIRE(info.has_value());
    const engine::scene2d::SheetInfo sheet_info = info.value_or(engine::scene2d::SheetInfo{});
    CHECK(sheet_info.frame_width == 3);
    CHECK(sheet_info.frame_height == 2);
    CHECK(sheet_info.durations_ms == std::vector<std::uint32_t>{100, 40});
    REQUIRE(sheet_info.tags.size() == 1);
    CHECK(sheet_info.tags[0].name == "blink");
    CHECK(sheet_info.tags[0].direction == engine::scene2d::TagDirection::PingPong);

    const std::optional<sprite::Sprite> loaded = sprite::load(file, error);
    REQUIRE(loaded.has_value());
    const sprite::Sprite back = loaded.value_or(sprite::Sprite{});
    CHECK(back.width == 3);
    CHECK(back.layers.size() == 2);
    CHECK(back.layers[0].locked);
    CHECK(back.layers[1].opacity == 100);
    CHECK(back.frames[1].duration_ms == 40);
    REQUIRE(back.tags.size() == 1);
    CHECK(back.tags[0].name == "blink");
    CHECK(back.tags[0].direction == sprite::Direction::PingPong);
    CHECK(back.palette == editor.sprite().palette);
    CHECK(back.pixel(0, 1, 0, 0) == red);
    CHECK(back.pixel(1, 0, 2, 1) == Color{0, 255, 0, 128});
    CHECK(sprite::to_text(back) == sprite::to_text(editor.sprite()));

    // The sheet beside it is what the game draws.
    std::ifstream in(folder.path / "hero.png", std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    const std::optional<sprite::Image> decoded = engine::assets::decode_png(bytes, error);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value_or(sprite::Image{}).width == 6);
    CHECK(decoded.value_or(sprite::Image{}).height == 2);

    // Files that are not sprites are refused in plain language.
    CHECK_FALSE(sprite::from_text("{\"format\": \"sparxel.scene\"}", error).has_value());
    CHECK(error == "This is not a sprite file (its format is not \"sparxel.sprite\").");
    CHECK_FALSE(
        sprite::from_text(
            "{\"format\": \"sparxel.sprite\", \"version\": 1, \"width\": 0, \"height\": 2}", error)
            .has_value());
    CHECK_FALSE(sprite::load(folder.path / "missing.dsprite", error).has_value());
}

TEST_CASE("Sound effects: each kind sounds its way, the same every time, and saves as WAV") {
    namespace sfx = editor::sfx;
    // The same seed, the same sound; another seed, another of the same kind.
    const sfx::Params coin = sfx::make(sfx::Preset::Coin, 7);
    CHECK(sfx::synthesize(coin) == sfx::synthesize(sfx::make(sfx::Preset::Coin, 7)));
    CHECK(sfx::synthesize(coin) != sfx::synthesize(sfx::make(sfx::Preset::Coin, 8)));
    CHECK(coin.arpeggio > 1.0); // a coin's two notes

    // As long as its envelope, starting and ending quiet.
    sfx::Params p;
    p.attack = 0.05;
    p.sustain = 0.1;
    p.decay = 0.1;
    const std::vector<float> samples = sfx::synthesize(p, 10000);
    CHECK(samples.size() == 2500);
    CHECK(std::abs(samples.front()) < 0.01F);
    CHECK(std::abs(samples.back()) < 0.05F);

    // A jump rises: more waves in its second half than its first.
    const sfx::Params jump = sfx::make(sfx::Preset::Jump, 3);
    CHECK(jump.slide > 0.0);
    const std::vector<float> rising = sfx::synthesize(jump, 44100);
    const auto crossings = [&](std::size_t from, std::size_t to) {
        int count = 0;
        for (std::size_t i = from + 1; i < to; ++i) {
            count += (rising[i - 1] < 0.0F) != (rising[i] < 0.0F) ? 1 : 0;
        }
        return count;
    };
    const std::size_t half = rising.size() / 2;
    CHECK(crossings(half, rising.size()) > crossings(0, half));

    // Numbers out of range are kept in it; a mutation stays near.
    sfx::Params wild;
    wild.frequency = 1e9;
    wild.sustain = 100.0;
    wild.duty = -3.0;
    const sfx::Params tame = sfx::clamped(wild);
    CHECK(tame.frequency == 4000.0);
    CHECK(tame.attack + tame.sustain + tame.decay <= sfx::longest + 1e-9);
    CHECK(tame.duty == 0.05);
    const sfx::Params near = sfx::mutate(coin, 99);
    CHECK(std::abs(near.frequency - coin.frequency) <= coin.frequency * 0.11);

    // A mono 16-bit WAV, which the engine's audio reads.
    const std::vector<std::uint8_t> wav = sfx::to_wav(samples, 10000);
    CHECK(wav.size() == 44 + (samples.size() * 2));
    CHECK(std::string(wav.begin(), wav.begin() + 4) == "RIFF");
    CHECK(std::string(wav.begin() + 8, wav.begin() + 12) == "WAVE");
    for (const sfx::Preset preset : sfx::presets) {
        CHECK_FALSE(sfx::synthesize(sfx::make(preset, 1)).empty());
        CHECK_FALSE(sfx::preset_name(preset).empty());
    }
}

TEST_CASE("Dither draws a checkerboard; Shade moves pixels one palette step towards the colour") {
    sprite::Editor editor(sprite::Sprite::blank(4, 4));
    sprite::Brush dither;
    dither.dither = true;
    CHECK(stroke(editor, Tool::FilledRectangle, red, {{0, 0}, {3, 3}}, dither));
    CHECK(drawn(editor) == 8); // half of 16
    CHECK(editor.sprite().pixel(0, 0, 0, 0) == red);
    CHECK(editor.sprite().pixel(0, 0, 1, 0) == clear);

    // A ramp of three in the palette: dark, middle, light.
    sprite::Sprite ramp = sprite::Sprite::blank(4, 1);
    const Color dark{40, 40, 40, 255};
    const Color middle{120, 120, 120, 255};
    const Color light{220, 220, 220, 255};
    ramp.palette = {dark, middle, light};
    sprite::Editor shading(std::move(ramp));
    CHECK(stroke(shading, Tool::FilledRectangle, dark, {{0, 0}, {3, 0}}));
    sprite::Brush shade;
    shade.shade = true;
    // Shading towards light: each pixel one step, once a stroke, however often passed over.
    CHECK(stroke(shading, Tool::Pencil, light, {{0, 0}, {1, 0}, {0, 0}, {1, 0}}, shade));
    CHECK(shading.sprite().pixel(0, 0, 0, 0) == middle);
    CHECK(shading.sprite().pixel(0, 0, 1, 0) == middle);
    CHECK(shading.sprite().pixel(0, 0, 2, 0) == dark); // not passed over
    CHECK(stroke(shading, Tool::Pencil, light, {{0, 0}}, shade));
    CHECK(shading.sprite().pixel(0, 0, 0, 0) == light);
    // Back towards dark; at the end of the ramp it stays.
    CHECK(stroke(shading, Tool::Pencil, dark, {{1, 0}}, shade));
    CHECK(shading.sprite().pixel(0, 0, 1, 0) == dark);
    CHECK_FALSE(stroke(shading, Tool::Pencil, dark, {{1, 0}}, shade));
}
