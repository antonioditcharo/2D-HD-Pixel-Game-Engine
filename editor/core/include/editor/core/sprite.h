#pragma once

// Sprite Studio's model (plan/06 §3.3, features B1-B5): a pixel-art sprite of layers and
// frames, drawn with pixel-art tools, with undo. It has no user interface, so it is tested
// on its own; the Qt application shows it.
//
// A sprite is saved as a ".dsprite" file (its layers, frames, tags and palette), and beside
// it as the PNG sprite sheet the game draws: every frame flattened, numbered left to right
// then top to bottom, as Sprite's frame_width and frame_height read them
// (docs/dev/scene2d.md §3). The game never reads the .dsprite.
//
// Cels are shared between undo snapshots until one is drawn on, so a snapshot of the whole
// sprite costs a few pointers, and undo is correct for every kind of edit.
//
// See docs/dev/sprite-studio.md.

#include "engine/assets/assets.h"
#include "engine/scene2d/scene2d.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor::sprite {

using Image = engine::assets::Image;

/// The largest width or height of a sprite, in pixels.
inline constexpr int max_side = 1024;
/// The most layers and frames a sprite has.
inline constexpr std::size_t max_layers = 64;
inline constexpr std::size_t max_frames = 1024;
/// The widest a saved sprite sheet is allowed to grow before frames wrap to a new row.
inline constexpr int max_sheet_width = 4096;

/// The ".dsprite" format name and version.
inline constexpr std::string_view format = "sparxel.sprite";
inline constexpr int format_version = 1;

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    friend bool operator==(const Color&, const Color&) = default;
};

/// "#rrggbb", or "#rrggbbaa" when not opaque.
[[nodiscard]] std::string to_hex(Color color);
/// Reads "#rgb", "#rrggbb" or "#rrggbbaa" (the "#" is optional).
[[nodiscard]] std::optional<Color> from_hex(std::string_view text);

/// A rectangle of pixels; empty when either side is 0.
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool empty() const noexcept { return width <= 0 || height <= 0; }
    [[nodiscard]] bool contains(int px, int py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    friend bool operator==(const Rect&, const Rect&) = default;
};

/// The rectangle with corners (x0, y0) and (x1, y1), both inside it, in either order.
[[nodiscard]] Rect rect_between(int x0, int y0, int x1, int y1) noexcept;

struct Layer {
    std::string name;
    bool visible = true;
    bool locked = false;
    std::uint8_t opacity = 255;
};

struct Frame {
    /// How long the frame shows when playing, in milliseconds (1 to 65,535).
    std::uint32_t duration_ms = 100;
};

/// Which way a tag's frames play.
enum class Direction : std::uint8_t { Forward, Reverse, PingPong };

/// A named run of frames, such as "walk" or "idle": an animation clip.
struct Tag {
    std::string name;
    std::size_t from = 0; ///< First frame, counting from 0.
    std::size_t to = 0;   ///< Last frame, included.
    Direction direction = Direction::Forward;
};

/// The pixels of one layer in one frame. Null when nothing is drawn there.
using Cel = std::shared_ptr<const Image>;

struct Sprite {
    int width = 0;
    int height = 0;
    std::vector<Layer> layers; ///< Bottom first.
    std::vector<Frame> frames;
    std::vector<Tag> tags;
    std::vector<Color> palette;
    /// Frames across in the saved sheet; 0 chooses as many as fit in max_sheet_width.
    int sheet_columns = 0;
    /// cels[layer][frame].
    std::vector<std::vector<Cel>> cels;

    /// A sprite of one empty layer ("Layer 1") and one frame, with the default palette.
    /// Sides are kept within 1 to max_side.
    [[nodiscard]] static Sprite blank(int width, int height);

    /// The colour of a layer's pixel in a frame; transparent off the canvas or on an empty
    /// cel.
    [[nodiscard]] Color pixel(std::size_t layer, std::size_t frame, int x, int y) const;
    /// The frame as the game shows it: visible layers, bottom first, blended with their
    /// opacity.
    [[nodiscard]] Image flatten(std::size_t frame) const;
    /// The colour shown at a pixel of the frame, as flatten() would give it.
    [[nodiscard]] Color shown(std::size_t frame, int x, int y) const;
    /// The sprite sheet: every frame flattened, left to right then top to bottom.
    [[nodiscard]] Image sheet() const;
    /// How many frames across sheet() puts.
    [[nodiscard]] int columns() const noexcept;
    /// Every colour used by any cel, opaque or not, in the order first found (at most
    /// 256).
    [[nodiscard]] std::vector<Color> used_colors() const;
};

/// The default palette of a new sprite: 32 colours in ramps (greys, skin, reds, oranges,
/// greens, blues, purples) suited to pixel art, first transparent-free black and white.
[[nodiscard]] std::vector<Color> default_palette();
/// The colours of a palette written as text, "#1a1c2c #5d275d, ...": each "#rrggbb" (opaque),
/// in order, without repeats; anything else is skipped.
[[nodiscard]] std::vector<Color> parse_palette(std::string_view text);
/// A palette as parse_palette() reads it: "#1a1c2c #5d275d".
[[nodiscard]] std::string palette_text(const std::vector<Color>& colors);

/// Reads a ".dsprite" file. Returns nothing, with `error` in plain language, if it cannot.
[[nodiscard]] std::optional<Sprite> load(const std::filesystem::path& path, std::string& error);
/// Writes a ".dsprite" file, the sprite sheet PNG beside it (the same name with ".png"),
/// and the sheet's description the game reads (".sheet": the frame size, each frame's time
/// and the tags). Returns false, with `error`, if one cannot be written.
bool save(const Sprite& sprite, const std::filesystem::path& path, std::string& error);
/// What the game reads of a sprite: its frame size, frame times and tags.
[[nodiscard]] engine::scene2d::SheetInfo sheet_info(const Sprite& sprite);
/// The ".dsprite" text of a sprite, as save() writes it.
[[nodiscard]] std::string to_text(const Sprite& sprite);
/// Reads ".dsprite" text.
[[nodiscard]] std::optional<Sprite> from_text(std::string_view text, std::string& error);

/// A sprite made from a sprite sheet: one layer, and a frame for each cell of
/// `frame_width` × `frame_height` pixels, left to right then top to bottom (0 for the
/// whole picture). Empty frames at the end are left out. Returns nothing, with `error`, if
/// the picture does not divide into such cells, or is larger than a sprite can be.
[[nodiscard]] std::optional<Sprite> import_sheet(const Image& image, int frame_width,
                                                 int frame_height, std::string& error);

/// What a drag on the canvas does.
enum class Tool : std::uint8_t {
    Pencil,    ///< Paints the brush along the drag.
    Eraser,    ///< Clears the brush along the drag.
    Line,      ///< A straight line from where the drag began.
    Rectangle, ///< The outline of the rectangle dragged.
    FilledRectangle,
    Ellipse, ///< The outline of the ellipse inside the rectangle dragged.
    FilledEllipse,
    Fill,   ///< Paints the clicked pixel and those of its colour joined to it.
    Select, ///< Selects the rectangle dragged.
    Move,   ///< Moves the selected pixels, or the whole layer, by the drag.
};

/// How drawing tools draw.
struct Brush {
    int size = 1; ///< Width of the square brush, 1 to 32.
    /// Pencil strokes of one pixel leave out the corner pixels of L shapes, so a curve is
    /// one pixel thick (Aseprite's "pixel-perfect").
    bool pixel_perfect = true;
    /// Fill paints every pixel of the clicked colour in the layer, joined or not.
    bool fill_all = false;
    /// Everything drawn is mirrored across the middle of the canvas, left to right and
    /// top to bottom.
    bool mirror_x = false;
    bool mirror_y = false;
    /// Dither: only the pixels of a checkerboard are drawn, for gradients and textures.
    bool dither = false;
    /// Shade: each pixel drawn over moves one step along the palette towards the colour
    /// drawn with (lighter or darker along its ramp), as Aseprite's shading ink; pixels not
    /// in the palette, and see-through ones, stay.
    bool shade = false;
};

/// Where a resized canvas keeps the old pixels.
enum class Anchor : std::uint8_t { TopLeft, Center };

/// A sprite being edited: drawing, layers, frames, tags and the palette, with undo and
/// redo. Every edit is one undo step, labelled in plain language; consecutive changes to
/// one value (an opacity dragged, a duration typed) merge until end_merge().
class Editor {
public:
    explicit Editor(Sprite sprite);

    [[nodiscard]] const Sprite& sprite() const noexcept { return sprite_; }

    /// The layer and frame drawn on.
    [[nodiscard]] std::size_t layer() const noexcept { return layer_; }
    [[nodiscard]] std::size_t frame() const noexcept { return frame_; }
    void set_layer(std::size_t layer) noexcept;
    void set_frame(std::size_t frame) noexcept;

    // ----- Drawing -----

    /// Starts a drag of `tool` at pixel (x, y) with `color`. Refused, with `error`, on a
    /// hidden or locked layer (every tool but Select). Fill paints at once.
    bool begin(Tool tool, Color color, const Brush& brush, int x, int y, std::string& error);
    /// Continues the drag to pixel (x, y): a stroke reaches it, a shape is redrawn to it.
    void drag(int x, int y);
    /// Ends the drag: what it drew is one undo step. Returns false if it changed nothing.
    bool end();
    [[nodiscard]] bool dragging() const noexcept { return drag_.has_value(); }

    // ----- Selection -----

    /// The selected rectangle, inside the canvas; empty when nothing is selected.
    [[nodiscard]] const Rect& selection() const noexcept { return selection_; }
    void select(const Rect& rect);
    void select_all();
    void deselect();
    /// Clears the selected pixels (or the whole cel) of the layer. False if nothing
    /// changed.
    bool clear(std::string& error);
    /// Mirrors the selected pixels (or the whole cel) left to right, or top to bottom.
    bool flip(bool horizontal, std::string& error);
    /// Keeps a copy of the selected pixels (or the whole cel) to paste.
    void copy();
    bool cut(std::string& error);
    /// Pastes what was copied onto the layer, at the selection's corner (or the canvas'),
    /// and selects it, to move with the Move tool.
    bool paste(std::string& error);
    [[nodiscard]] bool can_paste() const noexcept { return clipboard_ != nullptr; }

    // ----- Layers -----

    /// Adds an empty layer above the current one, named "Layer N", and makes it current.
    bool add_layer(std::string& error);
    /// Adds a copy of the current layer above it.
    bool duplicate_layer(std::string& error);
    /// Deletes the current layer; the last one is kept.
    bool remove_layer(std::string& error);
    /// Moves the current layer up (towards the top) or down.
    bool move_layer(bool up, std::string& error);
    /// Draws the current layer onto the one below it, then deletes it.
    bool merge_down(std::string& error);
    bool rename_layer(std::size_t layer, std::string_view name, std::string& error);
    bool set_layer_visible(std::size_t layer, bool visible);
    bool set_layer_locked(std::size_t layer, bool locked);
    /// Opacity 0 to 255. Merges while dragged.
    bool set_layer_opacity(std::size_t layer, int opacity);

    // ----- Frames -----

    /// Adds a frame after the current one, empty or (`copy`) a copy of it, and makes it
    /// current. Tags around it grow.
    bool add_frame(bool copy, std::string& error);
    /// Deletes the current frame; the last one is kept. Tags shrink or go.
    bool remove_frame(std::string& error);
    /// Moves the current frame one place earlier or later.
    bool move_frame(bool earlier, std::string& error);
    /// 1 to 65,535 milliseconds. Merges while typed. With `all`, every frame's.
    bool set_frame_duration(std::size_t frame, int duration_ms, std::string& error,
                            bool all = false);

    // ----- Tags -----

    /// Tags frames `from` to `to` as `name` (made unique). Returns its index.
    std::optional<std::size_t> add_tag(std::string_view name, std::size_t from, std::size_t to,
                                       std::string& error);
    bool set_tag(std::size_t tag, const Tag& value, std::string& error);
    bool remove_tag(std::size_t tag);

    // ----- Palette and canvas -----

    /// Adds a colour to the end of the palette, unless it is there already.
    bool add_color(Color color);
    bool set_color(std::size_t index, Color color);
    bool remove_color(std::size_t index);
    /// Replaces the palette with every colour the sprite uses.
    bool palette_from_sprite(std::string& error);
    /// Makes the canvas `width` × `height`, keeping the pixels at `anchor`.
    bool resize(int width, int height, Anchor anchor, std::string& error);
    /// Frames across in the saved sheet (0 chooses).
    bool set_sheet_columns(int columns, std::string& error);

    // ----- Undo -----

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    /// What undo or redo would change, such as "Draw with the pencil".
    [[nodiscard]] std::string undo_text() const;
    [[nodiscard]] std::string redo_text() const;
    bool undo();
    bool redo();
    /// Ends merging: the next change to the same value is a step of its own.
    void end_merge() noexcept { merge_key_.clear(); }

    /// Whether there are changes since opening or mark_saved().
    [[nodiscard]] bool modified() const noexcept { return saved_ != serial_; }
    void mark_saved() noexcept { saved_ = serial_; }

private:
    struct State {
        Sprite sprite;
        std::size_t layer = 0;
        std::size_t frame = 0;
        Rect selection;
    };
    struct Step {
        std::string label;
        State before;
        State after;
        std::uint64_t serial_before = 0;
        std::uint64_t serial_after = 0;
    };
    struct Drag {
        Tool tool = Tool::Pencil;
        Color color;
        Brush brush;
        int x0 = 0;
        int y0 = 0;
        int last_x = 0;
        int last_y = 0;
        State before;
        Cel base;                               ///< The cel as it was when the drag began.
        Cel lifted;                             ///< Move: the pixels being moved.
        Rect lifted_from;                       ///< Move: where they were.
        std::vector<std::array<int, 2>> points; ///< Pencil and eraser: the stroke so far.
    };

    [[nodiscard]] State state() const;
    void restore(const State& state);
    /// Records the change from `before` to now as an undo step.
    void record(std::string label, State before, std::string merge_key = {});
    [[nodiscard]] bool editable(std::string& error) const;
    /// The current cel's pixels, to change (a copy of them if a snapshot shares them).
    Image& cel_for_writing();
    void redraw_drag();

    Sprite sprite_;
    std::size_t layer_ = 0;
    std::size_t frame_ = 0;
    Rect selection_;
    std::optional<Drag> drag_;
    Cel clipboard_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::string merge_key_;
    std::uint64_t serial_ = 0;
    std::uint64_t next_serial_ = 1;
    std::uint64_t saved_ = 0;
};

} // namespace editor::sprite
