#include "engine/game/runtime.h"

#include "engine/game/logic_world.h"
#include "engine/logic/compiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

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
constexpr std::array<std::string_view, 9> known_project_keys{
    "format", "version", "name",       "start_scene", "ppu",
    "width",  "height",  "background", "render_style"};

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
    if (!json || !json->is_object()) {
        if (json) {
            found.push_back(
                serialize::Issue{"", "The project file must be a group of named values."});
        }
        add_issues(issues, file, std::move(found));
        return std::nullopt;
    }
    const serialize::Json& doc = *json;
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
    if (const auto style = doc.find("render_style");
        style != doc.end() &&
        !(style->is_string() && style->get_ref<const std::string&>() == "classic_2d")) {
        issues.push_back(serialize::Issue{file + "/render_style",
                                          "Only the Classic 2D render style (\"classic_2d\") is "
                                          "available in this version."});
        ok = false;
    }
    return ok ? std::optional<ProjectSettings>(std::move(settings)) : std::nullopt;
}

serialize::Json project_to_json(const ProjectSettings& settings) {
    serialize::Json json = serialize::Json::object();
    json["format"] = project_format;
    json["version"] = project_version;
    json["name"] = settings.name;
    json["start_scene"] = settings.start_scene;
    json["render_style"] = "classic_2d";
    json["ppu"] = settings.ppu;
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
    auto mover = registry.add_type<TopDownMover>("top_down_mover")
                     .display_name("Top-down Mover")
                     .category("Movement")
                     .tooltip("Walks in eight directions with the move actions.");
    mover.property<&TopDownMover::speed>("speed").range(0.0, 64.0).step(0.5).tooltip(
        "How fast it walks, in tiles per second.");
    mover.property<&TopDownMover::enabled>("enabled").tooltip(
        "Turn off to stop the player moving, for example in a cutscene.");
    mover.property<&TopDownMover::hitbox_width>("hitbox_width")
        .range(0.0, 16.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .flags(reflect::property_flag::advanced)
        .display_name("Hitbox width")
        .tooltip("How wide the feet are, for bumping into walls.");
    mover.property<&TopDownMover::hitbox_height>("hitbox_height")
        .range(0.0, 16.0)
        .step(0.125)
        .unit(reflect::Unit::Tiles)
        .flags(reflect::property_flag::advanced)
        .display_name("Hitbox height")
        .tooltip("How deep the feet are, for bumping into walls.");
}

scene2d::Box hitbox(const TopDownMover& mover, double x, double y) {
    const double half = std::max(mover.hitbox_width, 0.0) / 2.0;
    return scene2d::Box{x - half, y - std::max(mover.hitbox_height, 0.0), x + half, y};
}

// ---------------------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------------------

struct Runtime::Impl {
    explicit Impl(ProjectSettings project, const fs::path& root)
        : settings(std::move(project)), assets(root / "assets"),
          state(input::InputMap::rpg_preset()), input_object(&state) {}

    // Members are destroyed in reverse order: GPU objects before the device, the window
    // before the device, and the VM before the input and world it reaches into.
    ProjectSettings settings;
    reflect::Registry registry;
    assets::AssetDatabase assets;
    std::unique_ptr<world::World> world;
    input::InputState state;
    input::InputObject input_object;
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
    std::unique_ptr<scene2d::TextureCache> textures;
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
    r.world->register_component<TopDownMover>();

    // Assets.
    r.assets.refresh();
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
    r.vm->set_log_handler(
        [&r](std::string_view, std::string_view text) { r.log.emplace_back(text); });
    const logic::ObjectTypes types = [&r](std::string_view name) {
        return r.vm->object_types(name);
    };
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
            r.window =
                r.device->create_window(options.window_title, static_cast<std::uint32_t>(fit.width),
                                        static_cast<std::uint32_t>(fit.height));
        }
        if (r.target == nullptr || r.sprites == nullptr ||
            (!options.headless && r.window == nullptr)) {
            issues.push_back(serialize::Issue{"", r.device->last_error()});
            return nullptr;
        }
        r.textures = std::make_unique<scene2d::TextureCache>(*r.device, r.assets);
    }

    r.vm->emit("", "game_start");
    return std::unique_ptr<Runtime>(new Runtime(std::move(impl)));
}

void Runtime::step() {
    Impl& r = *impl_;
    // 1. Input (plan/05 §5).
    r.state.begin_step();
    if (r.replay != nullptr) {
        r.replay->replay(r.steps, r.state);
    } else if (r.window != nullptr) {
        r.keep_running = input::pump_events(r.state) && r.keep_running;
    }

    // 2. Movement.
    const double dt = r.clock.step_seconds();
    if (const reflect::TypeInfo* mover_type = r.registry.find_type<TopDownMover>()) {
        r.world->each(*mover_type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const TopDownMover* mover = ref.as<TopDownMover>();
            scene2d::Transform2D* transform = r.world->get<scene2d::Transform2D>(entity);
            if (mover == nullptr || transform == nullptr || !mover->enabled) {
                return;
            }
            const double dx =
                (r.state.held("move_right") ? 1.0 : 0.0) - (r.state.held("move_left") ? 1.0 : 0.0);
            const double dy =
                (r.state.held("move_down") ? 1.0 : 0.0) - (r.state.held("move_up") ? 1.0 : 0.0);
            const double length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.0) {
                const double step = mover->speed * dt / length;
                // One axis at a time, so a diagonal into a wall slides along it.
                transform->x += scene2d::sweep(*r.world, hitbox(*mover, transform->x, transform->y),
                                               scene2d::Axis::X, dx * step, r.settings.ppu);
                transform->y += scene2d::sweep(*r.world, hitbox(*mover, transform->x, transform->y),
                                               scene2d::Axis::Y, dy * step, r.settings.ppu);
            }
        });
    }

    // 3. Events: each pressed action runs the Rules listening for it.
    for (const std::string& action : r.state.pressed_actions()) {
        const std::array<reflect::Value, 1> payload{reflect::Value{action}};
        r.vm->emit("input", "pressed", payload);
    }

    // 4. Waiting Sequences, and events emitted while scripts ran.
    r.vm->update(dt);

    // 5. Animation; animations that reached their end tell Rules they finished.
    for (const world::Entity entity : scene2d::animate(*r.world, dt)) {
        emit(*r.vm, *r.world, entity, "finished");
    }
    ++r.steps;
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
    if (r.device == nullptr) {
        return false;
    }
    // The camera follows the first enabled Top-down Mover, else looks at the origin.
    scene2d::Camera2D camera;
    camera.ppu = r.settings.ppu;
    if (const reflect::TypeInfo* mover_type = r.registry.find_type<TopDownMover>()) {
        bool found = false;
        r.world->each(*mover_type, [&](world::Entity entity, reflect::ObjectRef) {
            const scene2d::Transform2D* transform = r.world->get<scene2d::Transform2D>(entity);
            if (!found && transform != nullptr) {
                camera.x = transform->x;
                camera.y = transform->y;
                found = true;
            }
        });
    }
    scene2d::draw_world(*r.world, *r.textures, *r.sprites, *r.target, camera,
                        r.settings.background);
    return true;
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

} // namespace engine::game
