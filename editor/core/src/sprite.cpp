#include "editor/core/sprite.h"

#include "engine/serialize/json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

namespace editor::sprite {

namespace fs = std::filesystem;
using engine::serialize::Json;

namespace {

/// How many undo steps are kept; older ones are forgotten.
constexpr std::size_t max_undo = 256;

Image blank_image(int width, int height) {
    Image image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    return image;
}

std::size_t offset(const Image& image, int x, int y) {
    return (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
}

bool inside(const Image& image, int x, int y) {
    return x >= 0 && y >= 0 && x < static_cast<int>(image.width) &&
           y < static_cast<int>(image.height);
}

Color get(const Image& image, int x, int y) {
    if (!inside(image, x, y)) {
        return {};
    }
    const std::size_t at = offset(image, x, y);
    return {image.rgba[at], image.rgba[at + 1], image.rgba[at + 2], image.rgba[at + 3]};
}

void put(Image& image, int x, int y, Color color) {
    if (!inside(image, x, y)) {
        return;
    }
    const std::size_t at = offset(image, x, y);
    image.rgba[at] = color.r;
    image.rgba[at + 1] = color.g;
    image.rgba[at + 2] = color.b;
    image.rgba[at + 3] = color.a;
}

bool all_clear(const Image& image) {
    for (std::size_t i = 3; i < image.rgba.size(); i += 4) {
        if (image.rgba[i] != 0) {
            return false;
        }
    }
    return true;
}

/// `src` over `dst`, straight (not premultiplied) alpha, with `src`'s alpha scaled by
/// `opacity` (0 to 255).
Color blend(Color dst, Color src, int opacity) {
    const double sa = (src.a / 255.0) * (opacity / 255.0);
    if (sa <= 0.0) {
        return dst;
    }
    const double da = dst.a / 255.0;
    const double out_a = sa + da * (1.0 - sa);
    const auto channel = [&](std::uint8_t s, std::uint8_t d) {
        const double v = (s * sa + d * da * (1.0 - sa)) / out_a;
        return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
    };
    return {channel(src.r, dst.r), channel(src.g, dst.g), channel(src.b, dst.b),
            static_cast<std::uint8_t>(std::clamp(std::lround(out_a * 255.0), 0L, 255L))};
}

/// Calls `plot` for each pixel of the line from (x0, y0) to (x1, y1), both included
/// (Bresenham).
template <typename Plot>
void line(int x0, int y0, int x1, int y1, Plot&& plot) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(x0, y0);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/// Calls `plot` for each pixel of the ellipse inside the rectangle with corners (x0, y0)
/// and (x1, y1) (Alois Zingl's integer algorithm, "A Rasterizing Algorithm for Drawing
/// Curves", 2012).
template <typename Plot>
void ellipse(int x0, int y0, int x1, int y1, Plot&& plot) {
    std::int64_t a = std::abs(x1 - x0);
    const std::int64_t b = std::abs(y1 - y0);
    std::int64_t b1 = b & 1;
    std::int64_t dx = 4 * (1 - a) * b * b;
    std::int64_t dy = 4 * (b1 + 1) * a * a;
    std::int64_t err = dx + dy + b1 * a * a;
    if (x0 > x1) {
        x0 = x1;
        x1 += static_cast<int>(a);
    }
    if (y0 > y1) {
        y0 = y1;
    }
    y0 += static_cast<int>((b + 1) / 2);
    y1 = y0 - static_cast<int>(b1);
    a *= 8 * a;
    b1 = 8 * b * b;
    do {
        plot(x1, y0);
        plot(x0, y0);
        plot(x0, y1);
        plot(x1, y1);
        const std::int64_t e2 = 2 * err;
        if (e2 <= dy) {
            ++y0;
            --y1;
            dy += a;
            err += dy;
        }
        if (e2 >= dx || 2 * err > dy) {
            ++x0;
            --x1;
            dx += b1;
            err += dx;
        }
    } while (x0 <= x1);
    while (y0 - y1 < b) {
        plot(x0 - 1, y0);
        plot(x1 + 1, y0++);
        plot(x0 - 1, y1);
        plot(x1 + 1, y1--);
    }
}

/// Leaves out the middle pixel of every L-shaped corner, so a freehand line is one pixel
/// thick (the "pixel-perfect" stroke of Aseprite and other pixel editors).
std::vector<std::array<int, 2>> pixel_perfect(const std::vector<std::array<int, 2>>& points) {
    std::vector<std::array<int, 2>> out;
    for (const std::array<int, 2>& point : points) {
        out.push_back(point);
        while (out.size() >= 3) {
            const auto& a = out[out.size() - 3];
            const auto& b = out[out.size() - 2];
            const auto& c = out[out.size() - 1];
            const bool corner = (a[0] == b[0] || a[1] == b[1]) && (c[0] == b[0] || c[1] == b[1]) &&
                                a[0] != c[0] && a[1] != c[1];
            if (!corner) {
                break;
            }
            out.erase(out.end() - 2);
        }
    }
    return out;
}

std::string base64(const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t b0 = bytes[i];
        const std::uint32_t b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0U;
        const std::uint32_t b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0U;
        const std::uint32_t n = (b0 << 16U) | (b1 << 8U) | b2;
        out += digits[(n >> 18U) & 63U];
        out += digits[(n >> 12U) & 63U];
        out += i + 1 < bytes.size() ? digits[(n >> 6U) & 63U] : '=';
        out += i + 2 < bytes.size() ? digits[n & 63U] : '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> from_base64(std::string_view text) {
    const auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '+') {
            return 62;
        }
        if (c == '/') {
            return 63;
        }
        return -1;
    };
    std::vector<std::uint8_t> out;
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=') {
            break;
        }
        const int v = value(c);
        if (v < 0) {
            return std::nullopt;
        }
        buffer = (buffer << 6U) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(
                static_cast<std::uint8_t>((buffer >> static_cast<unsigned>(bits)) & 0xFFU));
        }
    }
    return out;
}

std::string_view direction_id(Direction direction) {
    switch (direction) {
    case Direction::Reverse:
        return "reverse";
    case Direction::PingPong:
        return "ping_pong";
    case Direction::Forward:
        break;
    }
    return "forward";
}

std::string next_layer_name(const Sprite& sprite) {
    for (std::size_t n = sprite.layers.size() + 1;; ++n) {
        std::string name = "Layer " + std::to_string(n);
        if (std::none_of(sprite.layers.begin(), sprite.layers.end(),
                         [&](const Layer& layer) { return layer.name == name; })) {
            return name;
        }
    }
}

std::string unique_tag_name(const Sprite& sprite, std::string_view wanted,
                            std::optional<std::size_t> except = std::nullopt) {
    std::string base = wanted.empty() ? std::string("tag") : std::string(wanted);
    const auto taken = [&](const std::string& name) {
        for (std::size_t i = 0; i < sprite.tags.size(); ++i) {
            if (i != except && sprite.tags[i].name == name) {
                return true;
            }
        }
        return false;
    };
    if (!taken(base)) {
        return base;
    }
    for (int n = 2;; ++n) {
        std::string name = base + "_" + std::to_string(n);
        if (!taken(name)) {
            return name;
        }
    }
}

} // namespace

std::string to_hex(Color color) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "#";
    const auto add = [&](std::uint8_t v) {
        out += digits[v >> 4U];
        out += digits[v & 15U];
    };
    add(color.r);
    add(color.g);
    add(color.b);
    if (color.a != 255) {
        add(color.a);
    }
    return out;
}

std::optional<Color> from_hex(std::string_view text) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    std::vector<int> values;
    for (const char c : text) {
        const int v = digit(c);
        if (v < 0) {
            return std::nullopt;
        }
        values.push_back(v);
    }
    const auto byte = [&](std::size_t i) {
        return static_cast<std::uint8_t>(values[i] * 16 + values[i + 1]);
    };
    if (values.size() == 3) {
        return Color{static_cast<std::uint8_t>(values[0] * 17),
                     static_cast<std::uint8_t>(values[1] * 17),
                     static_cast<std::uint8_t>(values[2] * 17), 255};
    }
    if (values.size() == 6) {
        return Color{byte(0), byte(2), byte(4), 255};
    }
    if (values.size() == 8) {
        return Color{byte(0), byte(2), byte(4), byte(6)};
    }
    return std::nullopt;
}

Rect rect_between(int x0, int y0, int x1, int y1) noexcept {
    return {std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1};
}

std::vector<Color> parse_palette(std::string_view text) {
    std::vector<Color> colors;
    std::size_t at = 0;
    while ((at = text.find('#', at)) != std::string_view::npos) {
        const std::string_view hex = text.substr(at + 1, 6);
        at += 1;
        if (hex.size() != 6 || !std::ranges::all_of(hex, [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            })) {
            continue;
        }
        const auto part = [&hex](std::size_t i) {
            unsigned value = 0;
            std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
            return static_cast<std::uint8_t>(value);
        };
        const Color c{part(0), part(2), part(4), 255};
        if (std::ranges::find(colors, c) == colors.end()) {
            colors.push_back(c);
        }
        at += 6;
    }
    return colors;
}

std::string palette_text(const std::vector<Color>& colors) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const Color& c : colors) {
        if (!out.empty()) {
            out += ' ';
        }
        out += '#';
        for (const std::uint8_t v : {c.r, c.g, c.b}) {
            out += digits[v >> 4U];
            out += digits[v & 15U];
        }
    }
    return out;
}

std::vector<Color> default_palette() {
    static constexpr std::string_view colors[] = {
        // Greys, black to white.
        "#000000", "#1f1f29", "#3d3d4f", "#6b6b80", "#9e9eb0", "#d4d4dc", "#ffffff",
        // Browns and skin.
        "#3b2027", "#6e3b36", "#a8644d", "#d99a6c", "#f2cfa5",
        // Reds.
        "#5c1a24", "#a32638", "#e04848", "#ff8a7a",
        // Oranges and yellows.
        "#b3551b", "#f08c28", "#ffc94a", "#fff28a",
        // Greens.
        "#1d3b2a", "#2f6b3a", "#5aa33f", "#9fd65a",
        // Blues.
        "#16243d", "#24508a", "#3f8fd9", "#8ad1f2",
        // Purples.
        "#3a1f4d", "#6e3a8a", "#b06ad1", "#e8a8f0"};
    std::vector<Color> out;
    for (const std::string_view text : colors) {
        if (const std::optional<Color> color = from_hex(text)) {
            out.push_back(*color);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// Sprite
// ---------------------------------------------------------------------------------------

Sprite Sprite::blank(int width, int height) {
    Sprite sprite;
    sprite.width = std::clamp(width, 1, max_side);
    sprite.height = std::clamp(height, 1, max_side);
    sprite.layers.push_back({"Layer 1"});
    sprite.frames.push_back({});
    sprite.palette = default_palette();
    sprite.cels = {{nullptr}};
    return sprite;
}

Color Sprite::pixel(std::size_t layer, std::size_t frame, int x, int y) const {
    if (layer >= cels.size() || frame >= cels[layer].size() || !cels[layer][frame]) {
        return {};
    }
    return get(*cels[layer][frame], x, y);
}

Color Sprite::shown(std::size_t frame, int x, int y) const {
    Color out;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        if (layers[layer].visible) {
            out = blend(out, pixel(layer, frame, x, y), layers[layer].opacity);
        }
    }
    return out;
}

Image Sprite::flatten(std::size_t frame) const {
    Image out = blank_image(width, height);
    if (frame >= frames.size()) {
        return out;
    }
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        if (!layers[layer].visible || !cels[layer][frame]) {
            continue;
        }
        const Image& cel = *cels[layer][frame];
        const int opacity = layers[layer].opacity;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const Color src = get(cel, x, y);
                if (src.a != 0) {
                    put(out, x, y, blend(get(out, x, y), src, opacity));
                }
            }
        }
    }
    return out;
}

int Sprite::columns() const noexcept {
    const int count = static_cast<int>(std::max<std::size_t>(frames.size(), 1));
    if (sheet_columns > 0) {
        return std::min(sheet_columns, count);
    }
    return std::clamp(max_sheet_width / std::max(width, 1), 1, count);
}

Image Sprite::sheet() const {
    const int across = columns();
    const int count = static_cast<int>(frames.size());
    const int down = (count + across - 1) / across;
    Image out = blank_image(width * across, height * std::max(down, 1));
    for (int i = 0; i < count; ++i) {
        const Image frame = flatten(static_cast<std::size_t>(i));
        const int ox = (i % across) * width;
        const int oy = (i / across) * height;
        for (int y = 0; y < height; ++y) {
            std::copy_n(frame.rgba.begin() + static_cast<std::ptrdiff_t>(offset(frame, 0, y)),
                        static_cast<std::size_t>(width) * 4,
                        out.rgba.begin() + static_cast<std::ptrdiff_t>(offset(out, ox, oy + y)));
        }
    }
    return out;
}

std::vector<Color> Sprite::used_colors() const {
    std::vector<Color> out;
    for (const auto& layer : cels) {
        for (const Cel& cel : layer) {
            if (!cel) {
                continue;
            }
            for (std::size_t i = 0; i + 3 < cel->rgba.size(); i += 4) {
                const Color c{cel->rgba[i], cel->rgba[i + 1], cel->rgba[i + 2], cel->rgba[i + 3]};
                if (c.a == 0 || std::find(out.begin(), out.end(), c) != out.end()) {
                    continue;
                }
                out.push_back(c);
                if (out.size() >= 256) {
                    return out;
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------

std::string to_text(const Sprite& sprite) {
    Json json = Json::object();
    json["format"] = format;
    json["version"] = format_version;
    json["width"] = sprite.width;
    json["height"] = sprite.height;
    if (sprite.sheet_columns > 0) {
        json["sheet_columns"] = sprite.sheet_columns;
    }
    Json layers = Json::array();
    for (const Layer& layer : sprite.layers) {
        layers.push_back({{"name", layer.name},
                          {"visible", layer.visible},
                          {"locked", layer.locked},
                          {"opacity", layer.opacity}});
    }
    json["layers"] = std::move(layers);
    Json frames = Json::array();
    for (const Frame& frame : sprite.frames) {
        frames.push_back({{"duration_ms", frame.duration_ms}});
    }
    json["frames"] = std::move(frames);
    Json tags = Json::array();
    for (const Tag& tag : sprite.tags) {
        tags.push_back({{"name", tag.name},
                        {"from", tag.from},
                        {"to", tag.to},
                        {"direction", direction_id(tag.direction)}});
    }
    json["tags"] = std::move(tags);
    Json palette = Json::array();
    for (const Color color : sprite.palette) {
        palette.push_back(to_hex(color));
    }
    json["palette"] = std::move(palette);
    Json cels = Json::array();
    for (std::size_t layer = 0; layer < sprite.cels.size(); ++layer) {
        for (std::size_t frame = 0; frame < sprite.cels[layer].size(); ++frame) {
            const Cel& cel = sprite.cels[layer][frame];
            if (!cel || all_clear(*cel)) {
                continue;
            }
            cels.push_back({{"layer", layer},
                            {"frame", frame},
                            {"png", base64(engine::assets::encode_png(*cel))}});
        }
    }
    json["cels"] = std::move(cels);
    return engine::serialize::to_text(json);
}

std::optional<Sprite> from_text(std::string_view text, std::string& error) {
    std::vector<engine::serialize::Issue> issues;
    const std::optional<Json> parsed = engine::serialize::parse(text, issues);
    if (!parsed || !parsed->is_object()) {
        error = issues.empty() ? "This is not a sprite file." : issues.front().message;
        return std::nullopt;
    }
    const Json& json = *parsed;
    if (json.value("format", std::string()) != format) {
        error = "This is not a sprite file (its format is not \"sparxel.sprite\").";
        return std::nullopt;
    }
    if (!json.contains("version") || !json["version"].is_number_integer() ||
        json["version"].get<int>() > format_version) {
        error = "This sprite was saved by a newer version of Sparxel.";
        return std::nullopt;
    }
    const auto int_of = [&](const Json& at, const char* key, int fallback) {
        return at.contains(key) && at[key].is_number_integer() ? at[key].get<int>() : fallback;
    };
    Sprite sprite;
    sprite.width = int_of(json, "width", 0);
    sprite.height = int_of(json, "height", 0);
    if (sprite.width < 1 || sprite.height < 1 || sprite.width > max_side ||
        sprite.height > max_side) {
        error =
            "The sprite's size must be from 1 to " + std::to_string(max_side) + " pixels each way.";
        return std::nullopt;
    }
    sprite.sheet_columns = std::max(0, int_of(json, "sheet_columns", 0));
    if (json.contains("layers") && json["layers"].is_array()) {
        for (const Json& layer : json["layers"]) {
            if (!layer.is_object()) {
                continue;
            }
            Layer l;
            l.name = layer.value("name", std::string("Layer"));
            l.visible = layer.value("visible", true);
            l.locked = layer.value("locked", false);
            l.opacity =
                static_cast<std::uint8_t>(std::clamp(int_of(layer, "opacity", 255), 0, 255));
            sprite.layers.push_back(std::move(l));
        }
    }
    if (json.contains("frames") && json["frames"].is_array()) {
        for (const Json& frame : json["frames"]) {
            if (frame.is_object()) {
                sprite.frames.push_back({static_cast<std::uint32_t>(
                    std::clamp(int_of(frame, "duration_ms", 100), 1, 65535))});
            }
        }
    }
    if (sprite.layers.empty() || sprite.frames.empty() || sprite.layers.size() > max_layers ||
        sprite.frames.size() > max_frames) {
        error = "A sprite needs from 1 to " + std::to_string(max_layers) + " layers and 1 to " +
                std::to_string(max_frames) + " frames.";
        return std::nullopt;
    }
    if (json.contains("tags") && json["tags"].is_array()) {
        for (const Json& tag : json["tags"]) {
            if (!tag.is_object()) {
                continue;
            }
            Tag t;
            t.name = tag.value("name", std::string("tag"));
            const int from = int_of(tag, "from", 0);
            const int to = int_of(tag, "to", 0);
            if (from < 0 || to < from || static_cast<std::size_t>(to) >= sprite.frames.size()) {
                continue;
            }
            t.from = static_cast<std::size_t>(from);
            t.to = static_cast<std::size_t>(to);
            const std::string direction = tag.value("direction", std::string("forward"));
            t.direction = direction == "reverse"     ? Direction::Reverse
                          : direction == "ping_pong" ? Direction::PingPong
                                                     : Direction::Forward;
            sprite.tags.push_back(std::move(t));
        }
    }
    if (json.contains("palette") && json["palette"].is_array()) {
        for (const Json& color : json["palette"]) {
            if (color.is_string() && sprite.palette.size() < 256) {
                if (const std::optional<Color> c = from_hex(color.get<std::string>())) {
                    sprite.palette.push_back(*c);
                }
            }
        }
    }
    sprite.cels.assign(sprite.layers.size(), std::vector<Cel>(sprite.frames.size()));
    if (json.contains("cels") && json["cels"].is_array()) {
        for (const Json& cel : json["cels"]) {
            if (!cel.is_object()) {
                continue;
            }
            const int layer = int_of(cel, "layer", -1);
            const int frame = int_of(cel, "frame", -1);
            if (layer < 0 || frame < 0 || static_cast<std::size_t>(layer) >= sprite.layers.size() ||
                static_cast<std::size_t>(frame) >= sprite.frames.size()) {
                continue;
            }
            const std::optional<std::vector<std::uint8_t>> bytes =
                from_base64(cel.value("png", std::string()));
            std::string png_error;
            std::optional<Image> image =
                bytes ? engine::assets::decode_png(*bytes, png_error) : std::nullopt;
            if (!image || image->width != static_cast<std::uint32_t>(sprite.width) ||
                image->height != static_cast<std::uint32_t>(sprite.height)) {
                error = "The pixels of layer " + std::to_string(layer + 1) + ", frame " +
                        std::to_string(frame + 1) + " cannot be read.";
                return std::nullopt;
            }
            sprite.cels[static_cast<std::size_t>(layer)][static_cast<std::size_t>(frame)] =
                std::make_shared<const Image>(std::move(*image));
        }
    }
    return sprite;
}

std::optional<Sprite> load(const fs::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "The sprite file cannot be opened.";
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return from_text(text, error);
}

engine::scene2d::SheetInfo sheet_info(const Sprite& sprite) {
    engine::scene2d::SheetInfo sheet;
    sheet.frame_width = static_cast<std::uint32_t>(sprite.width);
    sheet.frame_height = static_cast<std::uint32_t>(sprite.height);
    for (const Frame& frame : sprite.frames) {
        sheet.durations_ms.push_back(frame.duration_ms);
    }
    for (const Tag& tag : sprite.tags) {
        engine::scene2d::SheetTag t;
        t.name = tag.name;
        t.from = static_cast<std::uint32_t>(tag.from);
        t.to = static_cast<std::uint32_t>(tag.to);
        t.direction = static_cast<engine::scene2d::TagDirection>(tag.direction);
        sheet.tags.push_back(std::move(t));
    }
    return sheet;
}

bool save(const Sprite& sprite, const fs::path& path, std::string& error) {
    const auto write = [&](const fs::path& to, const void* data, std::size_t size) {
        const fs::path temporary = fs::path(to).concat(".saving");
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) {
                return false;
            }
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!out) {
                return false;
            }
        }
        std::error_code ec;
        fs::rename(temporary, to, ec);
        if (ec) {
            fs::remove(temporary, ec);
            return false;
        }
        return true;
    };
    const std::string text = to_text(sprite);
    const std::vector<std::uint8_t> png = engine::assets::encode_png(sprite.sheet());
    fs::path sheet = path;
    sheet.replace_extension(".png");
    if (png.empty() || !write(sheet, png.data(), png.size())) {
        error = "The sprite sheet cannot be written.";
        return false;
    }
    fs::path described = path;
    described.replace_extension(".sheet");
    const std::string sheet_text = engine::scene2d::sheet_to_text(sheet_info(sprite));
    if (!write(described, sheet_text.data(), sheet_text.size())) {
        error = "The sheet's description cannot be written.";
        return false;
    }
    if (!write(path, text.data(), text.size())) {
        error = "The sprite file cannot be written.";
        return false;
    }
    return true;
}

std::optional<Sprite> import_sheet(const Image& image, int frame_width, int frame_height,
                                   std::string& error) {
    const int width = static_cast<int>(image.width);
    const int height = static_cast<int>(image.height);
    if (width < 1 || height < 1 ||
        image.rgba.size() !=
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        error = "The picture is empty.";
        return std::nullopt;
    }
    if (frame_width <= 0) {
        frame_width = width;
    }
    if (frame_height <= 0) {
        frame_height = height;
    }
    if (width % frame_width != 0 || height % frame_height != 0) {
        error = "The picture (" + std::to_string(width) + " × " + std::to_string(height) +
                ") does not divide into frames of " + std::to_string(frame_width) + " × " +
                std::to_string(frame_height) + ".";
        return std::nullopt;
    }
    if (frame_width > max_side || frame_height > max_side) {
        error = "Frames can be at most " + std::to_string(max_side) + " pixels each way.";
        return std::nullopt;
    }
    const int across = width / frame_width;
    const int down = height / frame_height;
    if (static_cast<std::size_t>(across) * static_cast<std::size_t>(down) > max_frames) {
        error = "That makes more than " + std::to_string(max_frames) + " frames.";
        return std::nullopt;
    }
    Sprite sprite = Sprite::blank(frame_width, frame_height);
    sprite.sheet_columns = across;
    sprite.frames.clear();
    sprite.cels = {{}};
    for (int row = 0; row < down; ++row) {
        for (int column = 0; column < across; ++column) {
            Image cel = blank_image(frame_width, frame_height);
            for (int y = 0; y < frame_height; ++y) {
                std::copy_n(image.rgba.begin() +
                                static_cast<std::ptrdiff_t>(
                                    offset(image, column * frame_width, row * frame_height + y)),
                            static_cast<std::size_t>(frame_width) * 4,
                            cel.rgba.begin() + static_cast<std::ptrdiff_t>(offset(cel, 0, y)));
            }
            sprite.frames.push_back({});
            sprite.cels[0].push_back(
                all_clear(cel) ? nullptr : std::make_shared<const Image>(std::move(cel)));
        }
    }
    while (sprite.frames.size() > 1 && !sprite.cels[0].back()) {
        sprite.frames.pop_back();
        sprite.cels[0].pop_back();
    }
    const std::vector<Color> used = sprite.used_colors();
    for (const Color color : used) {
        if (color.a == 255 &&
            std::find(sprite.palette.begin(), sprite.palette.end(), color) ==
                sprite.palette.end() &&
            sprite.palette.size() < 256) {
            sprite.palette.push_back(color);
        }
    }
    return sprite;
}

// ---------------------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------------------

Editor::Editor(Sprite sprite) : sprite_(std::move(sprite)) {
    if (sprite_.layers.empty() || sprite_.frames.empty()) {
        sprite_ = Sprite::blank(std::max(sprite_.width, 1), std::max(sprite_.height, 1));
    }
    sprite_.cels.resize(sprite_.layers.size());
    for (auto& layer : sprite_.cels) {
        layer.resize(sprite_.frames.size());
    }
}

void Editor::set_layer(std::size_t layer) noexcept {
    layer_ = std::min(layer, sprite_.layers.size() - 1);
}

void Editor::set_frame(std::size_t frame) noexcept {
    frame_ = std::min(frame, sprite_.frames.size() - 1);
}

Editor::State Editor::state() const {
    return {sprite_, layer_, frame_, selection_};
}

void Editor::restore(const State& state) {
    sprite_ = state.sprite;
    layer_ = std::min(state.layer, sprite_.layers.size() - 1);
    frame_ = std::min(state.frame, sprite_.frames.size() - 1);
    selection_ = state.selection;
}

void Editor::record(std::string label, State before, std::string merge_key) {
    if (!merge_key.empty() && merge_key == merge_key_ && !undo_.empty() && redo_.empty()) {
        undo_.back().after = state();
        undo_.back().serial_after = next_serial_++;
        serial_ = undo_.back().serial_after;
        return;
    }
    Step step;
    step.label = std::move(label);
    step.before = std::move(before);
    step.after = state();
    step.serial_before = serial_;
    step.serial_after = next_serial_++;
    serial_ = step.serial_after;
    undo_.push_back(std::move(step));
    if (undo_.size() > max_undo) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    merge_key_ = std::move(merge_key);
}

std::string Editor::undo_text() const {
    return undo_.empty() ? std::string() : undo_.back().label;
}

std::string Editor::redo_text() const {
    return redo_.empty() ? std::string() : redo_.back().label;
}

bool Editor::undo() {
    if (undo_.empty() || drag_) {
        return false;
    }
    Step step = std::move(undo_.back());
    undo_.pop_back();
    restore(step.before);
    serial_ = step.serial_before;
    redo_.push_back(std::move(step));
    merge_key_.clear();
    return true;
}

bool Editor::redo() {
    if (redo_.empty() || drag_) {
        return false;
    }
    Step step = std::move(redo_.back());
    redo_.pop_back();
    restore(step.after);
    serial_ = step.serial_after;
    undo_.push_back(std::move(step));
    merge_key_.clear();
    return true;
}

bool Editor::editable(std::string& error) const {
    const Layer& layer = sprite_.layers[layer_];
    if (!layer.visible) {
        error = "\"" + layer.name + "\" is hidden. Show it to draw on it.";
        return false;
    }
    if (layer.locked) {
        error = "\"" + layer.name + "\" is locked. Unlock it to draw on it.";
        return false;
    }
    return true;
}

Image& Editor::cel_for_writing() {
    Cel& cel = sprite_.cels[layer_][frame_];
    auto copy = cel ? std::make_shared<Image>(*cel)
                    : std::make_shared<Image>(blank_image(sprite_.width, sprite_.height));
    Image& pixels = *copy;
    cel = std::move(copy);
    return pixels;
}

bool Editor::begin(Tool tool, Color color, const Brush& brush, int x, int y, std::string& error) {
    if (drag_) {
        end();
    }
    if (tool != Tool::Select && !editable(error)) {
        return false;
    }
    Drag started;
    started.tool = tool;
    started.color = tool == Tool::Eraser ? Color{} : color;
    started.brush = brush;
    started.brush.size = std::clamp(brush.size, 1, 32);
    started.x0 = started.last_x = x;
    started.y0 = started.last_y = y;
    started.before = state();
    started.base = sprite_.cels[layer_][frame_];
    if (tool == Tool::Move) {
        const Rect from =
            selection_.empty() ? Rect{0, 0, sprite_.width, sprite_.height} : selection_;
        Image lifted = blank_image(from.width, from.height);
        if (started.base) {
            for (int py = 0; py < from.height; ++py) {
                for (int px = 0; px < from.width; ++px) {
                    put(lifted, px, py, get(*started.base, from.x + px, from.y + py));
                }
            }
        }
        started.lifted = std::make_shared<const Image>(std::move(lifted));
        started.lifted_from = from;
    }
    started.points.push_back({x, y});
    drag_ = std::move(started);
    redraw_drag();
    return true;
}

void Editor::drag(int x, int y) {
    if (!drag_ || (x == drag_->last_x && y == drag_->last_y)) {
        return;
    }
    if (drag_->tool == Tool::Pencil || drag_->tool == Tool::Eraser) {
        line(drag_->last_x, drag_->last_y, x, y, [&](int px, int py) {
            if (drag_->points.empty() || drag_->points.back() != std::array<int, 2>{px, py}) {
                drag_->points.push_back({px, py});
            }
        });
    }
    drag_->last_x = x;
    drag_->last_y = y;
    redraw_drag();
}

void Editor::redraw_drag() {
    if (!drag_) {
        return;
    }
    Drag& d = *drag_;
    if (d.tool == Tool::Select) {
        const Rect r = rect_between(d.x0, d.y0, d.last_x, d.last_y);
        const int x0 = std::clamp(r.x, 0, sprite_.width);
        const int y0 = std::clamp(r.y, 0, sprite_.height);
        const int x1 = std::clamp(r.x + r.width, 0, sprite_.width);
        const int y1 = std::clamp(r.y + r.height, 0, sprite_.height);
        selection_ = {x0, y0, x1 - x0, y1 - y0};
        return;
    }
    Image image = d.base ? *d.base : blank_image(sprite_.width, sprite_.height);
    const Rect clip = d.before.selection;
    const int w = sprite_.width;
    const int h = sprite_.height;
    // Shading reads the picture as it was before the stroke, so each pixel moves one step.
    const Image* before = d.base ? &*d.base : nullptr;
    const std::vector<Color>& palette = sprite_.palette;
    const auto palette_index = [&palette](Color c) -> std::ptrdiff_t {
        const auto found = std::find(palette.begin(), palette.end(), c);
        return found == palette.end() ? -1 : found - palette.begin();
    };
    const std::ptrdiff_t towards = palette_index(d.color);
    // One pixel of colour, kept inside the selection, mirrored as asked.
    const auto dot = [&](int x, int y) {
        const auto one = [&](int px, int py) {
            if (!clip.empty() && !clip.contains(px, py)) {
                return;
            }
            if (d.brush.dither && ((px + py) % 2) != 0) {
                return; // the other squares of the checkerboard
            }
            if (d.brush.shade && d.tool != Tool::Eraser) {
                if (before == nullptr || px < 0 || py < 0 || px >= w || py >= h) {
                    return;
                }
                const Color now = get(*before, px, py);
                const std::ptrdiff_t at = palette_index(now);
                if (now.a == 0 || at < 0 || towards < 0 || at == towards) {
                    return;
                }
                put(image, px, py, palette[static_cast<std::size_t>(at + (towards > at ? 1 : -1))]);
                return;
            }
            put(image, px, py, d.color);
        };
        one(x, y);
        if (d.brush.mirror_x) {
            one(w - 1 - x, y);
        }
        if (d.brush.mirror_y) {
            one(x, h - 1 - y);
        }
        if (d.brush.mirror_x && d.brush.mirror_y) {
            one(w - 1 - x, h - 1 - y);
        }
    };
    // The brush centred on a pixel.
    const auto stamp = [&](int x, int y) {
        const int size = d.brush.size;
        const int start = -(size - 1) / 2;
        for (int by = start; by < start + size; ++by) {
            for (int bx = start; bx < start + size; ++bx) {
                dot(x + bx, y + by);
            }
        }
    };
    switch (d.tool) {
    case Tool::Pencil:
    case Tool::Eraser: {
        const bool thin = d.brush.size == 1 && d.brush.pixel_perfect;
        for (const auto& point : thin ? pixel_perfect(d.points) : d.points) {
            stamp(point[0], point[1]);
        }
        break;
    }
    case Tool::Line:
        line(d.x0, d.y0, d.last_x, d.last_y, stamp);
        break;
    case Tool::Rectangle:
    case Tool::FilledRectangle: {
        const Rect r = rect_between(d.x0, d.y0, d.last_x, d.last_y);
        for (int y = r.y; y < r.y + r.height; ++y) {
            for (int x = r.x; x < r.x + r.width; ++x) {
                const bool edge =
                    x == r.x || y == r.y || x == r.x + r.width - 1 || y == r.y + r.height - 1;
                if (edge || d.tool == Tool::FilledRectangle) {
                    stamp(x, y);
                }
            }
        }
        break;
    }
    case Tool::Ellipse:
        ellipse(d.x0, d.y0, d.last_x, d.last_y, stamp);
        break;
    case Tool::FilledEllipse: {
        // Each row from its leftmost to its rightmost pixel of the outline.
        const Rect r = rect_between(d.x0, d.y0, d.last_x, d.last_y);
        std::vector<std::array<int, 2>> spans(
            static_cast<std::size_t>(r.height),
            {std::numeric_limits<int>::max(), std::numeric_limits<int>::min()});
        ellipse(d.x0, d.y0, d.last_x, d.last_y, [&](int x, int y) {
            if (y < r.y || y >= r.y + r.height) {
                return;
            }
            auto& span = spans[static_cast<std::size_t>(y - r.y)];
            span[0] = std::min(span[0], x);
            span[1] = std::max(span[1], x);
        });
        for (int row = 0; row < r.height; ++row) {
            const auto& span = spans[static_cast<std::size_t>(row)];
            for (int x = span[0]; x <= span[1]; ++x) {
                stamp(x, r.y + row);
            }
        }
        break;
    }
    case Tool::Fill: {
        if (!inside(image, d.x0, d.y0) || (!clip.empty() && !clip.contains(d.x0, d.y0))) {
            break;
        }
        const Color target = get(image, d.x0, d.y0);
        if (target == d.color) {
            break;
        }
        const auto fillable = [&](int x, int y) {
            return inside(image, x, y) && (clip.empty() || clip.contains(x, y)) &&
                   get(image, x, y) == target;
        };
        if (d.brush.fill_all) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    if (fillable(x, y)) {
                        put(image, x, y, d.color);
                    }
                }
            }
            break;
        }
        std::vector<std::array<int, 2>> todo{{d.x0, d.y0}};
        put(image, d.x0, d.y0, d.color);
        while (!todo.empty()) {
            const auto [x, y] = todo.back();
            todo.pop_back();
            for (const auto& [nx, ny] :
                 {std::array<int, 2>{x + 1, y}, std::array<int, 2>{x - 1, y},
                  std::array<int, 2>{x, y + 1}, std::array<int, 2>{x, y - 1}}) {
                if (fillable(nx, ny)) {
                    put(image, nx, ny, d.color);
                    todo.push_back({nx, ny});
                }
            }
        }
        break;
    }
    case Tool::Move: {
        const int dx = d.last_x - d.x0;
        const int dy = d.last_y - d.y0;
        const Rect from = d.lifted_from;
        for (int y = from.y; y < from.y + from.height; ++y) {
            for (int x = from.x; x < from.x + from.width; ++x) {
                put(image, x, y, {});
            }
        }
        for (int y = 0; y < from.height; ++y) {
            for (int x = 0; x < from.width; ++x) {
                const Color c = get(*d.lifted, x, y);
                if (c.a != 0) {
                    put(image, from.x + x + dx, from.y + y + dy, c);
                }
            }
        }
        if (!d.before.selection.empty()) {
            selection_ = {from.x + dx, from.y + dy, from.width, from.height};
        }
        break;
    }
    case Tool::Select:
        break;
    }
    sprite_.cels[layer_][frame_] = std::make_shared<const Image>(std::move(image));
}

bool Editor::end() {
    if (!drag_) {
        return false;
    }
    Drag d = std::move(*drag_);
    drag_.reset();
    if (d.tool == Tool::Select) {
        return false;
    }
    const Cel& now = sprite_.cels[layer_][frame_];
    const bool unchanged = d.base ? (now->rgba == d.base->rgba) : all_clear(*now);
    if (unchanged) {
        sprite_.cels[layer_][frame_] = d.base;
        selection_ = d.before.selection;
        return false;
    }
    static constexpr std::string_view labels[] = {"Draw with the pencil",
                                                  "Erase",
                                                  "Draw a line",
                                                  "Draw a rectangle",
                                                  "Draw a rectangle",
                                                  "Draw an ellipse",
                                                  "Draw an ellipse",
                                                  "Fill",
                                                  "Select",
                                                  "Move pixels"};
    record(std::string(labels[static_cast<std::size_t>(d.tool)]), std::move(d.before));
    return true;
}

// ----- Selection -----

void Editor::select(const Rect& rect) {
    const int x0 = std::clamp(rect.x, 0, sprite_.width);
    const int y0 = std::clamp(rect.y, 0, sprite_.height);
    const int x1 = std::clamp(rect.x + rect.width, 0, sprite_.width);
    const int y1 = std::clamp(rect.y + rect.height, 0, sprite_.height);
    selection_ = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Editor::select_all() {
    selection_ = {0, 0, sprite_.width, sprite_.height};
}

void Editor::deselect() {
    selection_ = {};
}

bool Editor::clear(std::string& error) {
    if (!editable(error)) {
        return false;
    }
    const Cel& cel = sprite_.cels[layer_][frame_];
    if (!cel) {
        return false;
    }
    const Rect area = selection_.empty() ? Rect{0, 0, sprite_.width, sprite_.height} : selection_;
    State before = state();
    Image& image = cel_for_writing();
    bool changed = false;
    for (int y = area.y; y < area.y + area.height; ++y) {
        for (int x = area.x; x < area.x + area.width; ++x) {
            changed = changed || get(image, x, y).a != 0;
            put(image, x, y, {});
        }
    }
    if (!changed) {
        restore(before);
        return false;
    }
    record("Clear", std::move(before));
    return true;
}

bool Editor::flip(bool horizontal, std::string& error) {
    if (!editable(error)) {
        return false;
    }
    if (!sprite_.cels[layer_][frame_]) {
        return false;
    }
    const Rect area = selection_.empty() ? Rect{0, 0, sprite_.width, sprite_.height} : selection_;
    State before = state();
    Image& image = cel_for_writing();
    const Image original = image;
    for (int y = 0; y < area.height; ++y) {
        for (int x = 0; x < area.width; ++x) {
            const int sx = horizontal ? area.width - 1 - x : x;
            const int sy = horizontal ? y : area.height - 1 - y;
            put(image, area.x + x, area.y + y, get(original, area.x + sx, area.y + sy));
        }
    }
    if (image.rgba == original.rgba) {
        restore(before);
        return false;
    }
    record(horizontal ? "Flip horizontally" : "Flip vertically", std::move(before));
    return true;
}

void Editor::copy() {
    const Rect area = selection_.empty() ? Rect{0, 0, sprite_.width, sprite_.height} : selection_;
    Image out = blank_image(area.width, area.height);
    for (int y = 0; y < area.height; ++y) {
        for (int x = 0; x < area.width; ++x) {
            put(out, x, y, sprite_.pixel(layer_, frame_, area.x + x, area.y + y));
        }
    }
    clipboard_ = std::make_shared<const Image>(std::move(out));
}

bool Editor::cut(std::string& error) {
    copy();
    return clear(error);
}

bool Editor::paste(std::string& error) {
    if (!clipboard_ || !editable(error)) {
        return false;
    }
    State before = state();
    const int ox = selection_.empty() ? 0 : selection_.x;
    const int oy = selection_.empty() ? 0 : selection_.y;
    Image& image = cel_for_writing();
    for (int y = 0; y < static_cast<int>(clipboard_->height); ++y) {
        for (int x = 0; x < static_cast<int>(clipboard_->width); ++x) {
            const Color c = get(*clipboard_, x, y);
            if (c.a != 0) {
                put(image, ox + x, oy + y, c);
            }
        }
    }
    select({ox, oy, static_cast<int>(clipboard_->width), static_cast<int>(clipboard_->height)});
    record("Paste", std::move(before));
    return true;
}

// ----- Layers -----

bool Editor::add_layer(std::string& error) {
    if (sprite_.layers.size() >= max_layers) {
        error = "A sprite can have at most " + std::to_string(max_layers) + " layers.";
        return false;
    }
    State before = state();
    const std::size_t at = layer_ + 1;
    sprite_.layers.insert(sprite_.layers.begin() + static_cast<std::ptrdiff_t>(at),
                          Layer{next_layer_name(sprite_)});
    sprite_.cels.insert(sprite_.cels.begin() + static_cast<std::ptrdiff_t>(at),
                        std::vector<Cel>(sprite_.frames.size()));
    layer_ = at;
    record("Add " + sprite_.layers[at].name, std::move(before));
    return true;
}

bool Editor::duplicate_layer(std::string& error) {
    if (sprite_.layers.size() >= max_layers) {
        error = "A sprite can have at most " + std::to_string(max_layers) + " layers.";
        return false;
    }
    State before = state();
    const std::size_t at = layer_ + 1;
    Layer copy = sprite_.layers[layer_];
    copy.name += " copy";
    sprite_.layers.insert(sprite_.layers.begin() + static_cast<std::ptrdiff_t>(at), copy);
    std::vector<Cel> cels = sprite_.cels[layer_];
    sprite_.cels.insert(sprite_.cels.begin() + static_cast<std::ptrdiff_t>(at), std::move(cels));
    layer_ = at;
    record("Duplicate " + sprite_.layers[at - 1].name, std::move(before));
    return true;
}

bool Editor::remove_layer(std::string& error) {
    if (sprite_.layers.size() <= 1) {
        error = "A sprite needs at least one layer.";
        return false;
    }
    State before = state();
    const std::string name = sprite_.layers[layer_].name;
    sprite_.layers.erase(sprite_.layers.begin() + static_cast<std::ptrdiff_t>(layer_));
    sprite_.cels.erase(sprite_.cels.begin() + static_cast<std::ptrdiff_t>(layer_));
    layer_ = layer_ > 0 ? layer_ - 1 : 0;
    record("Delete " + name, std::move(before));
    return true;
}

bool Editor::move_layer(bool up, std::string& error) {
    if (up ? layer_ + 1 >= sprite_.layers.size() : layer_ == 0) {
        error = up ? "It is the top layer already." : "It is the bottom layer already.";
        return false;
    }
    State before = state();
    const std::size_t other = up ? layer_ + 1 : layer_ - 1;
    std::swap(sprite_.layers[layer_], sprite_.layers[other]);
    std::swap(sprite_.cels[layer_], sprite_.cels[other]);
    layer_ = other;
    record((up ? "Move up " : "Move down ") + sprite_.layers[other].name, std::move(before));
    return true;
}

bool Editor::merge_down(std::string& error) {
    if (layer_ == 0) {
        error = "There is no layer below to merge into.";
        return false;
    }
    const Layer& top = sprite_.layers[layer_];
    if (sprite_.layers[layer_ - 1].locked) {
        error = "\"" + sprite_.layers[layer_ - 1].name + "\" is locked.";
        return false;
    }
    State before = state();
    const std::string name = top.name;
    for (std::size_t frame = 0; frame < sprite_.frames.size(); ++frame) {
        const Cel& upper = sprite_.cels[layer_][frame];
        if (!upper || !top.visible) {
            continue;
        }
        const Cel& lower = sprite_.cels[layer_ - 1][frame];
        Image merged = lower ? *lower : blank_image(sprite_.width, sprite_.height);
        for (int y = 0; y < sprite_.height; ++y) {
            for (int x = 0; x < sprite_.width; ++x) {
                const Color src = get(*upper, x, y);
                if (src.a != 0) {
                    put(merged, x, y, blend(get(merged, x, y), src, top.opacity));
                }
            }
        }
        sprite_.cels[layer_ - 1][frame] = std::make_shared<const Image>(std::move(merged));
    }
    sprite_.layers.erase(sprite_.layers.begin() + static_cast<std::ptrdiff_t>(layer_));
    sprite_.cels.erase(sprite_.cels.begin() + static_cast<std::ptrdiff_t>(layer_));
    --layer_;
    record("Merge " + name + " down", std::move(before));
    return true;
}

bool Editor::rename_layer(std::size_t layer, std::string_view name, std::string& error) {
    if (layer >= sprite_.layers.size()) {
        return false;
    }
    if (name.empty()) {
        error = "A layer needs a name.";
        return false;
    }
    if (sprite_.layers[layer].name == name) {
        return false;
    }
    State before = state();
    sprite_.layers[layer].name = std::string(name);
    record("Rename layer", std::move(before), "layer_name:" + std::to_string(layer));
    return true;
}

bool Editor::set_layer_visible(std::size_t layer, bool visible) {
    if (layer >= sprite_.layers.size() || sprite_.layers[layer].visible == visible) {
        return false;
    }
    State before = state();
    sprite_.layers[layer].visible = visible;
    record((visible ? "Show " : "Hide ") + sprite_.layers[layer].name, std::move(before));
    return true;
}

bool Editor::set_layer_locked(std::size_t layer, bool locked) {
    if (layer >= sprite_.layers.size() || sprite_.layers[layer].locked == locked) {
        return false;
    }
    State before = state();
    sprite_.layers[layer].locked = locked;
    record((locked ? "Lock " : "Unlock ") + sprite_.layers[layer].name, std::move(before));
    return true;
}

bool Editor::set_layer_opacity(std::size_t layer, int opacity) {
    opacity = std::clamp(opacity, 0, 255);
    if (layer >= sprite_.layers.size() || sprite_.layers[layer].opacity == opacity) {
        return false;
    }
    State before = state();
    sprite_.layers[layer].opacity = static_cast<std::uint8_t>(opacity);
    record("Change opacity of " + sprite_.layers[layer].name, std::move(before),
           "layer_opacity:" + std::to_string(layer));
    return true;
}

// ----- Frames -----

bool Editor::add_frame(bool copy, std::string& error) {
    if (sprite_.frames.size() >= max_frames) {
        error = "A sprite can have at most " + std::to_string(max_frames) + " frames.";
        return false;
    }
    State before = state();
    const std::size_t at = frame_ + 1;
    sprite_.frames.insert(sprite_.frames.begin() + static_cast<std::ptrdiff_t>(at),
                          sprite_.frames[frame_]);
    for (auto& layer : sprite_.cels) {
        layer.insert(layer.begin() + static_cast<std::ptrdiff_t>(at),
                     copy ? layer[frame_] : nullptr);
    }
    for (Tag& tag : sprite_.tags) {
        if (tag.from > frame_) {
            ++tag.from;
            ++tag.to;
        } else if (tag.to >= frame_) {
            ++tag.to;
        }
    }
    frame_ = at;
    record(copy ? "Duplicate frame" : "Add frame", std::move(before));
    return true;
}

bool Editor::remove_frame(std::string& error) {
    if (sprite_.frames.size() <= 1) {
        error = "A sprite needs at least one frame.";
        return false;
    }
    State before = state();
    const std::size_t at = frame_;
    sprite_.frames.erase(sprite_.frames.begin() + static_cast<std::ptrdiff_t>(at));
    for (auto& layer : sprite_.cels) {
        layer.erase(layer.begin() + static_cast<std::ptrdiff_t>(at));
    }
    std::vector<Tag> kept;
    for (Tag tag : sprite_.tags) {
        if (tag.from > at) {
            --tag.from;
            --tag.to;
        } else if (tag.to >= at) {
            if (tag.from == tag.to) {
                continue;
            }
            --tag.to;
        }
        kept.push_back(std::move(tag));
    }
    sprite_.tags = std::move(kept);
    frame_ = std::min(at, sprite_.frames.size() - 1);
    record("Delete frame " + std::to_string(at + 1), std::move(before));
    return true;
}

bool Editor::move_frame(bool earlier, std::string& error) {
    if (earlier ? frame_ == 0 : frame_ + 1 >= sprite_.frames.size()) {
        error = earlier ? "It is the first frame already." : "It is the last frame already.";
        return false;
    }
    State before = state();
    const std::size_t other = earlier ? frame_ - 1 : frame_ + 1;
    std::swap(sprite_.frames[frame_], sprite_.frames[other]);
    for (auto& layer : sprite_.cels) {
        std::swap(layer[frame_], layer[other]);
    }
    frame_ = other;
    record("Move frame", std::move(before));
    return true;
}

bool Editor::set_frame_duration(std::size_t frame, int duration_ms, std::string& error, bool all) {
    if (frame >= sprite_.frames.size()) {
        return false;
    }
    if (duration_ms < 1 || duration_ms > 65535) {
        error = "A frame shows for 1 to 65,535 milliseconds.";
        return false;
    }
    const auto ms = static_cast<std::uint32_t>(duration_ms);
    const bool same = all ? std::all_of(sprite_.frames.begin(), sprite_.frames.end(),
                                        [&](const Frame& f) { return f.duration_ms == ms; })
                          : sprite_.frames[frame].duration_ms == ms;
    if (same) {
        return false;
    }
    State before = state();
    if (all) {
        for (Frame& f : sprite_.frames) {
            f.duration_ms = ms;
        }
    } else {
        sprite_.frames[frame].duration_ms = ms;
    }
    record(all ? std::string("Change duration of every frame")
               : "Change duration of frame " + std::to_string(frame + 1),
           std::move(before),
           all ? std::string("duration:all") : "duration:" + std::to_string(frame));
    return true;
}

// ----- Tags -----

std::optional<std::size_t> Editor::add_tag(std::string_view name, std::size_t from, std::size_t to,
                                           std::string& error) {
    if (from > to || to >= sprite_.frames.size()) {
        error = "A tag covers frames that the sprite has, the first before the last.";
        return std::nullopt;
    }
    State before = state();
    Tag tag;
    tag.name = unique_tag_name(sprite_, name);
    tag.from = from;
    tag.to = to;
    sprite_.tags.push_back(tag);
    record("Add tag " + tag.name, std::move(before));
    return sprite_.tags.size() - 1;
}

bool Editor::set_tag(std::size_t tag, const Tag& value, std::string& error) {
    if (tag >= sprite_.tags.size()) {
        return false;
    }
    if (value.from > value.to || value.to >= sprite_.frames.size()) {
        error = "A tag covers frames that the sprite has, the first before the last.";
        return false;
    }
    if (value.name.empty()) {
        error = "A tag needs a name.";
        return false;
    }
    Tag next = value;
    next.name = unique_tag_name(sprite_, value.name, tag);
    const Tag& now = sprite_.tags[tag];
    if (now.name == next.name && now.from == next.from && now.to == next.to &&
        now.direction == next.direction) {
        return false;
    }
    State before = state();
    sprite_.tags[tag] = std::move(next);
    record("Change tag " + sprite_.tags[tag].name, std::move(before), "tag:" + std::to_string(tag));
    return true;
}

bool Editor::remove_tag(std::size_t tag) {
    if (tag >= sprite_.tags.size()) {
        return false;
    }
    State before = state();
    const std::string name = sprite_.tags[tag].name;
    sprite_.tags.erase(sprite_.tags.begin() + static_cast<std::ptrdiff_t>(tag));
    record("Delete tag " + name, std::move(before));
    return true;
}

// ----- Palette and canvas -----

bool Editor::add_color(Color color) {
    if (sprite_.palette.size() >= 256 ||
        std::find(sprite_.palette.begin(), sprite_.palette.end(), color) != sprite_.palette.end()) {
        return false;
    }
    State before = state();
    sprite_.palette.push_back(color);
    record("Add a colour", std::move(before));
    return true;
}

bool Editor::set_color(std::size_t index, Color color) {
    if (index >= sprite_.palette.size() || sprite_.palette[index] == color) {
        return false;
    }
    State before = state();
    sprite_.palette[index] = color;
    record("Change a colour", std::move(before), "color:" + std::to_string(index));
    return true;
}

bool Editor::remove_color(std::size_t index) {
    if (index >= sprite_.palette.size()) {
        return false;
    }
    State before = state();
    sprite_.palette.erase(sprite_.palette.begin() + static_cast<std::ptrdiff_t>(index));
    record("Remove a colour", std::move(before));
    return true;
}

bool Editor::palette_from_sprite(std::string& error) {
    std::vector<Color> used = sprite_.used_colors();
    if (used.empty()) {
        error = "Nothing is drawn yet.";
        return false;
    }
    if (used == sprite_.palette) {
        return false;
    }
    State before = state();
    sprite_.palette = std::move(used);
    record("Palette from the sprite", std::move(before));
    return true;
}

bool Editor::resize(int width, int height, Anchor anchor, std::string& error) {
    if (width < 1 || height < 1 || width > max_side || height > max_side) {
        error = "A sprite is from 1 to " + std::to_string(max_side) + " pixels each way.";
        return false;
    }
    if (width == sprite_.width && height == sprite_.height) {
        return false;
    }
    State before = state();
    const int dx = anchor == Anchor::Center ? (width - sprite_.width) / 2 : 0;
    const int dy = anchor == Anchor::Center ? (height - sprite_.height) / 2 : 0;
    for (auto& layer : sprite_.cels) {
        for (Cel& cel : layer) {
            if (!cel) {
                continue;
            }
            Image resized = blank_image(width, height);
            for (int y = 0; y < sprite_.height; ++y) {
                for (int x = 0; x < sprite_.width; ++x) {
                    put(resized, x + dx, y + dy, get(*cel, x, y));
                }
            }
            cel = all_clear(resized) ? nullptr : std::make_shared<const Image>(std::move(resized));
        }
    }
    sprite_.width = width;
    sprite_.height = height;
    selection_ = {};
    record("Resize the canvas", std::move(before));
    return true;
}

bool Editor::set_sheet_columns(int columns, std::string& error) {
    if (columns < 0 || static_cast<std::size_t>(columns) > max_frames) {
        error =
            "Frames across must be from 0 (as many as fit) to " + std::to_string(max_frames) + ".";
        return false;
    }
    if (sprite_.sheet_columns == columns) {
        return false;
    }
    State before = state();
    sprite_.sheet_columns = columns;
    record("Change frames across", std::move(before), "sheet_columns");
    return true;
}

} // namespace editor::sprite
