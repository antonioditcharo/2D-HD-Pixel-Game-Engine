#include "engine/game/runtime.h"

#include "engine/foundation/text.h"
#include "engine/game/debug_menu.h"
#include "engine/game/hud.h"
#include "engine/game/localization.h"
#include "engine/game/logic_world.h"
#include "engine/game/shop.h"
#include "engine/logic/compiler.h"
#include "engine/ui/text.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace engine::game {

namespace fs = std::filesystem;

namespace {

/// Prefixes the issues of one file with its path, and appends them to `out`.
void add_issues(std::vector<serialize::Issue>& out, std::string_view file,
                std::vector<serialize::Issue> found) {
    for (serialize::Issue& issue : found) {
        issue.path = std::string(file) + issue.path;
        out.push_back(std::move(issue));
    }
}

[[nodiscard]] std::optional<serialize::Json> parse_bytes(const std::vector<std::uint8_t>& bytes,
                                                         std::vector<serialize::Issue>& issues) {
    return serialize::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), issues);
}

/// The entries a project file may have; anything else is reported and ignored.
constexpr std::array<std::string_view, 41> known_project_keys{"format",
                                                              "version",
                                                              "name",
                                                              "start_scene",
                                                              "ppu",
                                                              "width",
                                                              "height",
                                                              "background",
                                                              "render_style",
                                                              "gravity",
                                                              "camera_pitch",
                                                              "camera_distance",
                                                              "camera_fov",
                                                              "camera_clamp",
                                                              "camera_smoothing",
                                                              "camera_dead_zone",
                                                              "camera_look_ahead",
                                                              "ambient_light",
                                                              "sun_light",
                                                              "sun_angle",
                                                              "sun_height",
                                                              "look",
                                                              "sprite_shadow",
                                                              "shadow_shape",
                                                              "block_shadow",
                                                              "scene_fade",
                                                              "day_minutes",
                                                              "start_hour",
                                                              "title_screen",
                                                              "pause_menu",
                                                              "text_speed",
                                                              "language",
                                                              "languages",
                                                              "palette",
                                                              "font",
                                                              "font_characters",
                                                              "box_frame",
                                                              "box_frame_edge",
                                                              "ui",
                                                              "music_while_talking",
                                                              "minimap"};

} // namespace

LightingObject starting_lighting(const ProjectSettings& settings) {
    LightingObject lighting;
    lighting.ambient = settings.ambient_light;
    lighting.sun = settings.sun_light;
    lighting.sun_angle = settings.sun_angle;
    lighting.sun_height = settings.sun_height;
    // With a clock, the light follows the time from the starting hour.
    lighting.time_of_day =
        std::clamp(std::isfinite(settings.start_hour) ? settings.start_hour : 12.0, 0.0, 24.0);
    if (settings.day_minutes > 0.0) {
        lighting.day_minutes = settings.day_minutes;
        lighting.follows_time = true;
        advance_day(lighting, 0.0);
    }
    return lighting;
}

scene2d::DioramaLight diorama_light(const ProjectSettings& settings,
                                    const LightingObject& lighting) {
    // Logic can set anything, so every value is checked before it lights a frame.
    const auto within = [](double value, double fallback, double min, double max) {
        return std::clamp(std::isfinite(value) ? value : fallback, min, max);
    };
    scene2d::DioramaLight light;
    light.ambient = static_cast<float>(within(lighting.ambient, 0.55, 0.0, 2.0));
    light.diffuse = static_cast<float>(within(lighting.sun, 0.45, 0.0, 2.0));
    light.sun = scene2d::sun_direction(
        within(lighting.sun_angle, scene2d::default_sun_angle, -720.0, 720.0),
        within(lighting.sun_height, scene2d::default_sun_height, 5.0, 90.0));
    light.sprite_shadow = static_cast<float>(within(settings.sprite_shadow, 0.35, 0.0, 1.0));
    light.shadows = settings.shadow_shape;
    light.block_shadow = static_cast<float>(within(settings.block_shadow, 0.45, 0.0, 1.0));
    return light;
}

namespace {

/// The look a project's frames get: tilt-shift imitates a lens photographing a miniature,
/// so it belongs to the Diorama style; flat Classic 2D keeps the rest of the look.
render::Look effective_look(const ProjectSettings& settings, const LightingObject& lighting) {
    render::Look look = settings.look;
    if (settings.style != RenderStyle::Diorama) {
        look.tilt_shift = false;
    }
    // The light's colour (day and night, or logic's), in either style.
    const auto channel = [](double v) {
        return static_cast<float>(std::clamp(std::isfinite(v) ? v : 1.0, 0.0, 2.0));
    };
    // In Classic 2D, also how bright the ambient and sun light are: nights are dark, and
    // its lights (draw_lights_2d()) show.
    const double bright =
        settings.style == RenderStyle::Diorama ? 1.0 : classic_brightness(lighting);
    look.tint_r *= channel(lighting.tint_r * bright);
    look.tint_g *= channel(lighting.tint_g * bright);
    look.tint_b *= channel(lighting.tint_b * bright);
    return look;
}

/// The look's numbers in the project file: name, member, range.
struct LookNumber {
    std::string_view name;
    float render::Look::*member;
    float min;
    float max;
};
constexpr std::array<LookNumber, 11> look_numbers{{
    {"focus_center", &render::Look::focus_center, 0.0F, 1.0F},
    {"focus_height", &render::Look::focus_height, 0.0F, 1.0F},
    {"blur", &render::Look::blur, 0.0F, 12.0F},
    {"blur_ramp", &render::Look::blur_ramp, 0.0F, 1.0F},
    {"bloom", &render::Look::bloom, 0.0F, 2.0F},
    {"bloom_threshold", &render::Look::bloom_threshold, 0.0F, 1.0F},
    {"bloom_radius", &render::Look::bloom_radius, 0.0F, 16.0F},
    {"exposure", &render::Look::exposure, 0.0F, 4.0F},
    {"contrast", &render::Look::contrast, 0.0F, 2.0F},
    {"saturation", &render::Look::saturation, 0.0F, 2.0F},
    {"vignette", &render::Look::vignette, 0.0F, 1.0F},
}};

[[nodiscard]] std::string entry_path(const std::string& file, const std::string& key) {
    std::string path = file;
    path += '/';
    path += key;
    return path;
}

[[nodiscard]] std::string unknown_entry(const std::string& key) {
    std::string message = "Unknown entry '";
    message += key;
    message += "'. It was ignored.";
    return message;
}

[[nodiscard]] bool is_logic_asset(std::string_view path) {
    const auto ends_with = [&](std::string_view suffix) {
        return path.size() >= suffix.size() && path.substr(path.size() - suffix.size()) == suffix;
    };
    return ends_with(".drules") || ends_with(".dsequence");
}

/// Where the message box goes in a target of `width` x `height` pixels: three lines of
/// text at the bottom, fewer on very small screens.
struct MessageLayout {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    int lines = 1;
    int text_width = 0;
};

constexpr float message_margin = 4.0F;
constexpr float message_padding = 5.0F;

MessageLayout message_layout(std::uint32_t width, std::uint32_t height, const ui::PixelFont& font) {
    const int line = font.line_height();
    MessageLayout layout;
    layout.lines = std::clamp(((static_cast<int>(height) / 3) - 10) / line, 1, 3);
    layout.height = static_cast<float>((layout.lines * line) - 1) + (2.0F * message_padding);
    layout.x = message_margin;
    layout.width = std::max(static_cast<float>(width) - (2.0F * message_margin), 1.0F);
    layout.y = static_cast<float>(height) - message_margin - layout.height;
    layout.text_width = std::max(static_cast<int>(layout.width - (2.0F * message_padding)), 1);
    return layout;
}

/// How many pages the message showing takes in a game of `settings`' size (1 if none).
std::size_t message_pages(const DialogueObject& dialogue, const ProjectSettings& settings,
                          const ui::PixelFont& font) {
    const std::string* message = dialogue.current();
    const MessageLayout layout = message_layout(settings.width, settings.height, font);
    const std::size_t lines =
        message == nullptr
            ? 0
            : ui::wrap(font, ui::parse_rich(*message).plain, layout.text_width).size();
    const auto per_page = static_cast<std::size_t>(layout.lines);
    return std::max<std::size_t>(1, (lines + per_page - 1) / per_page);
}

} // namespace

// ---------------------------------------------------------------------------------------
// Project settings
// ---------------------------------------------------------------------------------------

std::optional<ProjectSettings> read_project(const fs::path& root,
                                            std::vector<serialize::Issue>& issues) {
    const std::string file(project_file);
    std::string contents;
    {
        std::error_code ec;
        const fs::path path = root / file;
        if (!fs::is_regular_file(path, ec)) {
            issues.push_back(serialize::Issue{file, "There is no " + file + " in " +
                                                        root.generic_string() +
                                                        ", so this is not a Sparxel project."});
            return std::nullopt;
        }
        std::ifstream in(path, std::ios::binary);
        contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!in.is_open() || in.bad()) {
            issues.push_back(serialize::Issue{file, "The project file cannot be read."});
            return std::nullopt;
        }
    }
    std::vector<serialize::Issue> found;
    const std::optional<serialize::Json> json = serialize::parse(contents, found);
    if (!json) {
        add_issues(issues, file, std::move(found));
        return std::nullopt;
    }
    return project_from_json(*json, issues);
}

std::optional<ProjectSettings> project_from_json(const serialize::Json& doc,
                                                 std::vector<serialize::Issue>& issues) {
    const std::string file(project_file);
    if (!doc.is_object()) {
        issues.push_back(
            serialize::Issue{file, "The project file must be a group of named values."});
        return std::nullopt;
    }
    const auto format = doc.find("format");
    const auto version = doc.find("version");
    if (format == doc.end() || !format->is_string() ||
        format->get_ref<const std::string&>() != project_format) {
        issues.push_back(serialize::Issue{file + "/format", "This is not a Sparxel project file."});
        return std::nullopt;
    }
    if (version == doc.end() || !version->is_number_integer() || version->get<std::int64_t>() < 1) {
        issues.push_back(serialize::Issue{
            file + "/version", "The format version is missing or not a whole number above 0."});
        return std::nullopt;
    }
    if (version->get<std::int64_t>() > project_version) {
        issues.push_back(serialize::Issue{file + "/version",
                                          "This project was saved by a newer version of Sparxel. "
                                          "Update Sparxel to open it."});
        return std::nullopt;
    }

    ProjectSettings settings;
    bool ok = true;
    const auto text = [&](const char* key, std::string& out, bool required) {
        const auto found_key = doc.find(key);
        if (found_key == doc.end()) {
            if (required) {
                issues.push_back(serialize::Issue{file + "/" + key, "This entry is missing."});
                ok = false;
            }
            return;
        }
        if (!found_key->is_string()) {
            issues.push_back(serialize::Issue{file + "/" + key, "This must be text."});
            ok = false;
            return;
        }
        out = found_key->get<std::string>();
    };
    const auto whole = [&](const char* key, std::uint32_t& out, std::uint32_t min,
                           std::uint32_t max) {
        const auto found_key = doc.find(key);
        if (found_key == doc.end()) {
            return; // keeps the default
        }
        if (!found_key->is_number_integer() ||
            found_key->get<std::int64_t>() < static_cast<std::int64_t>(min) ||
            found_key->get<std::int64_t>() > static_cast<std::int64_t>(max)) {
            issues.push_back(serialize::Issue{
                file + "/" + key, "This must be a whole number from " + std::to_string(min) +
                                      " to " + std::to_string(max) + "."});
            ok = false;
            return;
        }
        out = static_cast<std::uint32_t>(found_key->get<std::int64_t>());
    };
    text("name", settings.name, true);
    text("start_scene", settings.start_scene, true);
    text("language", settings.language, false);
    text("languages", settings.languages, false);
    text("palette", settings.palette, false);
    text("font", settings.font, false);
    text("font_characters", settings.font_characters, false);
    text("box_frame", settings.box_frame, false);
    // The menus' and messages' colours: each "#rrggbb"; those missing keep the standard.
    if (const auto ui = doc.find("ui"); ui != doc.end() && ui->is_object()) {
        for (const auto& [key, field] :
             {std::pair{"box", &UiTheme::box}, std::pair{"border", &UiTheme::border},
              std::pair{"text", &UiTheme::text}, std::pair{"dim", &UiTheme::dim},
              std::pair{"highlight", &UiTheme::highlight}, std::pair{"plate", &UiTheme::plate}}) {
            const auto found = ui->find(key);
            if (found == ui->end()) {
                continue;
            }
            const std::optional<render::Color> colour =
                found->is_string() ? color_from_hex(found->get<std::string>()) : std::nullopt;
            if (colour) {
                settings.ui.*field = *colour;
            } else {
                issues.push_back(serialize::Issue{entry_path(file, "ui") + "/" + key,
                                                  "This must be a colour such as #ffd27a."});
                ok = false;
            }
        }
    }
    whole("ppu", settings.ppu, 8, 64);
    whole("width", settings.width, 16, 7680);
    whole("height", settings.height, 16, 4320);
    if (const auto background = doc.find("background"); background != doc.end()) {
        if (background->is_array() && background->size() == 3 &&
            std::all_of(background->begin(), background->end(), [](const serialize::Json& c) {
                return c.is_number_integer() && c.get<std::int64_t>() >= 0 &&
                       c.get<std::int64_t>() <= 255;
            })) {
            settings.background =
                render::Color{static_cast<std::uint8_t>((*background)[0].get<std::int64_t>()),
                              static_cast<std::uint8_t>((*background)[1].get<std::int64_t>()),
                              static_cast<std::uint8_t>((*background)[2].get<std::int64_t>()), 255};
        } else {
            issues.push_back(serialize::Issue{
                file + "/background",
                "The background must be three whole numbers from 0 to 255 (red, green, blue)."});
            ok = false;
        }
    }
    for (const auto& [key, value] : doc.items()) {
        if (std::find(known_project_keys.begin(), known_project_keys.end(), key) ==
            known_project_keys.end()) {
            issues.push_back(serialize::Issue{entry_path(file, key), unknown_entry(key)});
        }
    }
    const auto number = [&](const char* key, double& out, double min, double max) {
        const auto found_key = doc.find(key);
        if (found_key == doc.end()) {
            return; // keeps the default
        }
        if (!found_key->is_number() || found_key->get<double>() < min ||
            found_key->get<double>() > max) {
            issues.push_back(serialize::Issue{entry_path(file, key),
                                              "This must be a number from " +
                                                  engine::text::format_number(min) + " to " +
                                                  engine::text::format_number(max) + "."});
            ok = false;
            return;
        }
        out = found_key->get<double>();
    };
    number("camera_pitch", settings.camera_pitch, 5.0, 85.0);
    number("camera_distance", settings.camera_distance, 1.0, 200.0);
    number("camera_fov", settings.camera_fov, 5.0, 90.0);
    number("ambient_light", settings.ambient_light, 0.0, 2.0);
    number("sun_light", settings.sun_light, 0.0, 2.0);
    number("sun_angle", settings.sun_angle, 0.0, 360.0);
    number("sun_height", settings.sun_height, 5.0, 90.0);
    number("sprite_shadow", settings.sprite_shadow, 0.0, 1.0);
    number("block_shadow", settings.block_shadow, 0.0, 1.0);
    number("scene_fade", settings.scene_fade, 0.0, 3.0);
    number("camera_smoothing", settings.camera_smoothing, 0.0, 2.0);
    number("day_minutes", settings.day_minutes, 0.0, 1440.0);
    number("text_speed", settings.text_speed, 0.0, 1000.0);
    number("box_frame_edge", settings.box_frame_edge, 0.0, 64.0);
    number("music_while_talking", settings.music_while_talking, 0.0, 1.0);
    number("start_hour", settings.start_hour, 0.0, 24.0);
    number("camera_dead_zone", settings.camera_dead_zone, 0.0, 16.0);
    number("camera_look_ahead", settings.camera_look_ahead, 0.0, 8.0);
    if (const auto shape = doc.find("shadow_shape"); shape != doc.end()) {
        if (shape->is_string() && shape->get<std::string>() == "cast") {
            settings.shadow_shape = scene2d::ShadowShape::Cast;
        } else if (shape->is_string() && shape->get<std::string>() == "round") {
            settings.shadow_shape = scene2d::ShadowShape::Round;
        } else {
            issues.push_back(serialize::Issue{
                file + "/shadow_shape",
                "The shadow shape must be \"cast\" (away from the sun) or \"round\"."});
            ok = false;
        }
    }
    if (const auto look = doc.find("look"); look != doc.end()) {
        if (!look->is_object()) {
            issues.push_back(
                serialize::Issue{file + "/look", "The look must be a group of named settings."});
            ok = false;
        } else {
            for (const auto& [key, value] : look->items()) {
                std::string where = file;
                where += "/look/";
                where += key;
                if (key == "tilt_shift") {
                    if (value.is_boolean()) {
                        settings.look.tilt_shift = value.get<bool>();
                    } else {
                        issues.push_back(serialize::Issue{where, "This must be true or false."});
                        ok = false;
                    }
                    continue;
                }
                const auto setting =
                    std::find_if(look_numbers.begin(), look_numbers.end(),
                                 [&key](const LookNumber& n) { return n.name == key; });
                if (setting == look_numbers.end()) {
                    issues.push_back(serialize::Issue{where, unknown_entry(key)});
                    continue;
                }
                if (!value.is_number() || value.get<double>() < setting->min ||
                    value.get<double>() > setting->max) {
                    issues.push_back(serialize::Issue{
                        where, "This must be a number from " +
                                   engine::text::format_number(setting->min) + " to " +
                                   engine::text::format_number(setting->max) + "."});
                    ok = false;
                    continue;
                }
                settings.look.*(setting->member) = static_cast<float>(value.get<double>());
            }
        }
    }
    for (const auto& [key, target] :
         {std::pair{"title_screen", &settings.title_screen},
          std::pair{"pause_menu", &settings.pause_menu}, std::pair{"minimap", &settings.minimap}}) {
        if (const auto found = doc.find(key); found != doc.end()) {
            if (found->is_boolean()) {
                *target = found->get<bool>();
            } else {
                issues.push_back(serialize::Issue{file + "/" + key, "This must be true or false."});
                ok = false;
            }
        }
    }
    if (const auto clamp = doc.find("camera_clamp"); clamp != doc.end()) {
        if (clamp->is_boolean()) {
            settings.camera_clamp = clamp->get<bool>();
        } else {
            issues.push_back(
                serialize::Issue{file + "/camera_clamp", "This must be true or false."});
            ok = false;
        }
    }
    if (const auto gravity = doc.find("gravity"); gravity != doc.end()) {
        const bool valid =
            gravity->is_array() && gravity->size() == 2 &&
            std::all_of(gravity->begin(), gravity->end(), [](const serialize::Json& v) {
                return v.is_number() && std::abs(v.get<double>()) <= 1000.0;
            });
        if (valid) {
            settings.gravity_x = (*gravity)[0].get<double>();
            settings.gravity_y = (*gravity)[1].get<double>();
        } else {
            issues.push_back(
                serialize::Issue{file + "/gravity",
                                 "Gravity must be two numbers from -1000 to 1000 (across, down)."});
            ok = false;
        }
    }
    if (const auto style = doc.find("render_style"); style != doc.end()) {
        const std::string name = style->is_string() ? style->get<std::string>() : std::string();
        if (name == "classic_2d") {
            settings.style = RenderStyle::Classic2D;
        } else if (name == "diorama") {
            settings.style = RenderStyle::Diorama;
        } else {
            issues.push_back(serialize::Issue{file + "/render_style",
                                              "The render style must be \"classic_2d\" or "
                                              "\"diorama\"."});
            ok = false;
        }
    }
    return ok ? std::optional<ProjectSettings>(std::move(settings)) : std::nullopt;
}

serialize::Json project_to_json(const ProjectSettings& settings) {
    serialize::Json json = serialize::Json::object();
    json["format"] = project_format;
    json["version"] = project_version;
    json["name"] = settings.name;
    json["start_scene"] = settings.start_scene;
    if (!settings.language.empty()) {
        json["language"] = settings.language;
    }
    if (!settings.languages.empty()) {
        json["languages"] = settings.languages;
    }
    if (settings.ui != UiTheme{}) {
        json["ui"] = serialize::Json{{"box", color_to_hex(settings.ui.box)},
                                     {"border", color_to_hex(settings.ui.border)},
                                     {"text", color_to_hex(settings.ui.text)},
                                     {"dim", color_to_hex(settings.ui.dim)},
                                     {"highlight", color_to_hex(settings.ui.highlight)},
                                     {"plate", color_to_hex(settings.ui.plate)}};
    }
    json["render_style"] = settings.style == RenderStyle::Diorama ? "diorama" : "classic_2d";
    json["camera_pitch"] = settings.camera_pitch;
    json["camera_distance"] = settings.camera_distance;
    json["camera_fov"] = settings.camera_fov;
    json["camera_clamp"] = settings.camera_clamp;
    json["title_screen"] = settings.title_screen;
    json["pause_menu"] = settings.pause_menu;
    json["minimap"] = settings.minimap;
    json["ambient_light"] = settings.ambient_light;
    json["sun_light"] = settings.sun_light;
    json["sun_angle"] = settings.sun_angle;
    json["sun_height"] = settings.sun_height;
    json["sprite_shadow"] = settings.sprite_shadow;
    json["shadow_shape"] = settings.shadow_shape == scene2d::ShadowShape::Round ? "round" : "cast";
    json["block_shadow"] = settings.block_shadow;
    json["scene_fade"] = settings.scene_fade;
    json["camera_smoothing"] = settings.camera_smoothing;
    json["day_minutes"] = settings.day_minutes;
    json["text_speed"] = settings.text_speed;
    json["music_while_talking"] = settings.music_while_talking;
    json["start_hour"] = settings.start_hour;
    json["camera_dead_zone"] = settings.camera_dead_zone;
    json["camera_look_ahead"] = settings.camera_look_ahead;
    serialize::Json look = serialize::Json::object();
    look["tilt_shift"] = settings.look.tilt_shift;
    for (const LookNumber& number : look_numbers) {
        look[std::string(number.name)] = static_cast<double>(settings.look.*(number.member));
    }
    json["look"] = std::move(look);
    json["gravity"] = serialize::Json::array({settings.gravity_x, settings.gravity_y});
    json["ppu"] = settings.ppu;
    if (!settings.palette.empty()) {
        json["palette"] = settings.palette;
    }
    if (!settings.font.empty()) {
        json["font"] = settings.font;
        json["font_characters"] = settings.font_characters;
    }
    if (!settings.box_frame.empty()) {
        json["box_frame"] = settings.box_frame;
        json["box_frame_edge"] = settings.box_frame_edge;
    }
    json["width"] = settings.width;
    json["height"] = settings.height;
    json["background"] = serialize::Json::array(
        {settings.background.r, settings.background.g, settings.background.b});
    return json;
}

// ---------------------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------------------

void register_types(reflect::Registry& registry) {
    scene2d::register_types(registry);
    input::register_types(registry);
    register_behaviours(registry);
    register_save_types(registry);
    register_audio_types(registry);
    register_dialogue_types(registry);
    register_lighting_types(registry);
    register_inventory_types(registry);
    register_camera_types(registry);
    register_quest_types(registry);
    register_particle_types(registry);
    register_table_types(registry);
    register_battle_types(registry);
}

// ---------------------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------------------

/// "#rrggbb" as a colour; `fallback` if it is not one.
render::Color hex_color(std::string_view hex, render::Color fallback) {
    if (hex.size() != 7 || hex[0] != '#') {
        return fallback;
    }
    std::array<std::uint8_t, 3> parts{};
    for (std::size_t i = 0; i < 3; ++i) {
        unsigned value = 0;
        for (const char c : hex.substr(1 + (i * 2), 2)) {
            value *= 16;
            if (c >= '0' && c <= '9') {
                value += static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value += static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value += static_cast<unsigned>(c - 'A' + 10);
            } else {
                return fallback;
            }
        }
        parts.at(i) = static_cast<std::uint8_t>(value);
    }
    return render::Color{parts[0], parts[1], parts[2], 255};
}

/// Adds how long the scope it lives in took, in milliseconds, to `average` (smoothed).
class Timing {
public:
    explicit Timing(double& average) noexcept
        : average_(average), started_(std::chrono::steady_clock::now()) {}
    Timing(const Timing&) = delete;
    Timing& operator=(const Timing&) = delete;
    Timing(Timing&&) = delete;
    Timing& operator=(Timing&&) = delete;
    ~Timing() {
        const std::chrono::duration<double, std::milli> took =
            std::chrono::steady_clock::now() - started_;
        average_ = average_ == 0.0 ? took.count() : (0.9 * average_) + (0.1 * took.count());
    }

private:
    double& average_;
    std::chrono::steady_clock::time_point started_;
};

/// A message's letters in their styles, those in the default white in the theme's text
/// colour instead, so words show on light boxes too.
ui::RichText themed_rich(const std::string& message, const UiTheme& theme) {
    ui::RichText rich = ui::parse_rich(message);
    constexpr render::Color white{255, 255, 255, 255};
    for (auto& style : rich.styles) {
        if (style.color.r == white.r && style.color.g == white.g && style.color.b == white.b &&
            style.color.a == white.a) {
            style.color = theme.text;
        }
    }
    return rich;
}

/// The actions the player may rebind on the Options page.
constexpr std::array<std::string_view, 7> rebindable{
    "move_up", "move_down", "move_left", "move_right", "interact", "cancel", "menu"};

std::vector<input::Binding> keyboard_bindings(const input::InputMap& map, std::string_view action) {
    std::vector<input::Binding> out;
    for (const input::Binding binding : map.bindings(action)) {
        if (binding.device == input::Binding::Device::Keyboard) {
            out.push_back(binding);
        }
    }
    return out;
}

/// The Options page's list of keys, from the bindings now.
void refresh_option_keys(GameMenu::Options& options, const input::InputMap& map) {
    options.keys.clear();
    for (const std::string_view action : rebindable) {
        std::string names;
        for (const input::Binding binding : keyboard_bindings(map, action)) {
            names += (names.empty() ? "" : ", ") + input::binding_name(binding);
        }
        options.keys.emplace_back(std::string(action), names.empty() ? "none" : names);
    }
}

/// `action`'s keys become `key` alone. Another action that had `key` loses it, and if it
/// is left with no key it takes `action`'s old ones: the two swap.
input::InputMap rebind(input::InputMap map, std::string_view action, input::Binding key) {
    const std::vector<input::Binding> old = keyboard_bindings(map, action);
    for (const input::Binding binding : old) {
        map.unbind(action, binding);
    }
    for (const std::string_view other : rebindable) {
        if (other == action || !map.unbind(other, key)) {
            continue;
        }
        if (keyboard_bindings(map, other).empty()) {
            for (const input::Binding binding : old) {
                if (binding != key) {
                    map.bind(other, binding);
                }
            }
        }
    }
    map.bind(action, key);
    return map;
}

serialize::Json options_to_json(const GameMenu::Options& options, const input::InputMap& map) {
    serialize::Json json = serialize::Json::object();
    json["music"] = options.music;
    json["sounds"] = options.sounds;
    json["text_speed"] = options.text_speed;
    json["language"] = options.language;
    serialize::Json keys = serialize::Json::object();
    for (const std::string_view action : rebindable) {
        serialize::Json codes = serialize::Json::array();
        for (const input::Binding binding : keyboard_bindings(map, action)) {
            codes.push_back(binding.code);
        }
        keys[std::string(action)] = std::move(codes);
    }
    json["keys"] = std::move(keys);
    return json;
}

/// Reads the player's options; anything missing or wrong keeps what it was.
void options_from_json(const serialize::Json& json, GameMenu::Options& options,
                       input::InputMap& map) {
    if (!json.is_object()) {
        return;
    }
    const auto number = [&json](const char* key, double& out, double min, double max) {
        if (const auto found = json.find(key); found != json.end() && found->is_number()) {
            out = std::clamp(found->get<double>(), min, max);
        }
    };
    number("music", options.music, 0.0, 1.0);
    number("sounds", options.sounds, 0.0, 1.0);
    number("text_speed", options.text_speed, -1.0, 1000.0);
    if (const auto language = json.find("language");
        language != json.end() && language->is_string()) {
        options.language = language->get<std::string>();
    }
    const auto keys = json.find("keys");
    if (keys == json.end() || !keys->is_object()) {
        return;
    }
    for (const std::string_view action : rebindable) {
        const auto codes = keys->find(std::string(action));
        if (codes == keys->end() || !codes->is_array() || codes->empty()) {
            continue;
        }
        std::vector<input::Binding> chosen;
        for (const serialize::Json& code : *codes) {
            if (code.is_number_unsigned() && code.get<std::uint64_t>() <= 0xFFFFU) {
                chosen.push_back(
                    input::Binding{input::Binding::Device::Keyboard,
                                   static_cast<std::uint16_t>(code.get<std::uint64_t>())});
            }
        }
        if (chosen.empty()) {
            continue;
        }
        for (const input::Binding binding : keyboard_bindings(map, action)) {
            map.unbind(action, binding);
        }
        for (const input::Binding binding : chosen) {
            map.bind(action, binding);
        }
    }
}

/// How the speaker of the message showing looks: its own look, else the cast's.
const SpeakerLook& speaker_look(const DialogueObject& dialogue,
                                const std::map<std::string, SpeakerLook, std::less<>>& cast) {
    if (!dialogue.current_look().empty()) {
        return dialogue.current_look();
    }
    const auto found = cast.find(dialogue.current_speaker());
    return found != cast.end() ? found->second : dialogue.current_look();
}

struct Runtime::Impl {
    explicit Impl(ProjectSettings project, const fs::path& root)
        : settings(std::move(project)), assets(root / "assets"),
          state(input::InputMap::rpg_preset()), input_object(&state),
          idle(input::InputMap::rpg_preset()) {}

    // Members are destroyed in reverse order: GPU objects before the device, the window
    // before the device, and the VM before the input and world it reaches into.
    ProjectSettings settings;
    reflect::Registry registry;
    assets::AssetDatabase assets;
    std::unique_ptr<world::World> world;
    input::InputState state;
    input::InputObject input_object;
    std::unique_ptr<PhysicsLink> physics;
    SaveObject save_object;
    DialogueObject dialogue;
    LightingObject lighting;
    CameraObject camera;
    InventoryObject inventory;
    QuestObject quests;
    TablesObject tables;
    PartyObject party;
    BattleObject battle;
    std::optional<BattleScreen> battle_screen;
    std::optional<ShopScreen> shop_screen;
    /// Achievements just unlocked, shown one after another at the top: the text, and how
    /// long it has shown.
    std::deque<std::pair<std::string, double>> toasts;
    // Testing from the editor (and the flags `debug` and `show_performance` below).
    DebugMenu debug_menu;
    double step_ms = 0.0; ///< How long steps and frames take, smoothed.
    double draw_ms = 0.0;
    double frame_gap_ms = 0.0; ///< Between frames drawn: 1000 / frames a second.
    std::chrono::steady_clock::time_point last_frame;
    /// The battle's picture behind it (empty: the world), and the Enemy on the Map that
    /// started it, if one did.
    std::string battle_picture;
    std::optional<std::string> picture_wanted; ///< From an area or an enemy, for the next.
    std::optional<Guid> touched_enemy;
    Translations translations; ///< From data/translations, in the language chosen.
    GameMenu menu;
    std::optional<std::array<double, 2>> player_was; ///< For encounters: where the player stood.
    ConversationPlayer conversation;
    std::size_t page = 0;   ///< Of the message showing.
    std::size_t choice = 0; ///< The answer the cursor is on, for a question.
    // The typewriter, for the message showing.
    std::string typing_for;   ///< The message being typed (with its tags).
    ui::RichText rich;        ///< It, without tags, with each letter's style.
    double typed = 0.0;       ///< Bytes of rich.plain shown.
    double typing_wait = 0.0; ///< Seconds a [pause] still holds typing.
    std::size_t pauses_done = 0;
    double message_seconds = 0.0; ///< How long it has shown, for waves and shakes.
    double box_seconds = 0.0;     ///< How long the message box has been open, to open it.
    double stepped = 0.0;         ///< Tiles the player walked since the last footstep.
    /// The weather on screen: drops (or flakes) in screen fractions, 0 to 1 across and
    /// down, each with its speed; the strength eases towards the lighting's.
    struct Drop {
        double x = 0.0;
        double y = 0.0;
        double speed = 1.0;
    };
    std::vector<Drop> drops;
    double weather_now = 0.0;
    std::string weather_shown = "clear";
    std::uint32_t weather_seed = 2463534242U;
    std::size_t blipped = 0; ///< Bytes of rich.plain the voice has gone past.
    std::size_t letters = 0; ///< Letters (not spaces) typed, for every other blip.
    /// Every speaker of the project's conversations, by name: how Rules' "{speaker} says"
    /// shows them too.
    std::map<std::string, SpeakerLook, std::less<>> cast;
    input::InputState idle; ///< No controls held: what movement sees while a message is open.
    std::unique_ptr<audio::AudioEngine> audio_engine;
    AudioObject audio_object;
    std::vector<std::string> problems;
    /// What the entities of the project's other scenes are made of, by each name logic can
    /// use for them, so Rules about them compile while another scene is played.
    std::map<std::string, std::vector<const reflect::TypeInfo*>, std::less<>> elsewhere;
    /// A change of scene on its way: the screen fades out (`fade_steps` up to
    /// `fade_length`), the scene changes, then it fades in (down to 0).
    enum class Fade : std::uint8_t { None, Out, In } fading = Fade::None;
    std::optional<SaveObject::SceneRequest> leaving;
    std::uint32_t fade_steps = 0;
    std::uint32_t fade_length = 0;
    std::uint32_t steps_taken = 0; ///< Footsteps taken, for each one's pitch.
    bool debug = false;            ///< Testing from the editor: the debug menu and timings.
    /// Testing: time paused (F2), going a step (F6), or slowed (F7).
    enum class Time : std::uint8_t { Normal, Paused, Slow } time_control = Time::Normal;
    bool frame_step = false;
    bool show_performance = false;
    std::unique_ptr<logic::Vm> vm;
    FixedStepClock clock;
    const input::Recording* replay{nullptr};
    std::uint64_t steps = 0; ///< Steps run, counted by step() itself so direct calls count.
    bool keep_running = true;
    std::vector<std::string> log;

    std::unique_ptr<render::Device> device;
    std::unique_ptr<render::Window> window;
    std::unique_ptr<render::RenderTarget> target;
    std::unique_ptr<render::SpriteRenderer> sprites;
    std::unique_ptr<render::WorldRenderer> world_renderer; ///< For the Diorama style.
    /// With a look that changes something: the world is drawn into `scene`, and the
    /// post-processor writes it into `target`.
    std::unique_ptr<render::RenderTarget> scene;
    std::unique_ptr<render::PostProcessor> post;
    std::unique_ptr<scene2d::TextureCache> textures;
    /// The sheets of the pictures: tags with their frames' times.
    std::unique_ptr<scene2d::SheetLibrary> sheets;
    std::unique_ptr<render::Texture> font;              ///< The font's atlas.
    ui::PixelFont font_face = ui::PixelFont::builtin(); ///< The project's font, or the built-in.
    BoxFrame box_frame; ///< The boxes' picture (settings.box_frame), if any.
    /// Finds box_frame's texture again (when the game opens, and assets change).
    void find_box_frame() {
        box_frame.edge = static_cast<float>(
            std::isfinite(settings.box_frame_edge) ? settings.box_frame_edge : 3.0);
        const assets::AssetRecord* record =
            settings.box_frame.empty() ? nullptr : assets.find(settings.box_frame);
        box_frame.texture =
            record != nullptr && textures != nullptr ? textures->get(record->guid) : nullptr;
    }
    std::unique_ptr<render::Texture> white; ///< One white pixel, for boxes.
};

Runtime::Runtime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Runtime::~Runtime() = default;

std::unique_ptr<Runtime> Runtime::open(const fs::path& root, const RuntimeOptions& options,
                                       std::vector<serialize::Issue>& issues) {
    std::optional<ProjectSettings> settings = read_project(root, issues);
    if (!settings) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>(std::move(*settings), root);
    Impl& r = *impl;
    r.replay = options.replay;

    register_types(r.registry);
    r.world = std::make_unique<world::World>(r.registry);
    scene2d::register_components(*r.world);
    register_behaviour_components(*r.world);
    r.physics = std::make_unique<PhysicsLink>(r.settings.gravity_x, r.settings.gravity_y);

    // Assets.
    r.assets.refresh();
    r.sheets = std::make_unique<scene2d::SheetLibrary>(r.assets);
    for (const assets::AssetIssue& issue : r.assets.issues()) {
        issues.push_back(serialize::Issue{"assets/" + issue.path, issue.message});
    }

    // The start scene.
    const std::string scene_file = "assets/" + r.settings.start_scene;
    const assets::AssetRecord* scene = r.assets.find(r.settings.start_scene);
    const std::optional<std::vector<std::uint8_t>> scene_bytes =
        scene == nullptr ? std::nullopt : r.assets.read(scene->guid);
    if (!scene_bytes) {
        issues.push_back(
            serialize::Issue{std::string(project_file) + "/start_scene",
                             "The start scene " + r.settings.start_scene + " is not in assets/."});
        return nullptr;
    }
    std::vector<serialize::Issue> found;
    const std::optional<serialize::Json> scene_json = parse_bytes(*scene_bytes, found);
    if (!scene_json) {
        add_issues(issues, scene_file, std::move(found));
        return nullptr;
    }
    serialize::SceneLoad loaded = serialize::scene_from_json(*scene_json, *r.world);
    const bool scene_failed = loaded.entities.empty() && !loaded.issues.empty();
    add_issues(issues, scene_file, std::move(loaded.issues));
    if (scene_failed) {
        return nullptr;
    }

    // Logic.
    r.vm = std::make_unique<logic::Vm>(r.registry);
    attach(*r.vm, *r.world);
    if (!r.vm->bind("input", reflect::ObjectRef::of(r.input_object))) {
        // Not expected: the name is valid and the type was registered above. The game can
        // still run; only logic that reads input is affected.
        issues.push_back(
            serialize::Issue{"", "The input object could not be made available to logic."});
    }
    // Saves go where asked, or else to the player's own folder for this game, found (and
    // created) only when a save is first used.
    r.save_object = SaveObject([folder = options.save_folder, name = r.settings.name,
                                project_root = fs::path(root)]() -> fs::path {
        if (!folder.empty()) {
            return folder;
        }
        char* pref = SDL_GetPrefPath("Sparxel", name.c_str());
        if (pref == nullptr) {
            return project_root / "saves";
        }
        fs::path path(std::u8string(reinterpret_cast<const char8_t*>(pref)));
        SDL_free(pref);
        return path;
    });
    r.save_object.set_scene(scene->path, scene->guid.to_string());
    // "[who] is tagged enemy": asked of whichever world is playing.
    r.save_object.set_tag_check([&r](std::string_view object, std::string_view tag) {
        if (r.world == nullptr) {
            return false;
        }
        const world::Entity entity = resolve(*r.world, object);
        std::string wanted;
        for (const char c : tag) {
            wanted += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::vector<std::string> tags = tags_of(*r.world, entity);
        return entity.is_valid() && std::find(tags.begin(), tags.end(), wanted) != tags.end();
    });
    // The other scenes' entities, for logic about them.
    for (const assets::AssetRecord& record : r.assets.assets()) {
        if (!record.path.ends_with(".dscene") || record.guid == scene->guid) {
            continue;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = r.assets.read(record.guid);
        std::vector<serialize::Issue> ignored; // told when the scene is gone to
        const std::optional<serialize::Json> other =
            bytes ? parse_bytes(*bytes, ignored) : std::nullopt;
        if (!other) {
            continue;
        }
        world::World scratch(r.registry);
        scene2d::register_components(scratch);
        register_behaviour_components(scratch);
        for (const world::Entity entity : serialize::scene_from_json(*other, scratch).entities) {
            for (const std::string& name : logic_names(scratch, entity)) {
                std::vector<const reflect::TypeInfo*>& parts = r.elsewhere[name];
                for (const reflect::TypeInfo* type : scratch.components(entity)) {
                    if (std::find(parts.begin(), parts.end(), type) == parts.end()) {
                        parts.push_back(type);
                    }
                }
            }
        }
    }
    // Sound: a missing sound output is reported, and the game runs silent.
    if (options.audio != AudioMode::Off) {
        std::string error;
        r.audio_engine = audio::AudioEngine::create(
            audio::AudioOptions{options.audio == AudioMode::Headless, 48000, 2}, error);
        if (r.audio_engine == nullptr) {
            issues.push_back(serialize::Issue{"", "No sound: " + error});
        }
    }
    r.audio_object = AudioObject(r.audio_engine.get(), &r.assets, r.world.get());
    r.party.set_tables(&r.tables);       // levels come from data/levels
    r.party.set_inventory(&r.inventory); // equipment comes from what the player carries
    r.menu.attach(&r.party, &r.tables, &r.inventory);
    if (!r.vm->bind("audio", reflect::ObjectRef::of(r.audio_object))) {
        issues.push_back(
            serialize::Issue{"", "The audio object could not be made available to logic."});
    }
    r.camera.smoothing = r.settings.camera_smoothing;
    r.camera.dead_zone = r.settings.camera_dead_zone;
    r.camera.look_ahead = r.settings.camera_look_ahead;
    if (!r.vm->bind("camera", reflect::ObjectRef::of(r.camera))) {
        issues.push_back(
            serialize::Issue{"", "The camera object could not be made available to logic."});
    }
    r.lighting = starting_lighting(r.settings);
    if (!r.vm->bind("lighting", reflect::ObjectRef::of(r.lighting))) {
        issues.push_back(
            serialize::Issue{"", "The lighting object could not be made available to logic."});
    }
    r.tables = TablesObject(&r.assets);
    // Words in the language chosen: messages, answers, names and the menus.
    r.translations = Translations(r.tables.find(translations_table));
    r.translations.set_language(r.settings.language);
    r.dialogue.set_translations(&r.translations);
    r.menu.set_translations(&r.translations);
    r.menu.set_theme(&r.settings.ui);
    r.menu.set_frame(&r.box_frame);
    // Credits, from the project's credits.txt: a line each.
    if (const assets::AssetRecord* record = r.assets.find("credits.txt")) {
        if (const std::optional<std::vector<std::uint8_t>> bytes = r.assets.read(record->guid)) {
            std::vector<std::string> lines;
            std::string line;
            for (const std::uint8_t c : *bytes) {
                if (c == '\n') {
                    lines.push_back(line);
                    line.clear();
                } else if (c != '\r') {
                    line += static_cast<char>(c);
                }
            }
            if (!line.empty()) {
                lines.push_back(line);
            }
            r.menu.set_credits(std::move(lines));
        }
    }
    if (!r.vm->bind("tables", reflect::ObjectRef::of(r.tables))) {
        issues.push_back(
            serialize::Issue{"", "The tables object could not be made available to logic."});
    }
    for (const auto& [name, object] : {std::pair{"party", reflect::ObjectRef::of(r.party)},
                                       std::pair{"battle", reflect::ObjectRef::of(r.battle)}}) {
        if (!r.vm->bind(name, object)) {
            issues.push_back(serialize::Issue{
                "", std::string("The ") + name + " object could not be made available to logic."});
        }
    }
    r.quests.set_dialogue(&r.dialogue);
    if (!r.vm->bind("quests", reflect::ObjectRef::of(r.quests))) {
        issues.push_back(
            serialize::Issue{"", "The quests object could not be made available to logic."});
    }
    if (!r.vm->bind("inventory", reflect::ObjectRef::of(r.inventory))) {
        issues.push_back(
            serialize::Issue{"", "The inventory object could not be made available to logic."});
    }
    if (!r.vm->bind("dialogue", reflect::ObjectRef::of(r.dialogue))) {
        issues.push_back(
            serialize::Issue{"", "The dialogue object could not be made available to logic."});
    }
    if (!r.vm->bind("game", reflect::ObjectRef::of(r.save_object))) {
        issues.push_back(
            serialize::Issue{"", "The game object could not be made available to logic."});
    }
    r.vm->set_log_handler(
        [&r](std::string_view, std::string_view text) { r.log.emplace_back(text); });
    // An object is one of the game's, an entity of this scene, or one of another scene.
    const logic::ObjectTypes types = [&r](std::string_view name) {
        std::vector<const reflect::TypeInfo*> here = r.vm->object_types(name);
        if (const auto other = r.elsewhere.find(name); here.empty() && other != r.elsewhere.end()) {
            return other->second;
        }
        return here;
    };
    for (const assets::AssetRecord& record : r.assets.assets()) {
        if (!record.path.ends_with(".dtalk")) {
            continue;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = r.assets.read(record.guid);
        std::string ignored; // a broken conversation is reported when it is played
        const std::optional<Conversation> talk =
            bytes ? parse_conversation(
                        std::string_view(reinterpret_cast<const char*>(bytes->data()), // NOLINT
                                         bytes->size()),
                        ignored)
                  : std::nullopt;
        for (const Speaker& speaker : talk ? talk->speakers : std::vector<Speaker>{}) {
            r.cast.try_emplace(speaker.name, SpeakerLook{speaker.portrait, speaker.voice,
                                                         speaker.pitch, speaker.color});
        }
    }
    for (const assets::AssetRecord& record : r.assets.assets()) {
        if (!is_logic_asset(record.path)) {
            continue;
        }
        const std::string file = "assets/" + record.path;
        const std::optional<std::vector<std::uint8_t>> bytes = r.assets.read(record.guid);
        std::vector<serialize::Issue> logic_issues;
        const std::optional<serialize::Json> json =
            bytes ? parse_bytes(*bytes, logic_issues) : std::nullopt;
        serialize::LogicLoad logic_load =
            json ? serialize::logic_from_json(*json) : serialize::LogicLoad{};
        add_issues(issues, file, std::move(logic_issues));
        add_issues(issues, file, std::move(logic_load.issues));
        if (!logic_load.asset) {
            continue;
        }
        const logic::CompiledLogic compiled = logic::compile(*logic_load.asset, r.registry, types);
        for (const logic::Diagnostic& error : compiled.errors) {
            issues.push_back(serialize::Issue{file + "#block-" + std::to_string(error.block.value),
                                              error.message});
        }
        if (!compiled.ok()) {
            continue;
        }
        if (!r.vm->load(logic_load.asset->name, compiled.source)) {
            for (const logic::ScriptError& error : r.vm->errors()) {
                issues.push_back(serialize::Issue{
                    file + "#block-" + std::to_string(compiled.block_at_line(error.line).value),
                    error.message});
            }
            r.vm->clear_errors();
        }
    }

    // Graphics.
    if (options.graphics) {
        std::string error;
        r.device = render::Device::create(render::DeviceOptions{options.headless, false}, error);
        if (r.device == nullptr) {
            issues.push_back(serialize::Issue{"", error});
            return nullptr;
        }
        r.target = r.device->create_render_target(r.settings.width, r.settings.height);
        r.sprites = render::SpriteRenderer::create(*r.device);
        if (!options.headless) {
            const render::Placement fit =
                render::fit_integer(r.settings.width, r.settings.height, 1280, 720);
            r.window = r.device->create_window(
                options.window_title.empty() ? r.settings.name : options.window_title,
                static_cast<std::uint32_t>(fit.width), static_cast<std::uint32_t>(fit.height));
        }
        if (r.settings.style == RenderStyle::Diorama) {
            r.world_renderer = render::WorldRenderer::create(*r.device);
        }
        const bool post = !effective_look(r.settings, r.lighting).neutral();
        if (post) {
            r.scene = r.device->create_render_target(r.settings.width, r.settings.height);
            r.post = render::PostProcessor::create(*r.device);
        }
        if (r.target == nullptr || r.sprites == nullptr ||
            (r.settings.style == RenderStyle::Diorama && r.world_renderer == nullptr) ||
            (post && (r.scene == nullptr || r.post == nullptr)) ||
            (!options.headless && r.window == nullptr)) {
            issues.push_back(serialize::Issue{"", r.device->last_error()});
            return nullptr;
        }
        r.textures = std::make_unique<scene2d::TextureCache>(*r.device, r.assets);
        // The project's font, if it has one that can be read; else the built-in.
        if (!r.settings.font.empty()) {
            const assets::AssetRecord* record = r.assets.find(r.settings.font);
            const std::optional<std::vector<std::uint8_t>> bytes =
                record != nullptr ? r.assets.read(record->guid) : std::nullopt;
            std::string font_error;
            std::optional<assets::Image> strip;
            if (bytes.has_value()) {
                strip = assets::decode_png(bytes.value(), font_error);
            } else {
                font_error = "there is no such picture in assets/";
            }
            std::optional<ui::PixelFont> font_loaded;
            if (strip.has_value()) {
                font_loaded = ui::PixelFont::from_strip(strip.value().rgba, strip.value().width,
                                                        strip.value().height,
                                                        r.settings.font_characters, font_error);
            }
            if (font_loaded.has_value()) {
                r.font_face = std::move(font_loaded.value());
            } else {
                issues.push_back(serialize::Issue{std::string(project_file) + "/font",
                                                  "The font " + r.settings.font +
                                                      " could not be used (" + font_error +
                                                      "), so the built-in font is shown."});
            }
        }
        const ui::PixelFont& font = r.font_face;
        r.font = r.device->create_texture(font.atlas_width(), font.atlas_height(), font.atlas());
        r.find_box_frame();
        const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
        r.white = r.device->create_texture(1, 1, white);
        if (r.font == nullptr || r.white == nullptr) {
            issues.push_back(serialize::Issue{"", r.device->last_error()});
            return nullptr;
        }
    }

    r.debug = options.debug;
    // The player's options, kept beside the saves.
    std::unique_ptr<Runtime> runtime(new Runtime(std::move(impl)));
    runtime->load_options();
    // With a title screen, the game starts when New game is chosen.
    if (r.settings.title_screen) {
        r.menu.open_title();
    } else {
        r.vm->emit("", "game_start");
    }
    return runtime;
}

void Runtime::step() {
    Impl& r = *impl_;
    const Timing timing(r.step_ms);
    // 1. Input (plan/05 §5).
    r.state.begin_step();
    if (r.replay != nullptr) {
        r.replay->replay(r.steps, r.state);
    } else if (r.window != nullptr) {
        r.keep_running = input::pump_events(r.state) && r.keep_running;
    }

    r.menu.advance(r.clock.step_seconds());
    // The message box opens over a moment, the first time a message shows after none.
    r.box_seconds = r.dialogue.is_open() ? r.box_seconds + r.clock.step_seconds() : 0.0;

    // The weather: the strength eases towards the lighting's (over about two seconds; a
    // change of kind fades the old one out first), and the drops fall.
    {
        const double dt = r.clock.step_seconds();
        const bool same = r.lighting.weather == r.weather_shown;
        const double target = same ? r.lighting.weather_strength : 0.0;
        r.weather_now += std::clamp(target - r.weather_now, -dt * 0.5, dt * 0.5);
        if (!same && r.weather_now <= 0.0) {
            r.weather_shown = r.lighting.weather;
            r.drops.clear();
        }
        const auto random = [&r] {
            r.weather_seed ^= r.weather_seed << 13U;
            r.weather_seed ^= r.weather_seed >> 17U;
            r.weather_seed ^= r.weather_seed << 5U;
            return static_cast<double>(r.weather_seed % 10000U) / 10000.0;
        };
        const bool rain = r.weather_shown == "rain";
        const bool snow = r.weather_shown == "snow";
        const auto wanted = static_cast<std::size_t>((rain   ? 220.0
                                                      : snow ? 140.0
                                                             : 0.0) *
                                                     r.weather_now);
        while (r.drops.size() < wanted) {
            r.drops.push_back(Impl::Drop{random(), random(),
                                         rain ? 1.4 + (random() * 0.6) : 0.08 + (random() * 0.08)});
        }
        if (r.drops.size() > wanted) {
            r.drops.resize(wanted);
        }
        for (Impl::Drop& drop : r.drops) {
            drop.y += drop.speed * dt;
            drop.x +=
                rain ? -0.15 * dt : std::sin((drop.y * 12.0) + (drop.speed * 40.0)) * 0.02 * dt;
            if (drop.y > 1.0) {
                drop.y -= 1.0;
                drop.x = random();
            }
            drop.x -= std::floor(drop.x);
        }
    }

    // Ducking: the music eases down while a message shows, and back up after (a quarter of
    // a second either way).
    if (r.audio_engine != nullptr) {
        const double target = r.dialogue.is_open() ? r.settings.music_while_talking : 1.0;
        const double now = r.audio_engine->music_duck();
        const double step = r.clock.step_seconds() * 4.0;
        if (now != target) {
            r.audio_engine->set_music_duck(now < target ? std::min(now + step, target)
                                                        : std::max(now - step, target));
        }
    }

    // Testing from the editor: once a second, files changed while playing (pictures saved in
    // Sprite Studio or Aseprite, tables, translations) are read again.
    if (r.debug && r.steps % 60 == 59) {
        reload_changed();
    }
    // Testing from the editor: F1 opens and closes the debug menu, F3 the timings.
    if (r.debug) {
        for (const input::Binding binding : r.state.pressed_bindings()) {
            if (binding == input::Binding::key(input::Key::F1)) {
                if (r.debug_menu.is_open()) {
                    r.debug_menu.close();
                } else {
                    r.debug_menu.attach(DebugTargets{&r.inventory, &r.dialogue, &r.quests, &r.party,
                                                     r.world.get(), &r.tables});
                    r.debug_menu.open();
                }
            } else if (binding == input::Binding::key(input::Key::F3)) {
                r.show_performance = !r.show_performance;
            } else if (binding == input::Binding::key(input::Key::F2)) {
                r.time_control =
                    r.time_control == Impl::Time::Paused ? Impl::Time::Normal : Impl::Time::Paused;
            } else if (binding == input::Binding::key(input::Key::F6)) {
                r.frame_step = true;
            } else if (binding == input::Binding::key(input::Key::F7)) {
                r.time_control =
                    r.time_control == Impl::Time::Slow ? Impl::Time::Normal : Impl::Time::Slow;
            }
        }
        // Time controls: paused (F6 goes one step), or slow motion (every other step).
        const bool hold = (r.time_control == Impl::Time::Paused && !r.frame_step) ||
                          (r.time_control == Impl::Time::Slow && r.steps % 2 == 1);
        r.frame_step = false;
        if (hold && !r.debug_menu.is_open()) {
            ++r.steps;
            return;
        }
    }

    // The typewriter: the message showing is typed out at the project's text speed, its
    // [speed] parts faster or slower, waiting at its [pause]s.
    if (const std::string* message = r.dialogue.current()) {
        if (*message != r.typing_for) {
            r.typing_for = *message;
            r.rich = themed_rich(*message, r.settings.ui);
            r.typed = 0.0;
            r.typing_wait = 0.0;
            r.pauses_done = 0;
            r.message_seconds = 0.0;
            r.blipped = 0;
            r.letters = 0;
        }
        const double step_seconds = r.clock.step_seconds();
        r.message_seconds += step_seconds;
        const auto size = static_cast<double>(r.rich.plain.size());
        const double text_speed = r.menu.options().text_speed >= 0.0 ? r.menu.options().text_speed
                                                                     : r.settings.text_speed;
        if (!(text_speed > 0.0)) {
            r.typed = size;
        } else if (r.typed < size) {
            if (r.typing_wait > 0.0) {
                r.typing_wait -= step_seconds;
            } else {
                const auto at = static_cast<std::size_t>(r.typed);
                const float speed = at < r.rich.styles.size() ? r.rich.styles[at].speed : 1.0F;
                double next = r.typed + (text_speed * speed * step_seconds);
                // A pause reached: typing stops there and waits.
                if (r.pauses_done < r.rich.pauses.size() &&
                    static_cast<double>(r.rich.pauses[r.pauses_done].first) <= next) {
                    next = static_cast<double>(r.rich.pauses[r.pauses_done].first);
                    r.typing_wait = r.rich.pauses[r.pauses_done].second;
                    ++r.pauses_done;
                }
                r.typed = std::min(next, size);
            }
        }
        // The speaker's voice: a blip every other letter typed, at most one a step, its
        // pitch varied a little so it sounds like talk.
        const auto now = static_cast<std::size_t>(r.typed);
        bool blip = false;
        for (; r.blipped < now && r.blipped < r.rich.plain.size(); ++r.blipped) {
            const char letter = r.rich.plain[r.blipped];
            if (letter != ' ' && letter != '\n') {
                blip = blip || r.letters % 2 == 0;
                ++r.letters;
            }
        }
        const SpeakerLook& look = speaker_look(r.dialogue, r.cast);
        if (blip && text_speed > 0.0 && !look.voice.empty()) {
            const double vary = 1.0 + (0.04 * (static_cast<double>(r.letters % 3) - 1.0));
            r.audio_object.play_voice(look.voice, look.pitch * vary);
        }
    } else {
        r.typing_for.clear();
    }

    // While a message is open, the player stands still and Interact turns its pages.
    // So does everyone while the scene fades out and in.
    const bool talking = r.dialogue.is_open();
    const input::InputState& moves = talking || r.battle_screen || r.shop_screen || r.menu.open() ||
                                             r.debug_menu.is_open() || r.fading != Impl::Fade::None
                                         ? r.idle
                                         : r.state;

    // 2. Movement, with walls, bodies and walk animations; then trigger areas.
    const double dt = r.clock.step_seconds();
    const std::vector<world::Entity> arrived =
        steer_walkers(*r.world, dt, r.settings.ppu, talking || r.fading != Impl::Fade::None);
    move_characters(*r.world, moves, dt, r.settings.ppu);
    for (const world::Entity entity : arrived) {
        emit(*r.vm, *r.world, entity, "arrived");
    }
    update_camera(r.camera, *r.world, dt);
    // Encounters: the distance the player walked counts inside Encounter Areas.
    if (const std::optional<std::array<double, 2>> now = player_position(*r.world)) {
        if (r.player_was && !talking && !r.battle_screen && !r.shop_screen && !r.menu.open()) {
            const double walked =
                std::hypot((*now)[0] - (*r.player_was)[0], (*now)[1] - (*r.player_was)[1]);
            footsteps(walked, *now);
            std::string background;
            if (const std::optional<std::string> enemies =
                    update_encounters(*r.world, walked, &background)) {
                r.battle.start(*enemies);
                r.picture_wanted = background;
            }
        }
        r.player_was = now;
    }
    // Enemies on the map: walking into one starts its battle.
    if (const std::optional<world::Entity> enemy = update_enemies_on_map(*r.world);
        enemy && !talking && !r.battle_screen && !r.shop_screen && !r.menu.open() &&
        r.fading == Impl::Fade::None) {
        if (const auto* foe = r.world->get<EnemyOnMap>(*enemy); foe != nullptr) {
            r.battle.start(foe->enemies);
            r.picture_wanted = foe->background;
            r.touched_enemy = r.world->guid(*enemy);
        }
    }
    advance_day(r.lighting, dt);
    // Physics: dynamic bodies move, and bodies that start touching tell Rules.
    for (const auto& [first, second] : r.physics->step(*r.world, moves, dt, r.settings.ppu)) {
        for (const auto& [touched, other] : {std::pair{first, second}, std::pair{second, first}}) {
            const std::vector<std::string> who = names_of(*r.world, other);
            const std::array<reflect::Value, 1> payload{
                reflect::Value{who.empty() ? std::string() : who.back()}};
            if (r.world->get<PhysicsBody>(touched) != nullptr) {
                emit(*r.vm, *r.world, touched, "touched", payload);
            }
        }
    }
    for (const TriggerEvent& event : update_triggers(*r.world)) {
        const std::vector<std::string> who = names_of(*r.world, event.who);
        const std::array<reflect::Value, 1> payload{
            reflect::Value{who.empty() ? std::string() : who.back()}};
        emit(*r.vm, *r.world, event.trigger, event.entered ? "entered" : "exited", payload);
    }

    // The Options page waiting for a key: the first key pressed is bound, and this step's
    // presses go no further.
    bool rebound = false;
    if (r.menu.waiting_for_key()) {
        for (const input::Binding binding : r.state.pressed_bindings()) {
            if (binding.device == input::Binding::Device::Keyboard) {
                const GameMenu::Result result = r.menu.capture(binding);
                r.state.set_map(rebind(r.state.map(), result.action, result.binding));
                refresh_option_keys(r.menu.options(), r.state.map());
                save_options();
                rebound = true;
                break;
            }
        }
    }

    // 3. Events: each pressed action runs the Rules listening for it. Interact also
    //    reaches what each mover faces, in mover GUID order.
    for (const std::string& action :
         rebound ? std::vector<std::string>{} : r.state.pressed_actions()) {
        // The debug menu, while testing, takes every press.
        if (r.debug_menu.is_open()) {
            r.debug_menu.press(action);
            continue;
        }
        // The title screen or the pause menu takes every press while open.
        if (r.menu.open()) {
            const GameMenu::Result result = r.menu.press(action, r.save_object);
            switch (result.kind) {
            case GameMenu::Result::Kind::NewGame:
                r.vm->emit("", "game_start");
                break;
            case GameMenu::Result::Kind::Load: {
                std::vector<serialize::Issue> issues;
                if (!load_from_slot(result.slot, issues)) {
                    r.problems.push_back("Slot " + std::to_string(result.slot) +
                                         " could not be loaded.");
                }
                break;
            }
            case GameMenu::Result::Kind::Save: {
                std::string error;
                r.menu.say(save_to_slot(result.slot, error)
                               ? "Saved in slot " + std::to_string(result.slot) + "."
                               : "Not saved: " + error);
                break;
            }
            case GameMenu::Result::Kind::Quit:
                r.keep_running = false;
                break;
            case GameMenu::Result::Kind::Options:
                apply_options();
                save_options();
                break;
            default:
                break;
            }
            continue;
        }
        // In a shop, the shop screen takes every press.
        if (r.shop_screen) {
            r.shop_screen->press(action, r.inventory);
            continue;
        }
        // The pause menu opens on the Menu key, when the project has it.
        if (action == "menu" && r.settings.pause_menu && !talking && !r.battle_screen) {
            refresh_option_keys(r.menu.options(), r.state.map());
            r.menu.open_pause();
            continue;
        }
        // In a battle, the battle screen takes every press.
        if (r.battle_screen) {
            r.battle_screen->press(action, r.inventory);
            continue;
        }
        // A question's answers show on its last page; Up and Down move the cursor there.
        const std::vector<std::string>& choices = r.dialogue.choices();
        const bool choosing = talking && !choices.empty() &&
                              r.page + 1 >= message_pages(r.dialogue, r.settings, r.font_face);
        if (choosing && (action == "move_up" || action == "move_down")) {
            r.choice = action == "move_up" ? (r.choice + choices.size() - 1) % choices.size()
                                           : (r.choice + 1) % choices.size();
            continue;
        }
        if (talking && action == "interact" && r.typed < static_cast<double>(r.rich.plain.size())) {
            // Still typing: the page shows whole first.
            const MessageLayout box =
                message_layout(r.settings.width, r.settings.height, r.font_face);
            const std::vector<std::string> lines =
                ui::wrap(r.font_face, r.rich.plain, box.text_width);
            const std::vector<std::size_t> starts = ui::line_starts(r.rich.plain, lines);
            const std::size_t next_page = (r.page + 1) * static_cast<std::size_t>(box.lines);
            const double page_end = next_page < starts.size()
                                        ? static_cast<double>(starts[next_page])
                                        : static_cast<double>(r.rich.plain.size());
            if (r.typed < page_end) {
                r.typed = page_end;
                r.blipped = static_cast<std::size_t>(page_end); // no burst of blips
                r.typing_wait = 0.0;
                while (r.pauses_done < r.rich.pauses.size() &&
                       static_cast<double>(r.rich.pauses[r.pauses_done].first) <= page_end) {
                    ++r.pauses_done;
                }
                continue;
            }
        }
        if (talking && action == "interact") {
            // The next page, or the next message; the last one closing tells Rules.
            if (!choosing && r.page + 1 < message_pages(r.dialogue, r.settings, r.font_face)) {
                ++r.page;
                continue;
            }
            std::optional<std::int64_t> answered;
            if (choosing && r.dialogue.choose(r.choice)) {
                answered = r.dialogue.answer();
            }
            r.page = 0;
            r.choice = 0;
            const bool last = r.dialogue.close_current();
            if (answered) {
                const std::array<reflect::Value, 1> payload{reflect::Value{*answered}};
                r.vm->emit("dialogue", "answered", payload);
            }
            if (last) {
                r.vm->emit("dialogue", "closed");
            }
            continue;
        }
        const std::array<reflect::Value, 1> payload{reflect::Value{action}};
        r.vm->emit("input", "pressed", payload);
        if (action != "interact" || talking) {
            continue;
        }
        std::vector<std::pair<Guid, world::Entity>> movers;
        if (const reflect::TypeInfo* type = r.registry.find_type<TopDownMover>()) {
            r.world->each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
                const TopDownMover* mover = ref.as<TopDownMover>();
                if (mover != nullptr && mover->enabled) {
                    movers.emplace_back(r.world->guid(entity), entity);
                }
            });
        }
        std::sort(movers.begin(), movers.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [guid, mover] : movers) {
            const world::Entity target = find_interaction(*r.world, mover);
            if (target.is_valid()) {
                const std::vector<std::string> who = names_of(*r.world, mover);
                const std::array<reflect::Value, 1> by{
                    reflect::Value{who.empty() ? std::string() : who.back()}};
                emit(*r.vm, *r.world, target, "interacted", by);
            }
        }
    }

    // 4. Waiting Sequences, and events emitted while scripts ran.
    r.vm->update(dt);

    // Battles: one logic asked for starts, with the numbers of the data tables; one that is
    // over and read gives its rewards, keeps the party's HP, and tells Rules how it went.
    if (std::optional<std::string> request = r.battle.take_request(); request && !r.battle_screen) {
        std::vector<std::string> enemies;
        std::string item;
        for (const char c : *request + ",") {
            if (c == ',' || c == '|') {
                const std::size_t a = item.find_first_not_of(' ');
                if (a != std::string::npos) {
                    enemies.push_back(item.substr(a, item.find_last_not_of(' ') - a + 1));
                }
                item.clear();
            } else {
                item += c;
            }
        }
        BattleData data{r.tables.find("data/party"), r.tables.find("data/enemies"),
                        r.tables.find("data/skills"), r.tables.find("data/items")};
        data.levels = r.tables.find("data/levels");
        data.states = r.tables.find("data/states");
        data.member_levels = r.party.levels();
        data.member_equipment = r.party.all_equipment();
        std::vector<std::string> members = r.party.members();
        if (members.empty() && data.party != nullptr && !data.party->rows.empty()) {
            members.push_back(data.party->rows.front().id); // the first of data/party
        }
        std::string error;
        std::optional<Battle> fight =
            Battle::start(data, members, r.party.condition(), enemies,
                          static_cast<std::uint32_t>((r.steps + 1) * 2654435761ULL), error);
        if (fight) {
            r.battle_screen.emplace(std::move(*fight));
            r.battle_screen->set_theme(&r.settings.ui);
            r.battle_screen->set_frame(&r.box_frame);
            r.battle.set_fighting(true);
            r.battle_picture = r.picture_wanted && !r.picture_wanted->empty()
                                   ? *r.picture_wanted
                                   : r.battle.background();
            r.battle_screen->set_backdrop(!r.battle_picture.empty());
            r.picture_wanted.reset();
        } else {
            r.problems.push_back("The battle could not start: " + error);
        }
    }
    // Shops: one logic asked for opens with the prices of data/items; one left tells Rules.
    if (std::optional<std::string> wanted = r.inventory.take_shop_request();
        wanted && !r.shop_screen) {
        const Table* items = r.tables.find("data/items");
        std::vector<std::string> missing;
        std::vector<Ware> wares = shop_wares(*wanted, items, missing);
        for (const std::string& item : missing) {
            r.problems.push_back("The shop cannot sell " + item +
                                 ": it needs a row in data/items with a price above 0.");
        }
        r.shop_screen.emplace(std::move(wares), items);
        r.shop_screen->set_theme(&r.settings.ui);
        r.shop_screen->set_frame(&r.box_frame);
        r.inventory.set_shopping(true);
    }
    if (r.shop_screen && r.shop_screen->closed()) {
        r.shop_screen.reset();
        r.inventory.set_shopping(false);
        r.vm->emit("inventory", "shop_closed");
    }
    if (r.battle_screen && r.battle_screen->finished()) {
        Battle& fought = r.battle_screen->battle();
        for (const Fighter& f : fought.fighters()) {
            if (!f.enemy) {
                r.party.condition()[f.id] = {f.hp, f.mp};
            }
        }
        for (const auto& [item, count] : fought.take_used_items()) {
            r.inventory.take(item, count);
        }
        const BattleOutcome outcome = fought.outcome();
        if (outcome == BattleOutcome::Won) {
            r.party.add_xp(fought.xp());
            // Each member still standing gets all of it, as in classic RPGs.
            for (const Fighter& f : fought.fighters()) {
                if (!f.enemy && f.alive()) {
                    r.party.give_xp(f.id, fought.xp());
                }
            }
            if (fought.gold() > 0) {
                r.inventory.give("gold", fought.gold());
            }
        }
        // An Enemy on the Map beaten is gone, and tells its Rules.
        if (r.touched_enemy) {
            const world::Entity foe = r.world->find(*r.touched_enemy);
            if (outcome == BattleOutcome::Won && foe.is_valid()) {
                if (auto* enemy = r.world->get<EnemyOnMap>(foe)) {
                    enemy->beaten = true;
                }
                if (auto* sprite = r.world->get<scene2d::Sprite>(foe)) {
                    sprite->visible = false;
                }
                emit(*r.vm, *r.world, foe, "defeated");
            }
            r.touched_enemy.reset();
        }
        r.battle_screen.reset();
        r.battle.set_fighting(false);
        r.vm->emit("battle", outcome == BattleOutcome::Won    ? "won"
                             : outcome == BattleOutcome::Lost ? "lost"
                                                              : "fled");
    }

    // Levels reached, after a battle or from logic: said in the message box, with the
    // skills learned, and told to Rules.
    for (const PartyObject::LevelUp& up : r.party.take_level_ups()) {
        const Table* party = r.tables.find("data/party");
        const Table* skills = r.tables.find("data/skills");
        const std::optional<std::string> named =
            party != nullptr ? party->text(up.id, "name") : std::nullopt;
        const std::string name = named && !named->empty() ? *named : up.id;
        r.dialogue.say(name + " is now level " + std::to_string(up.to) + "!");
        const std::vector<std::string> had = skills_at(party, up.id, up.from);
        for (const std::string& skill : skills_at(party, up.id, up.to)) {
            if (std::find(had.begin(), had.end(), skill) == had.end()) {
                const std::optional<std::string> called =
                    skills != nullptr ? skills->text(skill, "name") : std::nullopt;
                r.dialogue.say(name + " learns " + (called && !called->empty() ? *called : skill) +
                               "!");
            }
        }
        const std::array<reflect::Value, 1> payload{reflect::Value{up.id}};
        r.vm->emit("party", "level_up", payload);
    }

    // Conversations: one logic asked for starts; the one playing goes on once no message
    // is open, and Rules hear when it ends.
    if (std::optional<DialogueObject::ConversationRequest> request =
            r.dialogue.take_conversation_request()) {
        const assets::AssetRecord* record = r.assets.find(request->file);
        const std::optional<std::vector<std::uint8_t>> bytes =
            record != nullptr ? r.assets.read(record->guid) : std::nullopt;
        std::string error = "there is no such conversation in assets/.";
        std::optional<Conversation> talk =
            bytes ? parse_conversation(
                        std::string_view(reinterpret_cast<const char*>(bytes->data()), // NOLINT
                                         bytes->size()),
                        error)
                  : std::nullopt;
        if (talk) {
            r.conversation.start(std::move(*talk), request->file, request->node);
        } else {
            r.problems.push_back(request->file + ": " + error);
        }
    }
    if (const std::string ended =
            r.conversation.update(r.dialogue, r.inventory, r.quests, r.dialogue.flags());
        !ended.empty()) {
        const std::array<reflect::Value, 1> payload{reflect::Value{ended}};
        r.vm->emit("dialogue", "conversation_ended", payload);
    }

    // 5. Animation; animations that reached their end tell Rules they finished.
    for (const world::Entity entity : scene2d::animate(*r.world, dt, r.sheets.get())) {
        emit(*r.vm, *r.world, entity, "finished");
    }
    update_particles(*r.world, dt);
    ++r.steps;

    // 6. Saving and loading that logic asked for, now that no script is running.
    // Timers count down while the game plays (not while a menu is open); messages sent this
    // step reach Rules.
    if (!r.menu.open() && !r.debug_menu.is_open()) {
        for (const std::string& ended : r.save_object.tick_timers(dt)) {
            const std::array<reflect::Value, 1> payload{reflect::Value{ended}};
            r.vm->emit("game", "timer_ended", payload);
        }
    }
    for (const std::string& message : r.save_object.take_messages()) {
        const std::array<reflect::Value, 1> payload{reflect::Value{message}};
        r.vm->emit("game", "message", payload);
    }
    // Achievements unlocked: kept in the player's file at once, and announced.
    if (const std::vector<std::string> unlocked = r.save_object.take_unlocked();
        !unlocked.empty()) {
        save_achievements();
        const Table* table = r.tables.find("data/achievements");
        for (const std::string& id : unlocked) {
            const std::optional<std::string> named =
                table != nullptr ? table->text(id, "name") : std::nullopt;
            r.toasts.emplace_back(named && !named->empty() ? *named : id, 0.0);
        }
    }
    if (!r.toasts.empty()) {
        r.toasts.front().second += dt;
        if (r.toasts.front().second > 3.0) {
            r.toasts.pop_front();
        }
    }
    // A save point: the save slots, over the game.
    if (r.save_object.take_save_menu() && !r.menu.open()) {
        r.menu.open_save();
    }
    // Things created and removed by logic.
    for (const SaveObject::Spawn& spawn : r.save_object.take_spawns()) {
        if (spawn.remove) {
            const world::Entity gone = resolve(*r.world, spawn.where);
            if (!gone.is_valid() || !r.world->destroy(gone)) {
                r.problems.push_back("There is nothing called " + spawn.where + " to remove.");
            }
            continue;
        }
        std::string error;
        const std::string made = create_copy(spawn, error);
        if (made.empty()) {
            r.problems.push_back("Could not create " + spawn.prefab + ": " + error);
        } else {
            r.save_object.set_last_created(made);
        }
    }
    for (const SaveObject::Request& request : r.save_object.take_requests()) {
        if (request.load) {
            std::vector<serialize::Issue> issues;
            if (!load_from_slot(request.slot, issues) || !issues.empty()) {
                for (const serialize::Issue& issue : issues) {
                    r.problems.push_back(issue.path + ": " + issue.message);
                }
            }
        } else {
            std::string error;
            if (!save_to_slot(request.slot, error)) {
                r.problems.push_back(error);
            }
        }
    }
    // 7. A change of scene that logic asked for: at once with no fade; else the screen
    //    fades out over scene_fade, the scene changes, and it fades back in.
    const auto change = [this, &r](const SaveObject::SceneRequest& request) {
        std::vector<serialize::Issue> issues;
        static_cast<void>(change_scene(request.scene, request.where, issues));
        for (const serialize::Issue& issue : issues) {
            r.problems.push_back(issue.path.empty() ? issue.message
                                                    : issue.path + ": " + issue.message);
        }
    };
    if (r.fading == Impl::Fade::Out && ++r.fade_steps >= r.fade_length) {
        r.fading = Impl::Fade::In;
        r.fade_steps = r.fade_length;
        if (r.leaving) {
            change(*r.leaving);
            r.leaving.reset();
        }
    } else if (r.fading == Impl::Fade::In) {
        r.fade_steps = r.fade_steps > 0 ? r.fade_steps - 1 : 0;
        if (r.fade_steps == 0) {
            r.fading = Impl::Fade::None;
        }
    }
    if (std::optional<SaveObject::SceneRequest> request = r.save_object.take_scene_request()) {
        const auto length =
            static_cast<std::uint32_t>(std::lround(r.settings.scene_fade / r.clock.step_seconds()));
        if (length == 0) {
            change(*request);
        } else {
            // From as dark as the screen is now (a request while fading in turns back).
            r.leaving = std::move(request);
            r.fade_length = length;
            r.fade_steps = r.fading == Impl::Fade::None ? 0 : std::min(r.fade_steps, length);
            r.fading = Impl::Fade::Out;
        }
    }
    r.audio_object.update_sources(1.0 - fade(), r.clock.step_seconds());
    for (std::string& problem : r.audio_object.take_problems()) {
        r.problems.push_back(std::move(problem));
    }
}

bool Runtime::frame(double seconds) {
    Impl& r = *impl_;
    const std::uint32_t steps = r.clock.advance(seconds).steps;
    for (std::uint32_t i = 0; i < steps; ++i) {
        step();
    }
    if (r.device != nullptr && draw() && r.window != nullptr) {
        r.window->present(*r.target, render::Color{0, 0, 0, 255});
    }
    return r.keep_running;
}

bool Runtime::draw() {
    Impl& r = *impl_;
    const Timing timing(r.draw_ms);
    const auto now = std::chrono::steady_clock::now();
    if (r.last_frame != std::chrono::steady_clock::time_point{}) {
        const std::chrono::duration<double, std::milli> gap = now - r.last_frame;
        r.frame_gap_ms =
            r.frame_gap_ms == 0.0 ? gap.count() : (0.9 * r.frame_gap_ms) + (0.1 * gap.count());
    }
    r.last_frame = now;
    if (r.device == nullptr) {
        return false;
    }
    // The camera rig: the player (the first Top-down Mover not walking by itself), or what
    // logic told it to follow or look at, shaken; else the origin.
    scene2d::Camera2D camera;
    camera.ppu = r.settings.ppu;
    const std::array<double, 2> view_at = camera_view(r.camera, *r.world, r.steps);
    camera.x = view_at[0];
    camera.y = view_at[1];
    const double aspect = static_cast<double>(r.target->width()) /
                          static_cast<double>(std::max<std::uint32_t>(r.target->height(), 1));
    scene2d::DioramaCamera orbit;
    orbit.pitch = r.settings.camera_pitch;
    orbit.distance = r.settings.camera_distance;
    orbit.fov = r.settings.camera_fov;
    orbit.ppu = r.settings.ppu;
    // Keep the view inside the map, so the void beyond its edges never shows.
    if (r.settings.camera_clamp) {
        if (const std::optional<scene2d::Box> bounds = scene2d::map_bounds(*r.world, camera.ppu)) {
            scene2d::Box view;
            if (r.settings.style == RenderStyle::Diorama) {
                view = scene2d::diorama_view_extent(orbit, aspect);
                // Deeper than the map: keep the near edge on it and let the far side, which
                // reads as distance, show the background, rather than centring.
                if (view.bottom - view.top > bounds->bottom - bounds->top) {
                    view.top = -std::numeric_limits<double>::infinity();
                }
            } else {
                const double ppu = static_cast<double>(std::max<std::uint32_t>(camera.ppu, 1));
                const double half_width = static_cast<double>(r.target->width()) / ppu / 2.0;
                const double half_height = static_cast<double>(r.target->height()) / ppu / 2.0;
                view = scene2d::Box{-half_width, -half_height, half_width, half_height};
            }
            const std::array<double, 2> at = scene2d::clamp_view(camera.x, camera.y, view, *bounds);
            camera.x = at[0];
            camera.y = at[1];
        }
    }
    // A look that comes to need processing while playing (night falls): its pass, now.
    if (r.post == nullptr && !effective_look(r.settings, r.lighting).neutral()) {
        r.scene = r.device->create_render_target(r.settings.width, r.settings.height);
        r.post = render::PostProcessor::create(*r.device);
        if (r.scene == nullptr || r.post == nullptr) {
            r.post.reset();
            return false;
        }
    }
    render::RenderTarget& world_target = r.post != nullptr ? *r.scene : *r.target;
    ParticleView particles;
    particles.flat = camera;
    particles.width = world_target.width();
    particles.height = world_target.height();
    if (r.settings.style == RenderStyle::Diorama) {
        orbit.x = camera.x;
        orbit.y = camera.y;
        scene2d::draw_diorama(*r.world, *r.textures, *r.world_renderer, world_target, orbit,
                              r.settings.background, diorama_light(r.settings, r.lighting));
        particles.diorama = true;
        particles.view_projection =
            scene2d::diorama_view_projection(orbit, static_cast<float>(aspect));
    } else {
        scene2d::draw_world(*r.world, *r.textures, *r.sprites, world_target, camera,
                            r.settings.background);
    }
    // Particles over the world, before the look, so bloom makes fire glow.
    static_cast<void>(draw_particles(*r.world, *r.sprites, *r.white, world_target, particles));
    // The look, applied to the world only: the message box is drawn after, crisp.
    if (r.post != nullptr &&
        !r.post->apply(*r.scene, *r.target, effective_look(r.settings, r.lighting))) {
        return false;
    }
    // Classic 2D's lights, over the look's result: the frame night has darkened, which
    // they brighten in their own colour.
    if (r.settings.style != RenderStyle::Diorama) {
        const render::Look shown = effective_look(r.settings, r.lighting);
        scene2d::Lights2DShading shading;
        shading.strength = scene2d::lights_2d_strength(shown.tint_r, shown.tint_g, shown.tint_b);
        shading.darkening = {shown.tint_r, shown.tint_g, shown.tint_b};
        shading.saturation = shown.saturation;
        static_cast<void>(
            scene2d::draw_lights_2d(*r.world, *r.textures, *r.sprites, *r.target, camera, shading));
    }

    // The weather, over the world: rain darkens a little and falls in streaks, snow
    // drifts down, fog veils.
    if (r.weather_now > 0.0 && !r.battle_screen) {
        const auto sw = static_cast<float>(r.target->width());
        const auto sh = static_cast<float>(r.target->height());
        const auto strength = static_cast<float>(r.weather_now);
        const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
        r.sprites->begin_over(*r.target);
        if (r.weather_shown == "rain") {
            r.sprites->draw(*r.white, one, render::Rect{0.0F, 0.0F, sw, sh},
                            render::Color{20, 30, 60, static_cast<std::uint8_t>(60.0F * strength)});
            for (const Impl::Drop& drop : r.drops) {
                r.sprites->draw(*r.white, one,
                                render::Rect{std::floor(static_cast<float>(drop.x) * sw),
                                             std::floor(static_cast<float>(drop.y) * sh), 1.0F,
                                             4.0F},
                                render::Color{170, 190, 230, 150});
            }
        } else if (r.weather_shown == "snow") {
            for (const Impl::Drop& drop : r.drops) {
                const float size = drop.speed > 0.12 ? 2.0F : 1.0F; // nearer flakes are larger
                r.sprites->draw(*r.white, one,
                                render::Rect{std::floor(static_cast<float>(drop.x) * sw),
                                             std::floor(static_cast<float>(drop.y) * sh), size,
                                             size},
                                render::Color{245, 248, 255, 230});
            }
        } else if (r.weather_shown == "fog") {
            r.sprites->draw(
                *r.white, one, render::Rect{0.0F, 0.0F, sw, sh},
                render::Color{210, 215, 225, static_cast<std::uint8_t>(150.0F * strength)});
        }
        static_cast<void>(r.sprites->end());
    }

    // Screen Text and Screen Bars, fixed to the screen, over the world and the weather.
    if (!r.battle_screen && !r.menu.open()) {
        draw_hud();
    }

    // A battle, over the world, or over its background picture.
    if (r.battle_screen && !r.battle_picture.empty()) {
        const assets::AssetRecord* record = r.assets.find(r.battle_picture);
        if (const render::Texture* back = record != nullptr && r.textures != nullptr
                                              ? r.textures->get(record->guid)
                                              : nullptr) {
            // Covering the screen, kept in proportion, centred.
            const auto sw = static_cast<float>(r.target->width());
            const auto sh = static_cast<float>(r.target->height());
            const float scale = std::max(sw / static_cast<float>(std::max(back->width(), 1U)),
                                         sh / static_cast<float>(std::max(back->height(), 1U)));
            const float bw = static_cast<float>(back->width()) * scale;
            const float bh = static_cast<float>(back->height()) * scale;
            r.sprites->begin_over(*r.target);
            r.sprites->draw(
                *back, render::Rect{0.0F, 0.0F, 1.0F, 1.0F},
                render::Rect{std::floor((sw - bw) / 2.0F), std::floor((sh - bh) / 2.0F), bw, bh},
                render::Color{255, 255, 255, 255});
            static_cast<void>(r.sprites->end());
        }
    }
    if (r.battle_screen) {
        // Enemies' pictures, by their path in assets/.
        const auto picture = [&r](const std::string& path) -> const render::Texture* {
            const assets::AssetRecord* record = r.assets.find(path);
            return record != nullptr && r.textures != nullptr ? r.textures->get(record->guid)
                                                              : nullptr;
        };
        r.battle_screen->draw(*r.sprites, *r.target, *r.white, *r.font, r.font_face, r.inventory,
                              picture);
    }
    // A shop, over the world.
    if (r.shop_screen) {
        r.shop_screen->draw(*r.sprites, *r.target, *r.white, *r.font, r.font_face, r.inventory);
    }

    // The message box, over everything.
    if (const std::string* message = r.dialogue.current()) {
        const MessageLayout box =
            message_layout(r.target->width(), r.target->height(), r.font_face);
        const ui::PixelFont& font = r.font_face;
        const ui::RichText rich =
            *message == r.typing_for ? r.rich : themed_rich(*message, r.settings.ui);
        const std::vector<std::string> lines = ui::wrap(font, rich.plain, box.text_width);
        const std::vector<std::size_t> starts = ui::line_starts(rich.plain, lines);
        const auto shown =
            *message == r.typing_for ? static_cast<std::size_t>(r.typed) : rich.plain.size();
        r.sprites->begin_over(*r.target);
        const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
        // Opening (a tenth of a second): the box grows from its middle line, fading in;
        // its words come once it is open, as RPG windows open.
        if (constexpr double opening = 0.1; r.box_seconds < opening) {
            const auto open = static_cast<float>(std::clamp(r.box_seconds / opening, 0.0, 1.0));
            const float h = std::max(2.0F, std::floor(box.height * open));
            const float y = std::floor(box.y + ((box.height - h) / 2.0F));
            draw_box(*r.sprites, *r.white, r.settings.ui, &r.box_frame,
                     render::Rect{box.x, y, box.width, h}, open);
            static_cast<void>(r.sprites->end());
            return true;
        }
        draw_box(*r.sprites, *r.white, r.settings.ui, &r.box_frame,
                 render::Rect{box.x, box.y, box.width, box.height});
        const SpeakerLook& look = speaker_look(r.dialogue, r.cast);
        // The speaker's portrait, standing on the box's right end, as large as drawn
        // (smaller only if the screen has no room).
        if (const assets::AssetRecord* record =
                look.portrait.empty() ? nullptr : r.assets.find(look.portrait)) {
            if (const render::Texture* picture =
                    r.textures != nullptr ? r.textures->get(record->guid) : nullptr) {
                const auto w = static_cast<float>(picture->width());
                const auto h = static_cast<float>(picture->height());
                const float room = box.y + 1.0F;
                const float scale = h > room && h > 0.0F ? room / h : 1.0F;
                r.sprites->draw(
                    *picture, one,
                    render::Rect{std::floor(box.x + box.width - (w * scale) - message_padding),
                                 std::floor(box.y + 1.0F - (h * scale)), w * scale, h * scale},
                    render::Color{255, 255, 255, 255});
            }
        }
        // Who speaks, on a plate above the box's left end.
        if (const std::string speaker = r.translations.translate(r.dialogue.current_speaker());
            !speaker.empty()) {
            const float plate_width =
                static_cast<float>(ui::measure(font, speaker)) + (2.0F * message_padding);
            const float plate_height =
                static_cast<float>(font.line_height()) + (2.0F * message_padding) - 2.0F;
            const float plate_y = std::max(0.0F, box.y - plate_height + 1.0F);
            r.sprites->draw(*r.white, one, render::Rect{box.x, plate_y, plate_width, plate_height},
                            r.settings.ui.border);
            r.sprites->draw(
                *r.white, one,
                render::Rect{box.x + 1.0F, plate_y + 1.0F, plate_width - 2.0F, plate_height - 2.0F},
                r.settings.ui.plate);
            ui::draw_text(*r.sprites, *r.font, font, speaker, box.x + message_padding,
                          plate_y + message_padding - 1.0F,
                          hex_color(look.color, r.settings.ui.highlight));
        }
        const std::size_t first = r.page * static_cast<std::size_t>(box.lines);
        for (std::size_t i = first;
             i < lines.size() && i < first + static_cast<std::size_t>(box.lines); ++i) {
            ui::draw_rich(*r.sprites, *r.font, font, rich, starts[i], starts[i] + lines[i].size(),
                          box.x + message_padding,
                          box.y + message_padding +
                              static_cast<float>(static_cast<int>(i - first) * font.line_height()),
                          r.message_seconds, shown);
        }
        // A question's answers, on its last page: a box above the message, at the right,
        // with a cursor on the answer chosen.
        const std::vector<std::string>& choices = r.dialogue.choices();
        const std::size_t pages = (lines.size() + static_cast<std::size_t>(box.lines) - 1) /
                                  static_cast<std::size_t>(box.lines);
        // The page all shown, and no answers to pick: which button goes on, on the box's
        // bottom edge, blinking ("[Z]", or "[Pad A]" after a gamepad was used).
        const std::size_t page_end = first + static_cast<std::size_t>(box.lines) < starts.size()
                                         ? starts[first + static_cast<std::size_t>(box.lines)]
                                         : rich.plain.size();
        const bool answers_here = !choices.empty() && r.page + 1 >= std::max<std::size_t>(pages, 1);
        if (const std::string key = r.state.prompt("interact");
            !key.empty() && !answers_here && shown >= page_end &&
            std::fmod(r.message_seconds, 1.0) < 0.7) {
            const std::string prompt = "[" + key + "]";
            const auto pw = static_cast<float>(ui::measure(font, prompt) + 4);
            const auto ph = static_cast<float>(font.line_height());
            const float px = box.x + box.width - pw - message_padding;
            const float py = box.y + box.height - (ph / 2.0F) - 1.0F;
            r.sprites->draw(*r.white, one, render::Rect{px, py, pw, ph}, r.settings.ui.box);
            ui::draw_text(*r.sprites, *r.font, font, prompt, px + 2.0F, py,
                          r.settings.ui.highlight);
        }
        if (!choices.empty() && r.page + 1 >= std::max<std::size_t>(pages, 1) &&
            shown >= rich.plain.size()) {
            const float marker = static_cast<float>(ui::measure(font, ">") + 3);
            int widest = 0;
            for (const std::string& choice : choices) {
                widest = std::max(widest, ui::measure(font, choice));
            }
            const float width = (2.0F * message_padding) + marker + static_cast<float>(widest);
            const float height =
                static_cast<float>((static_cast<int>(choices.size()) * font.line_height()) - 1) +
                (2.0F * message_padding);
            const float x = std::max(box.x + box.width - width, message_margin);
            const float y = std::max(box.y - 2.0F - height, message_margin);
            draw_box(*r.sprites, *r.white, r.settings.ui, &r.box_frame,
                     render::Rect{x, y, width, height});
            for (std::size_t i = 0; i < choices.size(); ++i) {
                const float line_y = y + message_padding +
                                     static_cast<float>(static_cast<int>(i) * font.line_height());
                const bool chosen = i == r.choice;
                const render::Color colour = chosen ? r.settings.ui.text : r.settings.ui.dim;
                if (chosen) {
                    ui::draw_text(*r.sprites, *r.font, font, ">", x + message_padding, line_y,
                                  colour);
                }
                ui::draw_text(*r.sprites, *r.font, font, choices[i], x + message_padding + marker,
                              line_y, colour);
            }
        }
        r.sprites->end();
    }
    // A change of scene: black over everything, as dark as the fade has got.
    if (const double dark = fade(); dark > 0.0) {
        r.sprites->begin_over(*r.target);
        r.sprites->draw(
            *r.white, render::Rect{0.0F, 0.0F, 1.0F, 1.0F},
            render::Rect{0.0F, 0.0F, static_cast<float>(r.target->width()),
                         static_cast<float>(r.target->height())},
            render::Color{0, 0, 0, static_cast<std::uint8_t>(std::lround(dark * 255.0))});
        r.sprites->end();
    }
    // The title screen or the pause menu, over everything.
    r.menu.draw(*r.sprites, *r.target, *r.white, *r.font, r.font_face, r.settings.name,
                r.save_object, r.inventory, r.quests, r.party);
    // The minimap: the tiles around the player, solid ones as walls, top right, with the
    // player and the people to talk to as dots. Not over menus, shops or battles.
    if (r.save_object.minimap().value_or(r.settings.minimap) && !r.menu.open() &&
        !r.battle_screen && !r.shop_screen) {
        if (const std::optional<std::array<double, 2>> player = player_position(*r.world)) {
            constexpr int reach = 20;   // tiles each way
            constexpr float dot = 2.0F; // pixels a tile
            const float side = dot * static_cast<float>((2 * reach) + 1);
            const float left = static_cast<float>(r.target->width()) - side - 4.0F;
            constexpr float top = 4.0F;
            const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};
            r.sprites->begin_over(*r.target);
            r.sprites->draw(*r.white, one, render::Rect{left - 1, top - 1, side + 2, side + 2},
                            r.settings.ui.border);
            r.sprites->draw(*r.white, one, render::Rect{left, top, side, side}, r.settings.ui.box);
            const double ppu = std::max<std::uint32_t>(r.settings.ppu, 1);
            const double px = std::floor((*player)[0]);
            const double py = std::floor((*player)[1]);
            // Where map point (x, y) shows, if inside.
            const auto spot = [&](double x, double y) -> std::optional<std::array<float, 2>> {
                const double dx = std::floor(x) - px + reach;
                const double dy = std::floor(y) - py + reach;
                if (dx < 0 || dy < 0 || dx > 2 * reach || dy > 2 * reach) {
                    return std::nullopt;
                }
                return std::array<float, 2>{left + (static_cast<float>(dx) * dot),
                                            top + (static_cast<float>(dy) * dot)};
            };
            if (const reflect::TypeInfo* type = r.world->registry().find_type<scene2d::Tilemap>()) {
                r.world->each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
                    const auto* map = ref.as<scene2d::Tilemap>();
                    const auto* at = r.world->get<scene2d::Transform2D>(e);
                    std::string error;
                    const scene2d::TileGrid* grid =
                        map != nullptr && map->visible ? map->grid(error) : nullptr;
                    if (grid == nullptr || at == nullptr) {
                        return;
                    }
                    const double tile =
                        static_cast<double>(map->tile_size == 0 ? ppu : map->tile_size) / ppu;
                    const render::Color colour =
                        map->solid ? r.settings.ui.border : r.settings.ui.dim;
                    // Only the cells near the player.
                    const auto first = [&](double from, double origin) {
                        return static_cast<std::uint32_t>(
                            std::max(0.0, std::floor((from - origin) / tile)));
                    };
                    const std::uint32_t c0 = first(px - reach, at->x);
                    const std::uint32_t r0 = first(py - reach, at->y);
                    const std::uint32_t c1 =
                        std::min(grid->width, first(px + reach + 1, at->x) + 1);
                    const std::uint32_t r1 =
                        std::min(grid->height, first(py + reach + 1, at->y) + 1);
                    for (std::uint32_t row = r0; row < r1; ++row) {
                        for (std::uint32_t column = c0; column < c1; ++column) {
                            if (grid->at(column, row) < 0) {
                                continue;
                            }
                            if (const auto where =
                                    spot(at->x + (column * tile), at->y + (row * tile))) {
                                r.sprites->draw(*r.white, one,
                                                render::Rect{(*where)[0], (*where)[1], dot, dot},
                                                colour);
                            }
                        }
                    }
                });
            }
            if (const reflect::TypeInfo* type = r.world->registry().find_type<Interactable>()) {
                r.world->each(*type, [&](world::Entity e, reflect::ObjectRef) {
                    const auto* at = r.world->get<scene2d::Transform2D>(e);
                    if (const auto where =
                            at != nullptr ? spot(at->x, at->y - 0.5) : std::nullopt) {
                        r.sprites->draw(*r.white, one,
                                        render::Rect{(*where)[0], (*where)[1], dot, dot},
                                        r.settings.ui.text);
                    }
                });
            }
            r.sprites->draw(
                *r.white, one,
                render::Rect{left + (reach * dot) - 1, top + (reach * dot) - 1, dot + 2, dot + 2},
                r.settings.ui.highlight);
            static_cast<void>(r.sprites->end());
        }
    }
    // An achievement unlocked: a box at the top, sliding down, then up again.
    if (!r.toasts.empty()) {
        const auto& [text, seconds] = r.toasts.front();
        const ui::PixelFont& font = r.font_face;
        const std::string title = r.translations.translate("Achievement unlocked");
        const auto line = static_cast<float>(font.line_height());
        const auto width =
            static_cast<float>(std::max(ui::measure(font, title), ui::measure(font, text))) + 12.0F;
        const float height = (line * 2.0F) + 8.0F;
        const double in_out = std::min({seconds / 0.25, (3.0 - seconds) / 0.25, 1.0});
        const float y = std::floor(-height + (static_cast<float>(in_out) * (height + 4.0F)));
        const float x = std::floor((static_cast<float>(r.target->width()) - width) / 2.0F);
        r.sprites->begin_over(*r.target);
        draw_box(*r.sprites, *r.white, r.settings.ui, &r.box_frame,
                 render::Rect{x, y, width, height});
        ui::draw_text(*r.sprites, *r.font, font, title, x + 6.0F, y + 4.0F, r.settings.ui.dim);
        ui::draw_text(*r.sprites, *r.font, font, text, x + 6.0F, y + 4.0F + line,
                      r.settings.ui.highlight);
        static_cast<void>(r.sprites->end());
    }
    r.debug_menu.draw(*r.sprites, *r.target, *r.white, *r.font, r.font_face);
    // Time controls, said in the bottom-left corner while on.
    if (r.debug && r.time_control != Impl::Time::Normal) {
        const ui::PixelFont& font = r.font_face;
        const std::string words = r.time_control == Impl::Time::Paused
                                      ? "Paused (F2 goes on, F6 one step)"
                                      : "Slow motion (F7)";
        const auto y = static_cast<float>(r.target->height() - font.line_height() - 2);
        r.sprites->begin_over(*r.target);
        ui::draw_text(*r.sprites, *r.font, font, words, 3.0F, y + 1.0F,
                      render::Color{0, 0, 0, 200});
        ui::draw_text(*r.sprites, *r.font, font, words, 2.0F, y, render::Color{220, 60, 220, 255});
        static_cast<void>(r.sprites->end());
    }
    // How long frames take, in plain language (F3 while testing).
    if (r.debug && r.show_performance) {
        constexpr double budget = 1000.0 / 60.0;
        const double used = r.step_ms + r.draw_ms;
        const auto percent = static_cast<int>(std::lround(100.0 * used / budget));
        const auto fps =
            r.frame_gap_ms > 0.0 ? static_cast<int>(std::lround(1000.0 / r.frame_gap_ms)) : 0;
        const auto ms = [](double v) {
            return std::to_string(static_cast<int>(v)) + "." +
                   std::to_string(static_cast<int>(std::fmod(v * 10.0, 10.0))) + " ms";
        };
        const std::array<std::string, 4> report{
            std::to_string(fps) + " frames a second",
            "Logic " + ms(r.step_ms) + ", drawing " + ms(r.draw_ms),
            std::to_string(percent) + "% of a frame: " +
                (percent > 100  ? "too slow, it will stutter"
                 : percent > 70 ? "close to the limit"
                                : "plenty of room"),
            std::to_string(r.world->entity_count()) + " entities"};
        const ui::PixelFont& font = r.font_face;
        const auto line = static_cast<float>(font.line_height());
        r.sprites->begin_over(*r.target);
        r.sprites->draw(*r.white, render::Rect{0.0F, 0.0F, 1.0F, 1.0F},
                        render::Rect{2.0F, 2.0F, 180.0F, (line * 4.0F) + 4.0F},
                        render::Color{0, 0, 0, 170});
        for (std::size_t k = 0; k < report.size(); ++k) {
            ui::draw_text(*r.sprites, *r.font, font, report.at(k), 4.0F,
                          4.0F + (line * static_cast<float>(k)),
                          percent > 100 && k == 2 ? render::Color{255, 110, 110, 255}
                                                  : render::Color{180, 255, 180, 255});
        }
        static_cast<void>(r.sprites->end());
    }
    return true;
}

const DebugMenu& Runtime::debug_menu() const noexcept {
    return impl_->debug_menu;
}

const ProjectSettings& Runtime::settings() const noexcept {
    return impl_->settings;
}

world::World& Runtime::world() noexcept {
    return *impl_->world;
}

logic::Vm& Runtime::vm() noexcept {
    return *impl_->vm;
}

const input::InputState& Runtime::input() const noexcept {
    return impl_->state;
}

std::uint64_t Runtime::step_count() const noexcept {
    return impl_->steps;
}

render::RenderTarget* Runtime::target() noexcept {
    return impl_->target.get();
}

const std::vector<std::string>& Runtime::log() const noexcept {
    return impl_->log;
}

// ---------------------------------------------------------------------------------------
// Saved games
// ---------------------------------------------------------------------------------------

serialize::Json Runtime::save() const {
    const Impl& r = *impl_;
    serialize::Json json = serialize::Json::object();
    json["format"] = save_format;
    json["version"] = save_version;
    json["game"] = r.settings.name;
    json["step"] = r.steps;
    json["scene"] = r.save_object.scene();
    json["world"] = serialize::scene_to_json(*r.world);
    serialize::Json variables = serialize::Json::object();
    for (const std::string& script : r.vm->scripts()) {
        serialize::Json values = serialize::Json::object();
        for (const auto& [name, value] : r.vm->variables(script)) {
            if (const bool* yes = std::get_if<bool>(&value)) {
                values[name] = *yes;
            } else if (const std::int64_t* whole = std::get_if<std::int64_t>(&value)) {
                values[name] = *whole;
            } else if (const double* number = std::get_if<double>(&value)) {
                values[name] = *number;
            } else if (const std::string* words = std::get_if<std::string>(&value)) {
                values[name] = *words;
            }
        }
        variables[script] = std::move(values);
    }
    json["variables"] = std::move(variables);
    json["lighting"] = serialize::Json{
        {"time_of_day", r.lighting.time_of_day},
        {"day_minutes", r.lighting.day_minutes},
        {"follows_time", r.lighting.follows_time},
        {"tint", serialize::Json::array({r.lighting.tint_r, r.lighting.tint_g, r.lighting.tint_b})},
        {"ambient", r.lighting.ambient},
        {"sun", r.lighting.sun},
        {"sun_angle", r.lighting.sun_angle},
        {"sun_height", r.lighting.sun_height},
        {"weather", r.lighting.weather},
        {"weather_strength", r.lighting.weather_strength}};
    serialize::Json carried = serialize::Json::object();
    for (const auto& [item, count] : r.inventory.items()) {
        carried[item] = count;
    }
    json["inventory"] = std::move(carried);
    serialize::Json timers = serialize::Json::object();
    for (const auto& [name, left] : r.save_object.timers()) {
        timers[name] = left;
    }
    json["timers"] = std::move(timers);
    serialize::Json quests = serialize::Json::object();
    for (const auto& [name, quest] : r.quests.quests()) {
        const char* state = quest.state == QuestState::Done     ? "done"
                            : quest.state == QuestState::Failed ? "failed"
                                                                : "active";
        quests[name] = serialize::Json{{"title", quest.title},
                                       {"objective", quest.objective},
                                       {"stage", quest.stage},
                                       {"state", state},
                                       {"order", quest.order}};
    }
    json["quests"] = std::move(quests);
    serialize::Json flags = serialize::Json::object();
    for (const auto& [name, on] : r.dialogue.flags()) {
        flags[name] = on;
    }
    json["flags"] = std::move(flags);
    serialize::Json condition = serialize::Json::object();
    for (const auto& [member, hp_mp] : r.party.condition()) {
        condition[member] = serialize::Json::array({hp_mp.first, hp_mp.second});
    }
    json["party"] = serialize::Json{{"members", r.party.members()},
                                    {"condition", std::move(condition)},
                                    {"xp", r.party.xp()},
                                    {"member_xp", r.party.member_xp()},
                                    {"equipment", r.party.equipment()}};
    return json;
}

bool Runtime::load(const serialize::Json& json, std::vector<serialize::Issue>& issues) {
    Impl& r = *impl_;
    const auto refuse = [&issues](const char* path, std::string message) {
        issues.push_back(serialize::Issue{path, std::move(message)});
        return false;
    };
    if (!json.is_object()) {
        return refuse("", "A save must be a group of named values.");
    }
    const auto format = json.find("format");
    if (format == json.end() || !format->is_string() ||
        format->get_ref<const std::string&>() != save_format) {
        return refuse("/format", "This is not a Sparxel save.");
    }
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer() ||
        version->get<std::int64_t>() < 1) {
        return refuse("/version", "The format version is missing or not a whole number above 0.");
    }
    if (version->get<std::int64_t>() > save_version) {
        return refuse("/version",
                      "This save was made by a newer version of the game. Update it to load it.");
    }
    const auto game = json.find("game");
    if (game == json.end() || !game->is_string() ||
        game->get_ref<const std::string&>() != r.settings.name) {
        return refuse("/game", "This save belongs to another game.");
    }
    const auto step = json.find("step");
    if (step == json.end() || !step->is_number_unsigned()) {
        return refuse("/step", "The step count is missing or not a whole number.");
    }
    const auto saved_world = json.find("world");
    if (saved_world == json.end()) {
        return refuse("/world", "The saved world is missing.");
    }
    const auto variables = json.find("variables");
    if (variables == json.end() || !variables->is_object()) {
        return refuse("/variables", "The saved variables are missing.");
    }

    // Check the world in a scratch world first, so a broken save changes nothing.
    {
        world::World scratch(r.registry);
        scene2d::register_components(scratch);
        register_behaviour_components(scratch);
        serialize::SceneLoad trial = serialize::scene_from_json(*saved_world, scratch);
        if (trial.entities.empty() && !trial.issues.empty()) {
            for (serialize::Issue& issue : trial.issues) {
                issue.path = "/world" + issue.path;
                issues.push_back(std::move(issue));
            }
            return false;
        }
    }

    // Replace the world, stop what was running and close messages, and restore the
    // variables.
    r.vm->stop_all();
    r.dialogue.clear();
    r.page = 0;
    r.choice = 0;
    for (const world::Entity root : r.world->roots()) {
        r.world->destroy(root);
    }
    serialize::SceneLoad loaded = serialize::scene_from_json(*saved_world, *r.world);
    for (serialize::Issue& issue : loaded.issues) {
        issue.path = "/world" + issue.path;
        issues.push_back(std::move(issue));
    }
    for (const auto& [script, values] : variables->items()) {
        if (!values.is_object()) {
            issues.push_back(
                serialize::Issue{"/variables/" + script, "This must be a group of named values."});
            continue;
        }
        for (const auto& [name, value] : values.items()) {
            std::optional<reflect::Value> restored;
            if (value.is_boolean()) {
                restored = reflect::Value{value.get<bool>()};
            } else if (value.is_number_integer()) {
                restored = reflect::Value{value.get<std::int64_t>()};
            } else if (value.is_number_float()) {
                restored = reflect::Value{value.get<double>()};
            } else if (value.is_string()) {
                restored = reflect::Value{value.get<std::string>()};
            }
            if (!restored || !r.vm->set_variable(script, name, *restored)) {
                std::string where = "/variables/";
                where += script;
                where += '/';
                where += name;
                issues.push_back(serialize::Issue{
                    std::move(where),
                    "This variable no longer exists or holds another kind of value. It "
                    "keeps its current value."});
            }
        }
    }
    // Timers running when it was saved.
    r.save_object.timers().clear();
    if (const auto timers = json.find("timers"); timers != json.end() && timers->is_object()) {
        for (const auto& [name, left] : timers->items()) {
            if (left.is_number()) {
                r.save_object.start_timer(name, left.get<double>());
            }
        }
    }
    // What the player carries, if the save has it (saves made before it was saved do not):
    // whole numbers above 0 by name; anything else is passed over, with a note.
    r.inventory.clear();
    if (const auto carried = json.find("inventory");
        carried != json.end() && carried->is_object()) {
        for (const auto& [item, count] : carried->items()) {
            if (count.is_number_integer() && count.get<std::int64_t>() > 0) {
                r.inventory.set(item, count.get<std::int64_t>());
            } else {
                issues.push_back(serialize::Issue{
                    "/inventory/" + item,
                    "How many of it the player carries must be a whole number above 0. It "
                    "was left out."});
            }
        }
    }
    // The party, if the save has it; a battle being fought stops, and so does shopping.
    r.battle_screen.reset();
    r.battle.set_fighting(false);
    r.shop_screen.reset();
    r.inventory.set_shopping(false);
    r.party.clear();
    if (const auto party = json.find("party"); party != json.end() && party->is_object()) {
        if (party->contains("members") && (*party)["members"].is_array()) {
            for (const serialize::Json& member : (*party)["members"]) {
                if (member.is_string()) {
                    r.party.add_member(member.get<std::string>());
                }
            }
        }
        if (party->contains("condition") && (*party)["condition"].is_object()) {
            for (const auto& [member, hp_mp] : (*party)["condition"].items()) {
                if (hp_mp.is_array() && hp_mp.size() == 2 && hp_mp[0].is_number_integer() &&
                    hp_mp[1].is_number_integer()) {
                    r.party.condition()[member] = {hp_mp[0].get<std::int64_t>(),
                                                   hp_mp[1].get<std::int64_t>()};
                }
            }
        }
        if (party->contains("equipment") && (*party)["equipment"].is_object()) {
            for (const auto& [member, slots] : (*party)["equipment"].items()) {
                if (!slots.is_object()) {
                    continue;
                }
                for (const auto& [slot, item] : slots.items()) {
                    if (item.is_string() && !item.get<std::string>().empty()) {
                        r.party.equipment()[member][slot] = item.get<std::string>();
                    }
                }
            }
        }
        if (party->contains("member_xp") && (*party)["member_xp"].is_object()) {
            for (const auto& [member, xp] : (*party)["member_xp"].items()) {
                if (xp.is_number_integer() && xp.get<std::int64_t>() > 0) {
                    r.party.member_xp()[member] = xp.get<std::int64_t>();
                }
            }
        }
        if (party->contains("xp") && (*party)["xp"].is_number_integer()) {
            r.party.set_xp((*party)["xp"].get<std::int64_t>());
        }
    }
    // The story's flags, if the save has them; a conversation playing stops.
    r.conversation.stop();
    r.dialogue.flags().clear();
    if (const auto flags = json.find("flags"); flags != json.end() && flags->is_object()) {
        for (const auto& [name, on] : flags->items()) {
            if (on.is_boolean()) {
                r.dialogue.flags()[name] = on.get<bool>();
            } else {
                issues.push_back(serialize::Issue{
                    "/flags/" + name, "A flag must be true or false. It was left out."});
            }
        }
    }
    // The quests, if the save has it (saves made before they were saved do not).
    r.quests.clear();
    if (const auto quests = json.find("quests"); quests != json.end() && quests->is_object()) {
        for (const auto& [name, saved] : quests->items()) {
            if (!saved.is_object() || !saved.contains("title") || !saved["title"].is_string()) {
                issues.push_back(
                    serialize::Issue{"/quests/" + name, "A quest needs a title. It was left out."});
                continue;
            }
            Quest quest;
            quest.title = saved["title"].get<std::string>();
            quest.objective = saved.value("objective", std::string());
            quest.stage = saved.contains("stage") && saved["stage"].is_number_integer()
                              ? std::max<std::int64_t>(saved["stage"].get<std::int64_t>(), 0)
                              : 0;
            const std::string state = saved.value("state", std::string("active"));
            quest.state = state == "done"     ? QuestState::Done
                          : state == "failed" ? QuestState::Failed
                                              : QuestState::Active;
            quest.order = saved.contains("order") && saved["order"].is_number_unsigned()
                              ? saved["order"].get<std::uint64_t>()
                              : 0;
            r.quests.restore(name, std::move(quest));
        }
    }
    // The lighting, if the save has it (saves made before it was saved do not).
    if (const auto lighting = json.find("lighting");
        lighting != json.end() && lighting->is_object()) {
        if (const auto follows = lighting->find("follows_time");
            follows != lighting->end() && follows->is_boolean()) {
            r.lighting.follows_time = follows->get<bool>();
        }
        if (const auto tint = lighting->find("tint");
            tint != lighting->end() && tint->is_array() && tint->size() == 3) {
            std::array<double*, 3> channels{&r.lighting.tint_r, &r.lighting.tint_g,
                                            &r.lighting.tint_b};
            for (std::size_t i = 0; i < 3; ++i) {
                if ((*tint)[i].is_number()) {
                    *channels.at(i) = std::clamp((*tint)[i].get<double>(), 0.0, 2.0);
                }
            }
        }
        struct Entry {
            const char* key;
            double* target;
            double min;
            double max;
        };
        for (const Entry& entry : {Entry{"ambient", &r.lighting.ambient, 0.0, 2.0},
                                   Entry{"sun", &r.lighting.sun, 0.0, 2.0},
                                   Entry{"sun_angle", &r.lighting.sun_angle, 0.0, 360.0},
                                   Entry{"sun_height", &r.lighting.sun_height, 5.0, 90.0},
                                   Entry{"time_of_day", &r.lighting.time_of_day, 0.0, 24.0},
                                   Entry{"day_minutes", &r.lighting.day_minutes, 0.0, 1440.0}}) {
            const auto value = lighting->find(entry.key);
            if (value != lighting->end() && value->is_number() &&
                std::isfinite(value->get<double>())) {
                *entry.target = std::clamp(value->get<double>(), entry.min, entry.max);
            }
        }
        // The weather (saves made before it was kept are clear).
        const auto kind = lighting->find("weather");
        const auto strength = lighting->find("weather_strength");
        r.lighting.set_weather(
            kind != lighting->end() && kind->is_string() ? kind->get<std::string>()
                                                         : std::string("clear"),
            strength != lighting->end() && strength->is_number() ? strength->get<double>() : 0.0);
    }
    r.steps = step->get<std::uint64_t>();
    // The scene the save was made in (saves made before scenes changed are in the first).
    if (const auto saved_scene = json.find("scene");
        saved_scene != json.end() && saved_scene->is_string()) {
        if (const assets::AssetRecord* record =
                r.assets.find(saved_scene->get_ref<const std::string&>())) {
            r.save_object.set_scene(record->path, record->guid.to_string());
        }
    }
    // The simulation starts again from the loaded world.
    r.physics->invalidate();
    // Movers already standing in trigger areas are inside, not walking in.
    static_cast<void>(update_triggers(*r.world));
    return true;
}

bool Runtime::save_to_slot(std::int64_t slot, std::string& error) const {
    const fs::path folder = impl_->save_object.folder();
    const fs::path file = save_slot_file(folder, slot);
    if (file.empty()) {
        error = "There is no save slot " + std::to_string(slot) + "; slots go from 1 to " +
                std::to_string(max_save_slot) + ".";
        return false;
    }
    return write_file_atomically(file, serialize::to_text(save()), error);
}

bool Runtime::load_from_slot(std::int64_t slot, std::vector<serialize::Issue>& issues) {
    const fs::path file = save_slot_file(impl_->save_object.folder(), slot);
    std::error_code ec;
    if (file.empty() || !fs::is_regular_file(file, ec)) {
        issues.push_back(serialize::Issue{"", "Slot " + std::to_string(slot) + " has no save."});
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<serialize::Issue> found;
    const std::optional<serialize::Json> json = serialize::parse(text, found);
    if (!json) {
        add_issues(issues, file.filename().generic_string(), std::move(found));
        return false;
    }
    return load(*json, issues);
}

std::filesystem::path Runtime::save_folder() const {
    return impl_->save_object.folder();
}

std::string Runtime::create_copy(const SaveObject::Spawn& spawn, std::string& error) {
    const Impl& r = *impl_; // the world and assets are reached through pointers
    const std::optional<Guid> guid = Guid::parse(spawn.prefab);
    const assets::AssetRecord* record = guid ? r.assets.find(*guid) : r.assets.find(spawn.prefab);
    if (record == nullptr || !record->path.ends_with(".dprefab")) {
        error = "there is no prefab " + spawn.prefab + " in assets/.";
        return {};
    }
    // Where: at something, or at a point.
    double x = spawn.x;
    double y = spawn.y;
    if (!spawn.where.empty()) {
        const world::Entity at = resolve(*r.world, spawn.where);
        const auto* place = at.is_valid() ? r.world->get<scene2d::Transform2D>(at) : nullptr;
        if (place == nullptr) {
            error = "there is no " + spawn.where + " to create it at.";
            return {};
        }
        x = place->x;
        y = place->y;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        error = "that point is not on the map.";
        return {};
    }
    const std::optional<std::vector<std::uint8_t>> bytes = r.assets.read(record->guid);
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> json = bytes ? parse_bytes(*bytes, issues) : std::nullopt;
    if (!json || !json->contains("entities") || !(*json)["entities"].is_array() ||
        (*json)["entities"].empty()) {
        error = record->path + " cannot be read as a prefab.";
        return {};
    }
    const serialize::SceneLoad loaded =
        serialize::scene_from_json(serialize::with_new_guids(*json), *r.world);
    if (loaded.entities.empty()) {
        error = loaded.issues.empty() ? std::string("the prefab is empty.")
                                      : loaded.issues.front().message;
        return {};
    }
    const world::Entity root = loaded.entities.front();
    auto* place = r.world->get<scene2d::Transform2D>(root);
    if (place == nullptr) {
        place = r.world->add<scene2d::Transform2D>(root);
    }
    if (place != nullptr) {
        place->x = x;
        place->y = y;
    }
    // A name of its own: the prefab's, numbered after the first.
    std::string base(r.world->name(root));
    if (base.empty()) {
        base = fs::path(record->path).stem().string();
    }
    std::string name = base;
    for (int n = 2; r.world->find_named(name).size() > (name == base ? 1U : 0U); ++n) {
        name = base + "_" + std::to_string(n);
    }
    r.world->set_name(root, name);
    return name;
}

void Runtime::footsteps(double walked, std::array<double, 2> at) {
    Impl& r = *impl_;
    const reflect::TypeInfo* mover_type = r.world->registry().find_type<TopDownMover>();
    if (mover_type == nullptr || !(walked > 0.0) || walked > 4.0) {
        return; // standing, or moved by more than walking (a door)
    }
    const TopDownMover* player = nullptr;
    r.world->each(*mover_type, [&](world::Entity e, reflect::ObjectRef ref) {
        const Walker* walker = r.world->get<Walker>(e);
        if (player == nullptr && (walker == nullptr || !walker->enabled)) {
            player = ref.as<TopDownMover>();
        }
    });
    if (player == nullptr) {
        return;
    }
    r.stepped += walked;
    if (r.stepped < std::max(player->step_length, 0.1)) {
        return;
    }
    r.stepped = 0.0;
    // The ground under the player's feet: the footsteps of a map with a tile there.
    std::string sound = player->footsteps;
    const double ppu = std::max<std::uint32_t>(r.settings.ppu, 1);
    if (const reflect::TypeInfo* type = r.world->registry().find_type<scene2d::Tilemap>()) {
        r.world->each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
            const auto* map = ref.as<scene2d::Tilemap>();
            const auto* origin = r.world->get<scene2d::Transform2D>(e);
            if (map == nullptr || origin == nullptr || map->footsteps.empty()) {
                return;
            }
            std::string error;
            const scene2d::TileGrid* grid = map->grid(error);
            const double tile =
                static_cast<double>(map->tile_size == 0 ? ppu : map->tile_size) / ppu;
            const double column = std::floor((at[0] - origin->x) / tile);
            const double row = std::floor((at[1] - 0.01 - origin->y) / tile);
            if (grid != nullptr && column >= 0 && row >= 0 &&
                grid->at(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row)) >=
                    0) {
                sound = map->footsteps;
            }
        });
    }
    if (!sound.empty()) {
        // Each step a little different, as feet are.
        const std::array<double, 3> pitches{1.0, 0.94, 1.06};
        r.audio_object.play_voice(sound, pitches.at(r.steps_taken++ % pitches.size()));
    }
}

std::string Runtime::hud_value(std::string_view name) const {
    const Impl& r = *impl_;
    const std::size_t colon = name.find(':');
    const std::string_view kind = name.substr(0, colon);
    const std::string_view of =
        colon == std::string_view::npos ? std::string_view{} : name.substr(colon + 1);
    const auto number = [](double v) { return engine::text::format_number(v); };
    if (kind == "var") {
        for (const std::string& script : r.vm->scripts()) {
            for (const auto& [variable, value] : r.vm->variables(script)) {
                if (variable == of) {
                    if (const auto* whole = std::get_if<std::int64_t>(&value)) {
                        return std::to_string(*whole);
                    }
                    if (const auto* real = std::get_if<double>(&value)) {
                        return number(*real);
                    }
                    if (const auto* words = std::get_if<std::string>(&value)) {
                        return *words;
                    }
                    if (const auto* yes = std::get_if<bool>(&value)) {
                        return *yes ? "yes" : "no";
                    }
                }
            }
        }
        return "?";
    }
    if (kind == "time") {
        const auto minutes = static_cast<int>(r.lighting.time_of_day * 60.0) % (24 * 60);
        const std::string mm = std::to_string(minutes % 60);
        return std::to_string(minutes / 60) + ":" + (mm.size() == 1 ? "0" + mm : mm);
    }
    if (kind == "timer") {
        return std::to_string(
            static_cast<int>(std::ceil(r.save_object.time_left(std::string(of)))));
    }
    if (kind == "hp" || kind == "max_hp" || kind == "mp" || kind == "max_mp" || kind == "level") {
        const std::string member(of);
        BattleData data;
        data.party = r.tables.find("data/party");
        data.items = r.tables.find("data/items");
        data.levels = r.tables.find("data/levels");
        data.member_levels = r.party.levels();
        data.member_equipment = r.party.all_equipment();
        std::string error;
        std::optional<Fighter> f = make_fighter(data, member, false, error);
        if (!f) {
            return "?";
        }
        if (const auto known = r.party.condition().find(member);
            known != r.party.condition().end()) {
            f->hp = std::clamp<std::int64_t>(known->second.first, 0, f->max_hp);
            f->mp = std::clamp<std::int64_t>(known->second.second, 0, f->max_mp);
        }
        const std::int64_t v = kind == "hp"       ? f->hp
                               : kind == "max_hp" ? f->max_hp
                               : kind == "mp"     ? f->mp
                               : kind == "max_mp" ? f->max_mp
                                                  : f->level;
        return std::to_string(v);
    }
    // A bare name, or item:name: how many of it the player carries.
    return std::to_string(r.inventory.count_of(std::string(kind == "item" ? of : name)));
}

void Runtime::draw_hud() {
    Impl& r = *impl_;
    const ui::PixelFont& font = r.font_face;
    const auto sw = static_cast<float>(r.target->width());
    const auto sh = static_cast<float>(r.target->height());
    // Where a thing of w x h pixels goes for `anchor`, `x` and `y` in from it.
    const auto place = [&](ScreenAnchor anchor, double x, double y, float w, float h) {
        return screen_place(anchor, x, y, w, h, sw, sh);
    };
    const auto fill = [this](const std::string& text) {
        return fill_values(text, [this](std::string_view name) { return hud_value(name); });
    };
    const auto numeric = [](const std::string& text) {
        try {
            return std::stod(text);
        } catch (const std::exception&) {
            return 0.0;
        }
    };
    const render::Rect one{0.0F, 0.0F, 1.0F, 1.0F};

    // Screen Layouts first: where their children's elements go, or that they are hidden.
    // An element's size, as drawn below: a picture's, a bar's with its frame, words'.
    const auto picture_texture = [&r](const ScreenPicture& picture) -> const render::Texture* {
        const std::optional<Guid> guid =
            picture.visible ? Guid::parse(picture.image) : std::nullopt;
        return guid && r.textures != nullptr ? r.textures->get(*guid) : nullptr;
    };
    const auto size_of = [&](world::Entity e) -> std::optional<std::array<float, 2>> {
        if (const auto* picture = r.world->get<ScreenPicture>(e)) {
            if (const render::Texture* texture = picture_texture(*picture)) {
                return std::array<float, 2>{
                    picture->width > 0.0 ? static_cast<float>(picture->width)
                                         : static_cast<float>(texture->width()),
                    picture->height > 0.0 ? static_cast<float>(picture->height)
                                          : static_cast<float>(texture->height())};
            }
        }
        if (const auto* bar = r.world->get<ScreenBar>(e); bar != nullptr && bar->visible) {
            return std::array<float, 2>{static_cast<float>(bar->width) + 2.0F,
                                        static_cast<float>(bar->height) + 2.0F};
        }
        if (const auto* label = r.world->get<ScreenText>(e); label != nullptr && label->visible) {
            const std::string words = r.translations.translate(fill(label->text));
            const int size = label->size >= 2 ? 2 : 1;
            return std::array<float, 2>{static_cast<float>(ui::measure(font, words) * size),
                                        static_cast<float>(font.line_height() * size)};
        }
        return std::nullopt;
    };
    std::unordered_map<std::uint64_t, std::array<float, 2>> laid;
    std::unordered_set<std::uint64_t> hidden;
    if (const reflect::TypeInfo* type = r.world->registry().find_type<ScreenLayout>()) {
        r.world->each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
            const auto* layout = ref.as<ScreenLayout>();
            if (layout == nullptr) {
                return;
            }
            std::vector<std::uint64_t> members;
            std::vector<std::array<float, 2>> sizes;
            for (const world::Entity child : r.world->children(e)) {
                if (!layout->visible) {
                    hidden.insert(child.id);
                } else if (const std::optional<std::array<float, 2>> size = size_of(child)) {
                    members.push_back(child.id);
                    sizes.push_back(*size);
                }
            }
            const std::vector<std::array<float, 2>> at = lay_out(*layout, sizes, sw, sh);
            for (std::size_t i = 0; i < members.size(); ++i) {
                laid[members[i]] = at[i];
            }
        });
    }
    // Where an element goes: its layout's place for it, or its own.
    const auto where = [&laid](world::Entity e, std::array<float, 2> own) {
        const auto found = laid.find(e.id);
        return found != laid.end() ? found->second : own;
    };
    r.sprites->begin_over(*r.target);
    if (const reflect::TypeInfo* type = r.world->registry().find_type<ScreenPicture>();
        type != nullptr && r.textures != nullptr) {
        r.world->each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
            if (hidden.contains(e.id)) {
                return;
            }
            const auto* picture = ref.as<ScreenPicture>();
            const std::optional<Guid> guid =
                picture != nullptr && picture->visible ? Guid::parse(picture->image) : std::nullopt;
            const render::Texture* texture = guid ? r.textures->get(*guid) : nullptr;
            if (texture == nullptr) {
                return;
            }
            const auto tw = static_cast<float>(texture->width());
            const auto th = static_cast<float>(texture->height());
            const float w = picture->width > 0.0 ? static_cast<float>(picture->width) : tw;
            const float h = picture->height > 0.0 ? static_cast<float>(picture->height) : th;
            const auto [x, y] = where(e, place(picture->anchor, picture->x, picture->y, w, h));
            const auto alpha =
                static_cast<std::uint8_t>(std::clamp(picture->opacity, 0.0, 1.0) * 255.0);
            for (const SlicePiece& piece : nine_slice(tw, th, render::Rect{x, y, w, h},
                                                      static_cast<float>(picture->border))) {
                r.sprites->draw(*texture, piece.source, piece.destination,
                                render::Color{255, 255, 255, alpha});
            }
        });
    }
    if (const reflect::TypeInfo* type = r.world->registry().find_type<ScreenBar>()) {
        r.world->each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
            if (hidden.contains(e.id)) {
                return;
            }
            const auto* bar = ref.as<ScreenBar>();
            if (bar == nullptr || !bar->visible) {
                return;
            }
            const double most = numeric(fill(bar->most));
            const double now = numeric(fill(bar->value));
            const double part = most > 0.0 ? std::clamp(now / most, 0.0, 1.0) : 0.0;
            const auto w = static_cast<float>(bar->width);
            const auto h = static_cast<float>(bar->height);
            const auto [x, y] = where(e, place(bar->anchor, bar->x, bar->y, w + 2.0F, h + 2.0F));
            r.sprites->draw(*r.white, one, render::Rect{x, y, w + 2.0F, h + 2.0F},
                            r.settings.ui.border);
            r.sprites->draw(*r.white, one, render::Rect{x + 1.0F, y + 1.0F, w, h},
                            r.settings.ui.box);
            r.sprites->draw(
                *r.white, one,
                render::Rect{x + 1.0F, y + 1.0F, std::floor(w * static_cast<float>(part)), h},
                hex_color(bar->color, render::Color{220, 70, 70, 255}));
        });
    }
    if (const reflect::TypeInfo* type = r.world->registry().find_type<ScreenText>()) {
        r.world->each(*type, [&](world::Entity e, reflect::ObjectRef ref) {
            if (hidden.contains(e.id)) {
                return;
            }
            const auto* label = ref.as<ScreenText>();
            if (label == nullptr || !label->visible) {
                return;
            }
            const std::string words = r.translations.translate(fill(label->text));
            const int size = label->size >= 2 ? 2 : 1;
            const auto w = static_cast<float>(ui::measure(font, words) * size);
            const auto h = static_cast<float>(font.line_height() * size);
            const auto [x, y] = where(e, place(label->anchor, label->x, label->y, w, h));
            // A shadow, so the words read over any ground.
            ui::draw_text(*r.sprites, *r.font, font, words, x + 1.0F, y + 1.0F,
                          render::Color{0, 0, 0, 180}, size);
            ui::draw_text(*r.sprites, *r.font, font, words, x, y, r.settings.ui.text, size);
        });
    }
    static_cast<void>(r.sprites->end());
}

std::size_t Runtime::reload_changed() {
    Impl& r = *impl_;
    const assets::AssetChanges changes = r.assets.refresh();
    if (changes.empty()) {
        return 0;
    }
    if (r.textures != nullptr) {
        r.textures->apply(changes);
        r.find_box_frame();
    }
    if (r.sheets != nullptr) {
        r.sheets->apply(changes);
    }
    r.tables.forget(); // read again on next use
    r.translations = Translations(r.tables.find(translations_table));
    apply_options();
    return changes.added.size() + changes.modified.size() + changes.moved.size() +
           changes.removed.size();
}

void Runtime::save_achievements() {
    Impl& r = *impl_;
    const fs::path folder = r.save_object.folder();
    serialize::Json names = serialize::Json::array();
    for (const std::string& name : r.save_object.achievements()) {
        names.push_back(name);
    }
    std::string error;
    if (folder.empty() ||
        !write_file_atomically(folder / achievements_file,
                               serialize::to_text(serialize::Json{{"unlocked", names}}), error)) {
        r.problems.push_back("Achievements not kept: " +
                             (error.empty() ? std::string("no save folder") : error));
    }
}

void Runtime::load_options() {
    Impl& r = *impl_;
    const fs::path folder = r.save_object.folder();
    if (!folder.empty()) {
        std::ifstream in(folder / options_file, std::ios::binary);
        if (in) {
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            std::vector<serialize::Issue> ignored; // a broken file keeps the defaults
            if (const std::optional<serialize::Json> json = serialize::parse(text, ignored)) {
                input::InputMap map = r.state.map();
                options_from_json(*json, r.menu.options(), map);
                r.state.set_map(std::move(map));
            }
        }
    }
    refresh_option_keys(r.menu.options(), r.state.map());
    apply_options();
    // The player's achievements, from every game played before.
    if (!folder.empty()) {
        std::ifstream in(folder / achievements_file, std::ios::binary);
        if (in) {
            const std::string text((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            std::vector<serialize::Issue> ignored;
            const std::optional<serialize::Json> json = serialize::parse(text, ignored);
            std::set<std::string, std::less<>> names;
            if (json && json->contains("unlocked") && (*json)["unlocked"].is_array()) {
                for (const serialize::Json& name : (*json)["unlocked"]) {
                    if (name.is_string()) {
                        names.insert(name.get<std::string>());
                    }
                }
            }
            r.save_object.set_achievements(std::move(names));
        }
    }
}

void Runtime::save_options() {
    Impl& r = *impl_;
    const fs::path folder = r.save_object.folder();
    std::string error;
    if (folder.empty() ||
        !write_file_atomically(folder / options_file,
                               serialize::to_text(options_to_json(r.menu.options(), r.state.map())),
                               error)) {
        r.menu.say("Options not kept: " + (error.empty() ? std::string("no save folder") : error));
    }
}

void Runtime::apply_options() {
    Impl& r = *impl_;
    // The player's language, else the game's own.
    r.translations.set_language(r.menu.options().language.empty() ? r.settings.language
                                                                  : r.menu.options().language);
    if (r.audio_engine != nullptr) {
        r.audio_engine->set_player_volume(audio::Bus::Music, r.menu.options().music);
        r.audio_engine->set_player_volume(audio::Bus::Sounds, r.menu.options().sounds);
    }
}

bool Runtime::change_scene(std::string_view scene, std::string_view where,
                           std::vector<serialize::Issue>& issues) {
    Impl& r = *impl_;
    r.camera.placed = false; // it jumps to the new scene rather than gliding
    r.player_was.reset();    // arriving is not walking
    const std::optional<Guid> guid = Guid::parse(scene);
    const assets::AssetRecord* record = guid ? r.assets.find(*guid) : r.assets.find(scene);
    if (record == nullptr || !record->path.ends_with(".dscene")) {
        issues.push_back(
            serialize::Issue{"", "There is no scene " + std::string(scene) + " in assets/."});
        return false;
    }
    const std::string file = "assets/" + record->path;
    const std::optional<std::vector<std::uint8_t>> bytes = r.assets.read(record->guid);
    std::vector<serialize::Issue> found;
    const std::optional<serialize::Json> json = bytes ? parse_bytes(*bytes, found) : std::nullopt;
    if (!json) {
        if (found.empty()) {
            found.push_back(serialize::Issue{"", "The scene cannot be read."});
        }
        add_issues(issues, file, std::move(found));
        return false;
    }
    // Checked in a scratch world first, so a broken scene changes nothing.
    {
        world::World scratch(r.registry);
        scene2d::register_components(scratch);
        register_behaviour_components(scratch);
        serialize::SceneLoad trial = serialize::scene_from_json(*json, scratch);
        if (trial.entities.empty() && !trial.issues.empty()) {
            add_issues(issues, file, std::move(trial.issues));
            return false;
        }
    }
    for (const world::Entity root : r.world->roots()) {
        r.world->destroy(root);
    }
    serialize::SceneLoad loaded = serialize::scene_from_json(*json, *r.world);
    add_issues(issues, file, std::move(loaded.issues));
    r.save_object.set_scene(record->path, record->guid.to_string());
    // The player arrives where the named entity is.
    if (!where.empty()) {
        const world::Entity spot = resolve(*r.world, where);
        const scene2d::Transform2D* at =
            spot.is_valid() ? r.world->get<scene2d::Transform2D>(spot) : nullptr;
        const reflect::TypeInfo* mover_type = r.world->registry().find_type<TopDownMover>();
        world::Entity player;
        if (mover_type != nullptr) {
            r.world->each(*mover_type, [&](world::Entity entity, reflect::ObjectRef) {
                if (!player.is_valid() && r.world->get<scene2d::Transform2D>(entity) != nullptr) {
                    player = entity;
                }
            });
        }
        if (at == nullptr) {
            issues.push_back(serialize::Issue{file, "There is nothing called " +
                                                        std::string(where) + " to arrive at."});
        } else if (player.is_valid()) {
            const double x = at->x;
            const double y = at->y;
            scene2d::Transform2D* feet = r.world->get<scene2d::Transform2D>(player);
            feet->x = x;
            feet->y = y;
        }
    }
    // The simulation starts again from the new world; movers already standing in trigger
    // areas are inside, not walking in.
    r.physics->invalidate();
    static_cast<void>(update_triggers(*r.world));
    r.vm->emit("", "scene_start");
    return true;
}

double Runtime::fade() const noexcept {
    const Impl& r = *impl_;
    if (r.fading == Impl::Fade::None || r.fade_length == 0) {
        return 0.0;
    }
    return static_cast<double>(r.fade_steps) / static_cast<double>(r.fade_length);
}

const std::string& Runtime::scene() const noexcept {
    return impl_->save_object.scene();
}

const std::vector<std::string>& Runtime::problems() const noexcept {
    return impl_->problems;
}

const LightingObject& Runtime::lighting() const noexcept {
    return impl_->lighting;
}

const InventoryObject& Runtime::inventory() const noexcept {
    return impl_->inventory;
}

const QuestObject& Runtime::quests() const noexcept {
    return impl_->quests;
}

const PartyObject& Runtime::party() const noexcept {
    return impl_->party;
}

const GameMenu& Runtime::menu() const noexcept {
    return impl_->menu;
}

const BattleScreen* Runtime::battle_screen() const noexcept {
    if (const std::optional<BattleScreen>& screen = impl_->battle_screen; screen.has_value()) {
        return &*screen;
    }
    return nullptr;
}

const DialogueObject& Runtime::dialogue() const noexcept {
    return impl_->dialogue;
}

DialogueObject& Runtime::dialogue() noexcept {
    return impl_->dialogue;
}

audio::AudioEngine* Runtime::audio() noexcept {
    return impl_->audio_engine.get();
}

} // namespace engine::game
