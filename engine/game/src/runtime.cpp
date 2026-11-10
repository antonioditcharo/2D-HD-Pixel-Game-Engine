#include "engine/game/runtime.h"

#include "engine/foundation/text.h"
#include "engine/game/logic_world.h"
#include "engine/logic/compiler.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
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
constexpr std::array<std::string_view, 12> known_project_keys{
    "format",     "version",      "name",         "start_scene",     "ppu",       "width", "height",
    "background", "render_style", "camera_pitch", "camera_distance", "camera_fov"};

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
    json["render_style"] = settings.style == RenderStyle::Diorama ? "diorama" : "classic_2d";
    json["camera_pitch"] = settings.camera_pitch;
    json["camera_distance"] = settings.camera_distance;
    json["camera_fov"] = settings.camera_fov;
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
    register_behaviours(registry);
    register_save_types(registry);
    register_audio_types(registry);
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
    SaveObject save_object;
    std::unique_ptr<audio::AudioEngine> audio_engine;
    AudioObject audio_object;
    std::vector<std::string> problems;
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
    register_behaviour_components(*r.world);

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
    // Saves go where asked, or else to the player's own folder for this game, found (and
    // created) only when a save is first used.
    r.save_object = SaveObject([folder = options.save_folder, name = r.settings.name,
                                root = fs::path(root)]() -> fs::path {
        if (!folder.empty()) {
            return folder;
        }
        char* pref = SDL_GetPrefPath("Sparxel", name.c_str());
        if (pref == nullptr) {
            return root / "saves";
        }
        fs::path path(std::u8string(reinterpret_cast<const char8_t*>(pref)));
        SDL_free(pref);
        return path;
    });
    // Sound: a missing sound output is reported, and the game runs silent.
    if (options.audio != AudioMode::Off) {
        std::string error;
        r.audio_engine = audio::AudioEngine::create(
            audio::AudioOptions{options.audio == AudioMode::Headless, 48000, 2}, error);
        if (r.audio_engine == nullptr) {
            issues.push_back(serialize::Issue{"", "No sound: " + error});
        }
    }
    r.audio_object = AudioObject(r.audio_engine.get(), &r.assets);
    if (!r.vm->bind("audio", reflect::ObjectRef::of(r.audio_object))) {
        issues.push_back(
            serialize::Issue{"", "The audio object could not be made available to logic."});
    }
    if (!r.vm->bind("game", reflect::ObjectRef::of(r.save_object))) {
        issues.push_back(
            serialize::Issue{"", "The game object could not be made available to logic."});
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
            r.window = r.device->create_window(
                options.window_title.empty() ? r.settings.name : options.window_title,
                static_cast<std::uint32_t>(fit.width), static_cast<std::uint32_t>(fit.height));
        }
        if (r.settings.style == RenderStyle::Diorama) {
            r.world_renderer = render::WorldRenderer::create(*r.device);
        }
        if (r.target == nullptr || r.sprites == nullptr ||
            (r.settings.style == RenderStyle::Diorama && r.world_renderer == nullptr) ||
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

    // 2. Movement, with walls, bodies and walk animations; then trigger areas.
    const double dt = r.clock.step_seconds();
    move_characters(*r.world, r.state, dt, r.settings.ppu);
    for (const TriggerEvent& event : update_triggers(*r.world)) {
        const std::vector<std::string> who = names_of(*r.world, event.who);
        const std::array<reflect::Value, 1> payload{
            reflect::Value{who.empty() ? std::string() : who.back()}};
        emit(*r.vm, *r.world, event.trigger, event.entered ? "entered" : "exited", payload);
    }

    // 3. Events: each pressed action runs the Rules listening for it. Interact also
    //    reaches what each mover faces, in mover GUID order.
    for (const std::string& action : r.state.pressed_actions()) {
        const std::array<reflect::Value, 1> payload{reflect::Value{action}};
        r.vm->emit("input", "pressed", payload);
        if (action != "interact") {
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

    // 5. Animation; animations that reached their end tell Rules they finished.
    for (const world::Entity entity : scene2d::animate(*r.world, dt)) {
        emit(*r.vm, *r.world, entity, "finished");
    }
    ++r.steps;

    // 6. Saving and loading that logic asked for, now that no script is running.
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
    if (r.settings.style == RenderStyle::Diorama) {
        scene2d::DioramaCamera orbit;
        orbit.x = camera.x;
        orbit.y = camera.y;
        orbit.pitch = r.settings.camera_pitch;
        orbit.distance = r.settings.camera_distance;
        orbit.fov = r.settings.camera_fov;
        orbit.ppu = r.settings.ppu;
        scene2d::draw_diorama(*r.world, *r.textures, *r.world_renderer, *r.target, orbit,
                              r.settings.background);
        return true;
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
    json["world"] = serialize::scene_to_json(*r.world);
    serialize::Json variables = serialize::Json::object();
    for (const std::string& script : r.vm->scripts()) {
        serialize::Json values = serialize::Json::object();
        for (const auto& [name, value] : r.vm->variables(script)) {
            std::visit([&values, &name](const auto& v) { values[name] = v; }, value);
        }
        variables[script] = std::move(values);
    }
    json["variables"] = std::move(variables);
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

    // Replace the world, stop what was running, and restore the variables.
    r.vm->stop_all();
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
    r.steps = step->get<std::uint64_t>();
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

const std::vector<std::string>& Runtime::problems() const noexcept {
    return impl_->problems;
}

audio::AudioEngine* Runtime::audio() noexcept {
    return impl_->audio_engine.get();
}

} // namespace engine::game
