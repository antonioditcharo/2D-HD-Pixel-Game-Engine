#pragma once

// The editor's main window (plan/06 §2): the scene in the middle, the project's assets and
// the scene's entities on the left, the inspector on the right, and the commands that
// matter most a click away: save, undo and redo, add and delete, Play and Export.
//
// See docs/dev/editor.md.

#include <QMainWindow>
#include <filesystem>
#include <memory>

#include "editor/core/document.h"

class QAction;
class QTreeWidget;
class QTreeView;
class QFileSystemModel;

namespace editor {

class LogicPanel;
class ProjectPanel;

class Inspector;
class SceneView;
class TilePalette;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// `player` is the sparxel-player executable that Play runs and Export ships.
    explicit MainWindow(std::filesystem::path player, QWidget* parent = nullptr);
    ~MainWindow() override;
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    /// Opens a project folder. Returns false, showing why, if it cannot be opened.
    bool open_project(const std::filesystem::path& folder);
    /// Makes a new project named `name` in `folder` (new or empty), a small starter game,
    /// and opens it. Returns false, with the reason shown, if it cannot.
    bool new_project(const std::filesystem::path& folder, const QString& name,
                     bool diorama = false);
    /// Edits another scene of the project (its path in assets/), saving the open one
    /// first if it changed. Returns false, showing why, if it cannot.
    bool open_scene(const QString& path);
    /// Makes an empty scene named `name` in assets/maps and opens it, saving first.
    bool new_scene(const QString& name);
    /// Makes the open scene the one the game starts in.
    bool start_here();
    /// Selects an entity everywhere: the list, the scene and the inspector.
    void select(engine::Guid entity);

    [[nodiscard]] Document* document() const noexcept { return document_.get(); }
    [[nodiscard]] engine::Guid selection() const noexcept { return selection_; }
    [[nodiscard]] SceneView* scene_view() const noexcept { return view_; }
    [[nodiscard]] QTreeWidget* outliner() const noexcept { return outliner_; }
    [[nodiscard]] Inspector* inspector() const noexcept { return inspector_; }
    [[nodiscard]] TilePalette* tile_palette() const noexcept { return palette_; }
    [[nodiscard]] QTreeWidget* logic_view() const noexcept;
    [[nodiscard]] LogicPanel* logic_panel() const noexcept { return logic_; }
    [[nodiscard]] ProjectPanel* project_panel() const noexcept { return project_; }

    // The commands, for menus, tool buttons and tests.
    bool save();
    void undo();
    void redo();
    engine::Guid new_entity();
    void delete_selection();
    /// Saves, then starts the player on the project. Returns false, showing why, if it
    /// could not start.
    bool play();
    /// Exports the game to `folder` (asked for when empty). Returns false, showing why.
    bool export_to(const std::filesystem::path& folder);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    /// Shows the document's scene everywhere, after another was opened.
    void show_scene();

private:
    void build_actions();
    /// After an edit: `structure` rebuilds the lists, not only the scene.
    void changed(bool structure);
    void rebuild_outliner();
    void update_title_and_actions();
    void report(const QString& message);

    std::filesystem::path player_;
    std::unique_ptr<Document> document_;
    engine::Guid selection_;
    SceneView* view_ = nullptr;
    QTreeWidget* outliner_ = nullptr;
    QTreeView* asset_tree_ = nullptr;
    QFileSystemModel* asset_model_ = nullptr;
    Inspector* inspector_ = nullptr;
    TilePalette* palette_ = nullptr;
    LogicPanel* logic_ = nullptr;
    ProjectPanel* project_ = nullptr;
    QAction* save_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
    QAction* delete_ = nullptr;
    QAction* play_ = nullptr;
    QAction* export_ = nullptr;
    QAction* new_entity_ = nullptr;
};

} // namespace editor
