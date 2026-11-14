#include "engine/assets/aseprite.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

// stb_image's zlib decoder only: cels are compressed with zlib.
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h> // NOLINT
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace engine::assets {

namespace {

/// Little-endian reading, refusing to read past the end.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t at() const noexcept { return at_; }
    void seek(std::size_t to) {
        if (to > bytes_.size()) {
            ok_ = false;
            to = bytes_.size();
        }
        at_ = to;
    }
    void skip(std::size_t n) { seek(at_ + n); }
    std::uint8_t u8() { return ok_ && at_ < bytes_.size() ? bytes_[at_++] : fail<std::uint8_t>(); }
    std::uint16_t u16() {
        const std::uint16_t lo = u8();
        const std::uint16_t hi = u8();
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() {
        const std::uint32_t lo = u16();
        const std::uint32_t hi = u16();
        return lo | (hi << 16U);
    }
    std::string text() {
        const std::uint16_t n = u16();
        std::string out;
        for (std::uint16_t i = 0; i < n && ok_; ++i) {
            out += static_cast<char>(u8());
        }
        return out;
    }
    [[nodiscard]] std::span<const std::uint8_t> rest(std::size_t end) const {
        return end > at_ && end <= bytes_.size() ? bytes_.subspan(at_, end - at_)
                                                 : std::span<const std::uint8_t>{};
    }

private:
    template <typename T>
    T fail() {
        ok_ = false;
        return T{};
    }
    std::span<const std::uint8_t> bytes_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

struct Layer {
    bool visible = true;
    bool image = true; ///< A normal layer, not a group or tilemap.
    std::uint8_t opacity = 255;
};

struct Cel {
    std::size_t layer = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint8_t opacity = 255;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;    ///< Converted to RGBA.
    std::optional<std::size_t> linked; ///< The frame whose cel on this layer it reuses.
};

/// One channel of `a` (with `alpha`, 0 to 255) over `b` (with `b_alpha`): Porter-Duff
/// "over", `out_alpha` being the result's alpha.
std::uint8_t over(std::uint8_t a, std::uint8_t b, std::uint32_t alpha, std::uint32_t b_alpha,
                  std::uint32_t out_alpha) {
    if (out_alpha == 0) {
        return 0;
    }
    const std::uint32_t mine = std::uint32_t{a} * alpha;
    const std::uint32_t theirs = std::uint32_t{b} * b_alpha * (255U - alpha) / 255U;
    return static_cast<std::uint8_t>(std::min<std::uint32_t>((mine + theirs) / out_alpha, 255));
}

} // namespace

std::optional<AsepriteSheet> read_aseprite(std::span<const std::uint8_t> bytes,
                                           std::string& error) {
    Reader in(bytes);
    in.u32(); // file size
    if (in.u16() != 0xA5E0 || !in.ok()) {
        error = "This is not an Aseprite file.";
        return std::nullopt;
    }
    const std::uint16_t frames = in.u16();
    const std::uint16_t width = in.u16();
    const std::uint16_t height = in.u16();
    const std::uint16_t depth = in.u16();
    const std::uint32_t flags = in.u32();
    in.skip(2 + 4 + 4); // speed (old), two reserved words
    const std::uint8_t transparent = in.u8();
    in.seek(128);
    if (!in.ok() || frames == 0 || width == 0 || height == 0 ||
        (depth != 32 && depth != 16 && depth != 8) || frames > 4096 ||
        std::uint64_t{width} * height * frames > 64ULL * 1024 * 1024) {
        error = "This Aseprite file is damaged or too large.";
        return std::nullopt;
    }
    const bool layer_opacity = (flags & 1U) != 0;
    std::array<std::array<std::uint8_t, 4>, 256> palette{};
    std::vector<Layer> layers;
    std::vector<std::vector<Cel>> cels(frames);
    AsepriteSheet out;
    out.frame_width = width;
    out.frame_height = height;

    const auto to_rgba = [&](std::span<const std::uint8_t> pixels, std::uint32_t count) {
        std::vector<std::uint8_t> rgba(std::size_t{count} * 4, 0);
        const std::size_t size = depth / 8U;
        if (pixels.size() < count * size) {
            return std::vector<std::uint8_t>{};
        }
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint8_t* p = pixels.data() + (i * size);
            std::uint8_t* q = rgba.data() + (i * 4);
            if (depth == 32) {
                std::copy_n(p, 4, q);
            } else if (depth == 16) {
                q[0] = q[1] = q[2] = p[0];
                q[3] = p[1];
            } else if (p[0] != transparent) {
                std::copy_n(palette.at(p[0]).data(), 4, q);
            }
        }
        return rgba;
    };

    for (std::uint16_t f = 0; f < frames && in.ok(); ++f) {
        const std::size_t start = in.at();
        const std::uint32_t frame_bytes = in.u32();
        if (in.u16() != 0xF1FA) {
            error = "Frame " + std::to_string(f + 1) + " of this Aseprite file is damaged.";
            return std::nullopt;
        }
        const std::uint16_t old_chunks = in.u16();
        const std::uint16_t duration = in.u16();
        in.skip(2);
        const std::uint32_t new_chunks = in.u32();
        out.durations_ms.push_back(std::max<std::uint32_t>(duration, 1));
        const std::uint32_t chunks = new_chunks != 0 ? new_chunks : old_chunks;
        for (std::uint32_t c = 0; c < chunks && in.ok(); ++c) {
            const std::size_t chunk_start = in.at();
            const std::uint32_t size = in.u32();
            const std::uint16_t type = in.u16();
            const std::size_t end = chunk_start + size;
            if (size < 6 || end > start + frame_bytes) {
                error = "A part of this Aseprite file is damaged.";
                return std::nullopt;
            }
            if (type == 0x2004) { // layer
                Layer layer;
                const std::uint16_t layer_flags = in.u16();
                const std::uint16_t kind = in.u16();
                in.skip(2 + 2 + 2 + 2); // child level, default size, blend mode
                layer.opacity = layer_opacity ? in.u8() : 255;
                layer.visible = (layer_flags & 1U) != 0;
                layer.image = kind == 0;
                layers.push_back(layer);
            } else if (type == 0x2019) { // palette
                const std::uint32_t count = in.u32();
                const std::uint32_t first = in.u32();
                const std::uint32_t last = in.u32();
                in.skip(8);
                for (std::uint32_t i = first; i <= last && i < 256 && i - first < count; ++i) {
                    const std::uint16_t entry_flags = in.u16();
                    palette.at(i) = {in.u8(), in.u8(), in.u8(), in.u8()};
                    if ((entry_flags & 1U) != 0) {
                        in.text();
                    }
                }
            } else if (type == 0x2005) { // cel
                Cel cel;
                cel.layer = in.u16();
                cel.x = in.i16();
                cel.y = in.i16();
                cel.opacity = in.u8();
                const std::uint16_t kind = in.u16();
                in.skip(2 + 5); // z-index, reserved
                if (kind == 1) {
                    cel.linked = in.u16();
                } else if (kind == 0 || kind == 2) {
                    cel.width = in.u16();
                    cel.height = in.u16();
                    const std::uint32_t count = cel.width * cel.height;
                    const std::span<const std::uint8_t> data = in.rest(end);
                    if (kind == 0) {
                        cel.rgba = to_rgba(data, count);
                    } else {
                        int got = 0;
                        const std::unique_ptr<char, decltype(&std::free)> raw(
                            stbi_zlib_decode_malloc_guesssize_headerflag(
                                reinterpret_cast<const char*>(data.data()), // NOLINT
                                static_cast<int>(data.size()),
                                static_cast<int>(count * (depth / 8U)), &got, 1),
                            &std::free);
                        if (raw != nullptr && got >= 0) {
                            cel.rgba = to_rgba(std::span(reinterpret_cast<const std::uint8_t*>(
                                                             raw.get()), // NOLINT
                                                         static_cast<std::size_t>(got)),
                                               count);
                        }
                    }
                    if (cel.rgba.empty() && count > 0) {
                        error = "A picture in this Aseprite file is damaged.";
                        return std::nullopt;
                    }
                } else {
                    in.seek(end); // tilemaps: passed over
                    continue;
                }
                cels[f].push_back(std::move(cel));
            } else if (type == 0x2018) { // tags
                const std::uint16_t count = in.u16();
                in.skip(8);
                for (std::uint16_t i = 0; i < count && in.ok(); ++i) {
                    AsepriteTag tag;
                    tag.from = in.u16();
                    tag.to = in.u16();
                    tag.direction = in.u8();
                    in.skip(2 + 6 + 3 + 1); // repeat, reserved, old colour, extra
                    tag.name = in.text();
                    if (tag.from <= tag.to && tag.to < frames) {
                        out.tags.push_back(std::move(tag));
                    }
                }
            }
            in.seek(end);
        }
        in.seek(start + frame_bytes);
    }
    if (!in.ok()) {
        error = "This Aseprite file ends too soon.";
        return std::nullopt;
    }

    // Each frame, layer by layer from the bottom, into the strip.
    out.sheet.width = std::uint32_t{width} * frames;
    out.sheet.height = height;
    out.sheet.rgba.assign(std::size_t{out.sheet.width} * height * 4, 0);
    for (std::size_t f = 0; f < frames; ++f) {
        std::vector<const Cel*> here(layers.size(), nullptr);
        for (const Cel& cel : cels[f]) {
            if (cel.layer < here.size()) {
                const Cel* shown = &cel;
                if (cel.linked && *cel.linked < frames) {
                    for (const Cel& other : cels[*cel.linked]) {
                        if (other.layer == cel.layer && !other.linked) {
                            shown = &other;
                        }
                    }
                }
                here[cel.layer] = shown;
            }
        }
        for (std::size_t l = 0; l < layers.size(); ++l) {
            const Cel* cel = here[l];
            if (cel == nullptr || cel->linked || !layers[l].visible || !layers[l].image) {
                continue;
            }
            const std::uint32_t alpha_scale = std::uint32_t{layers[l].opacity} * cel->opacity;
            for (std::uint32_t y = 0; y < cel->height; ++y) {
                const std::int64_t ty = std::int64_t{cel->y} + y;
                if (ty < 0 || ty >= height) {
                    continue;
                }
                for (std::uint32_t x = 0; x < cel->width; ++x) {
                    const std::int64_t tx = std::int64_t{cel->x} + x;
                    if (tx < 0 || tx >= width) {
                        continue;
                    }
                    const std::uint8_t* src =
                        cel->rgba.data() + ((std::size_t{y} * cel->width + x) * 4);
                    std::uint8_t* dst =
                        out.sheet.rgba.data() + ((static_cast<std::size_t>(ty) * out.sheet.width +
                                                  (f * width) + static_cast<std::size_t>(tx)) *
                                                 4);
                    const std::uint32_t a = src[3] * alpha_scale / (255U * 255U);
                    if (a == 0) {
                        continue;
                    }
                    const std::uint32_t b_alpha = dst[3];
                    const std::uint32_t out_alpha = a + (b_alpha * (255U - a) / 255U);
                    for (int k = 0; k < 3; ++k) {
                        dst[k] = over(src[k], dst[k], a, b_alpha, out_alpha);
                    }
                    dst[3] = static_cast<std::uint8_t>(out_alpha);
                }
            }
        }
    }
    return out;
}

} // namespace engine::assets
