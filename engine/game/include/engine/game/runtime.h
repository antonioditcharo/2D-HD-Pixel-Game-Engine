#pragma once

// The game runtime: opens a project folder and runs it. It loads the settings, assets,
// start scene and logic, then runs fixed steps of input, movement and logic, drawn
// pixel-perfect to a window (plan/03 §7, plan/05 §5). This is what an exported game runs,
// and what the editor's Play button will run.
//
// See docs/dev/game.md.

#include "engine/assets/assets.h"
#include "engine/foundation/reflect.h"
#include "engine/game/behaviours.h"
#include "engine/game/clock.h"
#include "engine/game/save.h"
#include "engine/input/input.h"
#include "engine/logic/vm.h"
#include "engine/render/render.h"
#include "engine/scene2d/scene2d.h"
#include "engine/serialize/json.h"
#include "engine/world/world.h"

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

struct ProjectSettings {
    std::string name;
    std::uint32_t ppu = 16;    ///< Pixels per world unit (tile), 8 to 64 (plan/04 §2).
    std::uint32_t width = 320; ///< The internal resolution, upscaled pixel-perfect.
    std::uint32_t height = 180;
    std::string start_scene; ///< The scene asset to start in, a path in assets/.
    render::Color background{20, 24, 40, 255};
};

/// Reads `root`/project.sparxel.json. Returns nothing, with issues, if it is missing or
/// invalid. Unknown entries are reported and ignored.
[[nodiscard]] std::optional<ProjectSettings> read_project(const std::filesystem::path& root,
                                                          std::vector<serialize::Issue>& issues);
[[nodiscard]] serialize::Json project_to_json(const ProjectSettings& settings);

/// Registers every type the runtime uses: Classic 2D components, input, and behaviours.
void register_types(reflect::Registry& registry);

// ---------------------------------------------------------------------------------------
// The runtime
// ---------------------------------------------------------------------------------------

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
    std::string window_title = "Sparxel";
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
    /// Where save slots go (see RuntimeOptions::save_folder).
    [[nodiscard]] std::filesystem::path save_folder() const;
    /// Problems with saving or loading that logic asked for, oldest first.
    [[nodiscard]] const std::vector<std::string>& save_problems() const noexcept;

    struct Impl;

private:
    explicit Runtime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::game
