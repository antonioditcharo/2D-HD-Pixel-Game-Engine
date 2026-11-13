#pragma once

// The Story editor (plan/02 G1, G3; plan/06 §3.6): one conversation as a graph of nodes, as
// dialogue tools for games show it. Each box is a line someone says; arrows lead to what
// comes next, one for each choice, labelled with its words; the start is outlined in
// green. Boxes drag anywhere. The inspector beside it edits the chosen node: who says it,
// the words, what comes next, the choices, and what it needs and does.
//
// See docs/dev/story.md.

#include <QMainWindow>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "editor/core/story.h"

class QAction;
class QComboBox;
class QString;
class QWidget;
class QFormLayout;
class QGraphicsScene;
class QGraphicsView;
class QListWidget;
class QScrollArea;

namespace editor {

class StoryWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit StoryWindow(QWidget* parent = nullptr);
    ~StoryWindow() override;
    StoryWindow(const StoryWindow&) = delete;
    StoryWindow& operator=(const StoryWindow&) = delete;
    StoryWindow(StoryWindow&&) = delete;
    StoryWindow& operator=(StoryWindow&&) = delete;

    /// Edits `conversation`, to be saved as `file`.
    void edit(engine::game::Conversation conversation, const std::filesystem::path& file);
    /// Edits a ".dtalk" file. False, with `error`, if it cannot be read.
    bool open(const std::filesystem::path& file, std::string& error);

    [[nodiscard]] StoryEditor* editor() const noexcept { return editor_.get(); }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }
    [[nodiscard]] const std::string& selected() const noexcept { return selected_; }
    /// Chooses a node: outlined in the graph, listed, and in the inspector.
    void select(const std::string& id);
    [[nodiscard]] QGraphicsScene* graph() const noexcept { return scene_; }
    [[nodiscard]] QListWidget* node_list() const noexcept { return list_; }
    [[nodiscard]] QWidget* inspector() const noexcept { return inspector_; }

    // The commands, for menus, buttons and tests.
    bool save();
    void undo();
    void redo();
    /// A new node below the chosen one, which leads to it; selected.
    std::string new_node();
    bool delete_node();
    /// What is wrong with the conversation, in plain language; shown in the status bar.
    std::vector<std::string> check();
    void set_quiet(bool quiet) noexcept { quiet_ = quiet; }
    /// Shows everything again.
    void refresh();

Q_SIGNALS:
    void saved(const std::filesystem::path& file);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build_menus();
    void refresh_graph();
    void refresh_list();
    void refresh_inspector();
    void refresh_title();
    /// A box picking a file of the project's assets with one of `extensions` ("png"), by
    /// its path in assets/, showing `now`; empty shows `none`.
    QComboBox* asset_box(QWidget* parent, const std::vector<std::string>& extensions,
                         const std::string& now, const QString& none) const;
    /// Changes how the speaker `name` looks and sounds with `change`.
    void change_speaker(const std::string& name,
                        const std::function<void(engine::game::Speaker&)>& change);
    /// Changes the chosen node with `change`; `structure` rebuilds the inspector after.
    void change_node(const std::function<void(engine::game::TalkNode&)>& change,
                     bool structure = false);
    void report(const QString& message);

    std::unique_ptr<StoryEditor> editor_;
    std::filesystem::path file_;
    std::string selected_;
    bool quiet_ = false;
    bool refreshing_ = false;
    QGraphicsScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    QListWidget* list_ = nullptr;
    QScrollArea* inspector_scroll_ = nullptr;
    QWidget* inspector_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
};

} // namespace editor
