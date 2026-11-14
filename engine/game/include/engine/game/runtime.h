#pragma once

// The game runtime: opens a project folder and runs it. It loads the settings, assets,
// start scene and logic, then runs fixed steps of input, movement and logic, drawn
// pixel-perfect to a window (plan/03 §7, plan/05 §5). This is what an exported game runs,
// and what the editor's Play button will run.
//
// See docs/dev/game.md.

#include "engine/assets/assets.h"
#include "engine/foundation/reflect.h"
#include "engine/game/battle_screen.h"
#include "engine/game/behaviours.h"
#include "engine/game/camera.h"
#include "engine/game/clock.h"
#include "engine/game/conversation.h"
#include "engine/game/debug_menu.h"
#include "engine/game/dialogue.h"
#include "engine/game/inventory.h"
#include "engine/game/lighting.h"
#include "engine/game/menus.h"
#include "engine/game/particles.h"
#include "engine/game/quests.h"
#include "engine/game/save.h"
#include "engine/game/sound.h"
#include "engine/game/tables.h"
#include "engine/game/ui_theme.h"
#include "engine/input/input.h"
#include "engine/logic/vm.h"
#include "engine/render/render.h"
#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"
#include "engine/world/world.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

// ---------------------------------------------------------------------------------------
// Project settings
// ---------------------------------------------------------------------------------------

/// The file at the root of every project (plan/03 §4).
inline constexpr std::string_view project_file = "project.sparxel.json";
inline constexpr std::string_view project_format = "sparxel.project";
inline constexpr int project_version = 1;

/// How the game is drawn (plan/04 §3 and §4).
enum class RenderStyle : std::uint8_t {
    Classic2D, ///< "classic_2d": flat, pixel-perfect 2D.
    Diorama,   ///< "diorama": the same world in 3D, the HD-2D look.
};

struct ProjectSettings {
    std::string name;
    RenderStyle style = RenderStyle::Classic2D;
    std::uint32_t ppu = 16;    ///< Pixels per world unit (tile), 8 to 64 (plan/04 §2).
    std::uint32_t width = 320; ///< The internal resolution, upscaled pixel-perfect.
    std::uint32_t height = 180;
    std::string start_scene; ///< The scene asset to start in, a path in assets/.
    /// The language the game shows its words in: a column of data/translations ("fr"),
    /// "pseudo" to check translations, or empty for the words as written.
    std::string language;
    /// The languages "Collect texts to translate" makes columns for: "fr, de".
    std::string languages;
    /// The style guide's palette (plan/02 A11): the colours the game's art keeps to,
    /// "#1a1c2c #5d275d ...". New sprites start with it, and Check the project notes
    /// pictures with colours outside it. Empty: no palette is kept to.
    std::string palette;
    /// The game's font (plan/02 G7): a picture in assets/ of letters side by side, each
    /// separated by a column of the top-left pixel's colour (ui::PixelFont::from_strip()).
    /// Empty: the built-in font.
    std::string font;
    /// The font picture's letters, in order (UTF-8); empty: printable ASCII from the space
    /// to '~'.
    std::string font_characters;
    /// The picture every box is drawn with (messages, menus, shops, battles), a path in
    /// assets/, stretched by its 9-slice edge; empty: the UI look's colours.
    std::string box_frame;
    double box_frame_edge = 3.0; ///< Pixels, 0 to 64.
    /// The colours of the menus and messages.
    UiTheme ui;
    /// A small map of the tiles around the player in the top-right corner.
    bool minimap = false;
    render::Color background{20, 24, 40, 255};
    /// The Diorama camera: how far it looks down (5 to 85 degrees), how far it stands from
    /// the player (1 to 200 tiles), and its vertical field of view (5 to 90 degrees).
    double camera_pitch = 35.0;
    double camera_distance = 12.0;
    double camera_fov = 30.0;
    /// The camera stops at the edges of the map (the tilemaps), so nothing beyond them
    /// shows; a map smaller than the screen is centred.
    bool camera_clamp = true;
    /// How the camera follows the player, in either style (CameraObject): smoothing in
    /// seconds (0 to 2), a dead zone and a look-ahead in tiles (0 to 16, 0 to 8).
    double camera_smoothing = 0.0;
    /// Day and night: how many real minutes a game day lasts (0: no clock, the light as set
    /// above), and the hour a game starts at (0 to 24).
    /// Ready-made menus: a title screen before the game starts (New game, Continue,
    /// Quit), and a pause menu on the Menu key (Resume, Items, Quests, Party, Save, Load,
    /// Quit). Off by default: Rules then decide what the Menu key does.
    bool title_screen = false;
    bool pause_menu = false;
    /// The music's volume while a message shows, 0 to 1 of itself: ducking, so words and
    /// voices come through. 1 leaves it.
    double music_while_talking = 0.6;
    /// How fast messages are typed out, in letters a second; 0 shows each whole at once.
    double text_speed = 0.0;
    double day_minutes = 0.0;
    double start_hour = 12.0;
    double camera_dead_zone = 0.0;
    double camera_look_ahead = 0.0;
    /// The Diorama style's light at the start: ambient (0 to 2) and the sun (0 to 2).
    /// Logic changes them with the "lighting" object.
    double ambient_light = 0.55;
    double sun_light = 0.45;
    /// Where the sun is at the start: degrees clockwise from north (0 to 360) and above
    /// the horizon (5 to 90). Logic moves it with the "lighting" object.
    double sun_angle = scene2d::default_sun_angle;
    double sun_height = scene2d::default_sun_height;
    /// How dark each sprite's shadow is in the Diorama style, 0 (none) to 1, and its
    /// shape: its silhouette cast away from the sun, or a round blob under its feet. In
    /// the project file, "sprite_shadow" and "shadow_shape" ("cast" or "round").
    double sprite_shadow = 0.35;
    scene2d::ShadowShape shadow_shape = scene2d::ShadowShape::Cast;
    /// How dark the sun's shadows of blocks are in the Diorama style, 0 (none) to 1.
    double block_shadow = 0.45;
    /// Post-processing for the finished frame: tilt-shift, bloom, grading, vignette. The
    /// default changes nothing. In the project file, the group "look".
    render::Look look;
    /// Gravity for Physics Bodies, in tiles per second squared, down the map being +y:
    /// (0, 0) for top-down games, (0, 30) or so for side views. -1000 to 1000 each.
    double gravity_x = 0.0;
    double gravity_y = 0.0;
    /// How long the screen takes to fade to black when logic goes to another scene, and
    /// to fade back in, in seconds: 0 (a cut) to 3. In the project file, "scene_fade".
    double scene_fade = 0.4;
};

/// Reads `root`/project.sparxel.json. Returns nothing, with issues, if it is missing or
/// invalid. Unknown entries are reported and ignored.
[[nodiscard]] std::optional<ProjectSettings> read_project(const std::filesystem::path& root,
                                                          std::vector<serialize::Issue>& issues);
/// The settings in a project file's JSON, checked as read_project() checks them (issues
/// name project.sparxel.json). Nothing if they are invalid.
[[nodiscard]] std::optional<ProjectSettings>
project_from_json(const serialize::Json& json, std::vector<serialize::Issue>& issues);
[[nodiscard]] serialize::Json project_to_json(const ProjectSettings& settings);

/// The lighting a game starts with: the project's.
[[nodiscard]] LightingObject starting_lighting(const ProjectSettings& settings);
/// The Diorama light the game draws with, from the lighting now and the project's shadows,
/// every value clamped to its range. The editor's preview uses it too.
[[nodiscard]] scene2d::DioramaLight diorama_light(const ProjectSettings& settings,
                                                  const LightingObject& lighting);

/// Registers every type the runtime uses: Classic 2D components, input, and behaviours.
void register_types(reflect::Registry& registry);

// ---------------------------------------------------------------------------------------
// The runtime
// ---------------------------------------------------------------------------------------

enum class AudioMode : std::uint8_t {
    Off,      ///< No sound at all.
    Device,   ///< The computer's sound output. If there is none, the game runs silent.
    Headless, ///< Mixed but not played: read with Runtime::audio()->render(), for tests.
};

struct RuntimeOptions {
    /// Without graphics, nothing is drawn: for tests, servers and tools. The simulation is
    /// identical either way.
    bool graphics = true;
    /// With graphics but no window: draws into an offscreen target only.
    bool headless = false;
    /// Replays this recording instead of reading the keyboard and gamepads.
    const input::Recording* replay = nullptr;
    /// Where save slots are kept. Empty: the player's own folder for this game, as the
    /// operating system suggests (on Windows, under AppData\Roaming\Sparxel).
    std::filesystem::path save_folder;
    /// The window's title; empty for the game's name.
    std::string window_title;
    AudioMode audio = AudioMode::Device;
    /// Testing from the editor: F1 opens the debug menu (debug_menu.h), F3 shows how long
    /// each frame takes. Exported games never have it.
    bool debug = false;
};

class Runtime {
public:
    /// Opens the project at `root`. Returns null, with issues, if the project cannot run:
    /// no valid settings, no start scene, or no GPU when graphics were asked for. Problems
    /// that do not stop the game, such as a logic asset that does not compile (which is
    /// then left out), are reported in `issues` too. Issue paths start with the file.
    [[nodiscard]] static std::unique_ptr<Runtime> open(const std::filesystem::path& root,
                                                       const RuntimeOptions& options,
                                                       std::vector<serialize::Issue>& issues);

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    ~Runtime();

    /// Advances real time: runs the fixed steps that are due, then draws and presents.
    /// Returns false when the player asked to quit.
    bool frame(double seconds);
    /// One fixed step: input; movement; trigger areas, input and interaction events to
    /// Rules; waiting Sequences; then animation (with "finished" events to Rules).
    void step();
    /// Draws the current state into the offscreen target. False without graphics.
    bool draw();

    [[nodiscard]] const ProjectSettings& settings() const noexcept;
    [[nodiscard]] world::World& world() noexcept;
    [[nodiscard]] logic::Vm& vm() noexcept;
    [[nodiscard]] const input::InputState& input() const noexcept;
    [[nodiscard]] std::uint64_t step_count() const noexcept;
    /// The offscreen image the game is drawn into; null without graphics.
    [[nodiscard]] render::RenderTarget* target() noexcept;
    /// Every line logic wrote with Log blocks, oldest first.
    [[nodiscard]] const std::vector<std::string>& log() const noexcept;

    /// The game's state as a save (format "sparxel.save"): the world, every logic asset's
    /// variables, and the step count.
    [[nodiscard]] serialize::Json save() const;
    /// Goes back to a saved state. Checks the whole save first and changes nothing if it
    /// cannot be used (another game's, a newer version, a broken world). Waiting Sequences
    /// stop, as a save does not hold them. Variables that no longer exist, or changed
    /// kind, keep their current values and are reported. Must not be called while logic
    /// runs; from logic, use the "game" object, which loads at the end of the step.
    bool load(const serialize::Json& save, std::vector<serialize::Issue>& issues);
    /// save() written to a slot of save_folder(), atomically.
    bool save_to_slot(std::int64_t slot, std::string& error) const;
    /// load() from a slot of save_folder().
    bool load_from_slot(std::int64_t slot, std::vector<serialize::Issue>& issues);
    /// Replaces the scene being played with `scene` (its path in assets/, or its GUID),
    /// and, with `where`, moves the player to the entity of that name in it. The scene is
    /// checked whole first: if it cannot be used, nothing changes and `issues` says why.
    /// Variables, waiting Sequences and an open message carry on; Rules listening for
    /// "scene_start" then run. Must not be called while logic runs; from logic, use the
    /// "game" object, which changes scene at the end of the step.
    bool change_scene(std::string_view scene, std::string_view where,
                      std::vector<serialize::Issue>& issues);
    /// The scene being played: its path in assets/.
    [[nodiscard]] const std::string& scene() const noexcept;
    /// How dark the screen is, from 0 to 1, while a change of scene fades out and in.
    [[nodiscard]] double fade() const noexcept;
    /// Where save slots go (see RuntimeOptions::save_folder).
    [[nodiscard]] std::filesystem::path save_folder() const;
    /// The player's options (volumes, text speed, keys) file, in the save folder.
    static constexpr const char* options_file = "options.json";
    /// The achievements the player unlocked, in the save folder, across every save.
    static constexpr const char* achievements_file = "achievements.json";
    /// Problems with what logic asked for (saving, loading, playing sounds), oldest first.
    [[nodiscard]] const std::vector<std::string>& problems() const noexcept;
    /// What logic said with the dialogue object, and whether a message is showing.
    [[nodiscard]] const DialogueObject& dialogue() const noexcept;
    /// The Diorama style's ambient and sun light, as logic last set them.
    [[nodiscard]] const LightingObject& lighting() const noexcept;
    /// What the player carries, as logic gives and takes it.
    [[nodiscard]] const InventoryObject& inventory() const noexcept;
    [[nodiscard]] const QuestObject& quests() const noexcept;
    [[nodiscard]] const PartyObject& party() const noexcept;
    /// The title screen or pause menu, open or not.
    [[nodiscard]] const GameMenu& menu() const noexcept;
    [[nodiscard]] const DebugMenu& debug_menu() const noexcept;
    /// Reads again what changed in assets/ while playing: pictures, sprite sheets, data
    /// tables and translations. Done once a second when testing from the editor. Returns
    /// how many files changed.
    std::size_t reload_changed();
    /// The battle being fought, if any.
    [[nodiscard]] const BattleScreen* battle_screen() const noexcept;
    [[nodiscard]] DialogueObject& dialogue() noexcept;
    /// The audio engine; null with AudioMode::Off or when no sound output was found.
    [[nodiscard]] audio::AudioEngine* audio() noexcept;

    struct Impl;

private:
    explicit Runtime(std::unique_ptr<Impl> impl);
    /// The player's options: read at start, kept after each change, put into effect.
    void load_options();
    /// The player walked `walked` tiles to `at`: a footstep when due, on the ground's sound.
    void footsteps(double walked, std::array<double, 2> at);
    /// A value Screen Text and Screen Bars show, by its name in braces: "gold", "hp:hero".
    [[nodiscard]] std::string hud_value(std::string_view name) const;
    void draw_hud();
    /// Keeps the player's achievements in the save folder.
    void save_achievements();
    /// Makes a copy of a prefab for logic (SaveObject::create()); returns its name, or empty
    /// with `error`.
    std::string create_copy(const SaveObject::Spawn& spawn, std::string& error);
    void save_options();
    void apply_options();
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::game
