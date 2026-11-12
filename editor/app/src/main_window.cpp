#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QScrollArea>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "editor/core/new_project.h"
#include "editor/exporter/exporter.h"
#include "inspector.h"
#include "logic_panel.h"
#include "project_panel.h"
#include "scene_view.h"
#include "tile_palette.h"

namespace editor {

namespace fs = std::filesystem;

namespace {

QString text_of(const std::string& s) {
    return QString::fromStdString(s);
}

QString path_text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), // NOLINT
                             static_cast<qsizetype>(text.size()));
}

fs::path path_of(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

QString issues_text(const std::vector<engine::serialize::Issue>& issues) {
    QStringList lines;
    for (const engine::serialize::Issue& issue : issues) {
        lines << (issue.path.empty()
                      ? text_of(issue.message)
                      : text_of(issue.path) + QStringLiteral(": ") + text_of(issue.message));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

MainWindow::MainWindow(fs::path player, QWidget* parent)
    : QMainWindow(parent), player_(std::move(player)) {
    resize(1280, 800);
    view_ = new SceneView(this);
    setCentralWidget(view_);

    // Left: the project's assets, and the scene's entities.
    outliner_ = new QTreeWidget(this);
    outliner_->setObjectName(QStringLiteral("outliner"));
    outliner_->setHeaderHidden(true);
    outliner_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    outliner_->setAccessibleName(tr("Entities"));
    auto* entities = new QDockWidget(tr("Entities"), this);
    entities->setObjectName(QStringLiteral("entities_dock"));
    entities->setWidget(outliner_);
    addDockWidget(Qt::LeftDockWidgetArea, entities);

    asset_model_ = new QFileSystemModel(this);
    asset_tree_ = new QTreeView(this);
    asset_tree_->setObjectName(QStringLiteral("assets"));
    asset_tree_->setAccessibleName(tr("Assets"));
    asset_tree_->setModel(asset_model_);
    asset_model_->setNameFilters({QStringLiteral("*.png"), QStringLiteral("*.wav"),
                                  QStringLiteral("*.ogg"), QStringLiteral("*.mp3"),
                                  QStringLiteral("*.flac"), QStringLiteral("*.dscene"),
                                  QStringLiteral("*.drules")});
    asset_model_->setNameFilterDisables(false);
    // Pictures are dragged from here into the scene.
    asset_tree_->setDragEnabled(true);
    asset_tree_->setDragDropMode(QAbstractItemView::DragOnly);
    for (int column = 1; column < 4; ++column) {
        asset_tree_->hideColumn(column);
    }
    asset_tree_->header()->hide();
    auto* assets = new QDockWidget(tr("Assets"), this);
    assets->setObjectName(QStringLiteral("assets_dock"));
    assets->setWidget(asset_tree_);
    addDockWidget(Qt::LeftDockWidgetArea, assets);

    // Right: the inspector, with the advanced fields a tick away.
    inspector_ = new Inspector(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(inspector_);
    auto* advanced = new QCheckBox(tr("Show advanced settings"), this);
    advanced->setObjectName(QStringLiteral("show_advanced"));
    auto* side = new QWidget(this);
    auto* side_layout = new QVBoxLayout(side);
    side_layout->setContentsMargins(0, 0, 0, 0);
    side_layout->addWidget(advanced);
    side_layout->addWidget(scroll);
    auto* inspector_dock = new QDockWidget(tr("Inspector"), this);
    inspector_dock->setObjectName(QStringLiteral("inspector_dock"));
    inspector_dock->setWidget(side);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock);

    // Right, beside the inspector: the project's logic, as plain language.
    logic_ = new LogicPanel(this);
    auto* logic_dock = new QDockWidget(tr("Logic"), this);
    logic_dock->setObjectName(QStringLiteral("logic_dock"));
    logic_dock->setWidget(logic_);
    addDockWidget(Qt::RightDockWidgetArea, logic_dock);
    tabifyDockWidget(inspector_dock, logic_dock);
    // And the project's settings.
    project_ = new ProjectPanel(this);
    auto* project_scroll = new QScrollArea(this);
    project_scroll->setWidgetResizable(true);
    project_scroll->setWidget(project_);
    auto* project_dock = new QDockWidget(tr("Project"), this);
    project_dock->setObjectName(QStringLiteral("project_dock"));
    project_dock->setWidget(project_scroll);
    addDockWidget(Qt::RightDockWidgetArea, project_dock);
    tabifyDockWidget(logic_dock, project_dock);
    inspector_dock->raise();
    resizeDocks({entities, inspector_dock}, {240, 340}, Qt::Horizontal);

    // Left, below: the tiles of the selected tilemap, to paint with.
    palette_ = new TilePalette(this);
    auto* tiles = new QDockWidget(tr("Tiles"), this);
    tiles->setObjectName(QStringLiteral("tiles_dock"));
    tiles->setWidget(palette_);
    addDockWidget(Qt::LeftDockWidgetArea, tiles);
    connect(palette_, &TilePalette::chosen_changed, this,
            [this](std::optional<std::int32_t> tile) { view_->set_painting(selection_, tile); });
    connect(palette_, &TilePalette::tool_changed, this,
            [this](int tool) { view_->set_paint_tool(static_cast<SceneView::PaintTool>(tool)); });
    connect(view_, &SceneView::stop_painting, this, [this] { palette_->choose(std::nullopt); });

    build_actions();
    connect(advanced, &QCheckBox::toggled, inspector_, &Inspector::set_show_advanced);
    connect(outliner_, &QTreeWidget::itemSelectionChanged, this, [this] {
        // In the list's order, the current item (the one clicked last) first.
        std::vector<engine::Guid> chosen;
        const auto add = [&chosen](const QTreeWidgetItem* item) {
            const std::optional<engine::Guid> guid =
                engine::Guid::parse(item->data(0, Qt::UserRole).toString().toStdString());
            if (guid && !contains(chosen, *guid)) {
                chosen.push_back(*guid);
            }
        };
        if (outliner_->currentItem() != nullptr && outliner_->currentItem()->isSelected()) {
            add(outliner_->currentItem());
        }
        for (const QTreeWidgetItem* item : outliner_->selectedItems()) {
            add(item);
        }
        if (chosen != selected_) {
            select_many(std::move(chosen));
        }
    });
    connect(view_, &SceneView::selected, this, [this](engine::Guid guid) { select(guid); });
    connect(view_, &SceneView::toggled, this, [this](engine::Guid guid) { toggle_selected(guid); });
    connect(view_, &SceneView::boxed, this,
            [this](std::vector<engine::Guid> guids) { select_many(std::move(guids)); });
    connect(view_, &SceneView::moved, this, [this] { changed(false); });
    connect(view_, &SceneView::problem, this, &MainWindow::report);
    connect(logic_, &LogicPanel::edited, this, [this] { update_title_and_actions(); });
    connect(logic_, &LogicPanel::refused, this, &MainWindow::report);
    connect(project_, &ProjectPanel::edited, this, [this] { changed(false); });
    connect(project_, &ProjectPanel::refused, this, &MainWindow::report);
    connect(view_, &SceneView::placed, this, [this](engine::Guid guid) {
        changed(true);
        select(guid);
    });
    connect(inspector_, &Inspector::edited, this, [this](bool structure) { changed(structure); });
    connect(inspector_, &Inspector::refused, this, &MainWindow::report);
    update_title_and_actions();
}

MainWindow::~MainWindow() = default;

void MainWindow::build_actions() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QMenu* game = menuBar()->addMenu(tr("&Game"));
    QToolBar* tools = addToolBar(tr("Commands"));
    tools->setObjectName(QStringLiteral("commands"));

    QAction* fresh = file->addAction(tr("&New project…"), this, [this] {
        bool named = false;
        const QString name = QInputDialog::getText(this, tr("New project"), tr("The game's name:"),
                                                   QLineEdit::Normal, tr("My game"), &named);
        if (!named) {
            return;
        }
        const QStringList kinds{tr("Classic 2D: seen from above, flat"),
                                tr("Diorama: HD-2D, in 3D with light and shadows")};
        const QString kind = QInputDialog::getItem(this, tr("New project"), tr("Start from:"),
                                                   kinds, 0, false, &named);
        if (!named) {
            return;
        }
        const QString folder =
            QFileDialog::getExistingDirectory(this, tr("Choose an empty folder for %1").arg(name));
        if (!folder.isEmpty()) {
            new_project(path_of(folder), name, kind == kinds.at(1));
        }
    });
    fresh->setObjectName(QStringLiteral("new_project"));
    fresh->setShortcut(QKeySequence::New);
    QAction* scene = file->addAction(tr("New &scene…"), this, [this] {
        bool named = false;
        const QString name = QInputDialog::getText(this, tr("New scene"), tr("The scene's name:"),
                                                   QLineEdit::Normal, tr("cave"), &named);
        if (named) {
            new_scene(name);
        }
    });
    scene->setObjectName(QStringLiteral("new_scene"));
    QAction* start =
        file->addAction(tr("Start the game in this scene"), this, [this] { start_here(); });
    start->setObjectName(QStringLiteral("start_here"));
    file->addSeparator();
    // A scene in the assets, double-clicked, is opened for editing.
    connect(asset_tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (document_ == nullptr) {
            return;
        }
        const QString file_path = asset_model_->filePath(index);
        if (!file_path.endsWith(QStringLiteral(".dscene"))) {
            return;
        }
        // Its path in assets/, through the real paths of both: the same folder can be
        // spelled two ways (a short and a long name on Windows, a link).
        std::error_code ignored;
        const fs::path assets = fs::weakly_canonical(document_->folder() / "assets", ignored);
        const fs::path chosen = fs::weakly_canonical(path_of(file_path), ignored);
        open_scene(text_of(chosen.lexically_proximate(assets).generic_string()));
    });
    QAction* open = file->addAction(tr("&Open project…"), this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Open a project"));
        if (!folder.isEmpty()) {
            open_project(path_of(folder));
        }
    });
    open->setShortcut(QKeySequence::Open);
    save_ = file->addAction(tr("&Save"), this, [this] { save(); });
    save_->setShortcut(QKeySequence::Save);
    undo_ = edit->addAction(tr("&Undo"), this, [this] { undo(); });
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = edit->addAction(tr("&Redo"), this, [this] { redo(); });
    redo_->setShortcut(QKeySequence::Redo);
    edit->addSeparator();
    new_entity_ = edit->addAction(tr("&New entity"), this, [this] { new_entity(); });
    delete_ = edit->addAction(tr("&Delete"), this, [this] { delete_selection(); });
    delete_->setShortcut(QKeySequence::Delete);
    play_ = game->addAction(tr("&Play"), this, [this] { play(); });
    play_->setShortcut(Qt::Key_F5);
    export_ = game->addAction(tr("&Export…"), this, [this] { export_to({}); });
    for (QAction* action : {open, save_, undo_, redo_, new_entity_, delete_, play_, export_}) {
        tools->addAction(action);
    }
}

bool MainWindow::new_project(const fs::path& folder, const QString& name, bool diorama) {
    std::string error;
    const QByteArray bytes = name.trimmed().toUtf8();
    if (!create_project(folder,
                        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                        error, diorama ? ProjectTemplate::Diorama : ProjectTemplate::TopDown2D)) {
        report(tr("The project could not be made:\n%1").arg(text_of(error)));
        return false;
    }
    return open_project(folder);
}

bool MainWindow::open_scene(const QString& path) {
    if (document_ == nullptr || (document_->dirty() && !save())) {
        return false;
    }
    std::string error;
    if (!document_->open_scene(path.toStdString(), error)) {
        report(tr("The scene could not be opened: %1").arg(text_of(error)));
        return false;
    }
    show_scene();
    statusBar()->showMessage(tr("Editing %1").arg(text_of(document_->scene_path())), 5000);
    return true;
}

bool MainWindow::new_scene(const QString& name) {
    if (document_ == nullptr || (document_->dirty() && !save())) {
        return false;
    }
    std::string error;
    const std::string path = "maps/" + name.trimmed().toStdString() + ".dscene";
    if (!document_->create_scene(path, error)) {
        report(tr("The scene could not be made: %1").arg(text_of(error)));
        return false;
    }
    show_scene();
    statusBar()->showMessage(tr("Editing %1").arg(text_of(document_->scene_path())), 5000);
    return true;
}

bool MainWindow::start_here() {
    std::string error;
    if (document_ == nullptr || !document_->set_start_scene(document_->scene_path(), error)) {
        report(tr("The start scene could not be changed: %1").arg(text_of(error)));
        return false;
    }
    project_->refresh();
    update_title_and_actions();
    statusBar()->showMessage(
        tr("The game starts in %1").arg(text_of(document_->settings().start_scene)), 5000);
    return true;
}

void MainWindow::reselect() {
    // What is still there stays selected.
    std::vector<engine::Guid> still;
    for (const engine::Guid guid : selected_) {
        if (document_->world().find(guid).is_valid()) {
            still.push_back(guid);
        }
    }
    select_many(std::move(still));
}

void MainWindow::show_scene() {
    selection_ = engine::Guid{};
    selected_.clear();
    view_->set_document(document_.get());
    inspector_->show_entity(document_.get(), selection_);
    palette_->show_tilemap(document_.get(), selection_);
    rebuild_outliner();
    logic_->rebuild();
    project_->refresh();
    update_title_and_actions();
}

bool MainWindow::open_project(const fs::path& folder) {
    std::vector<engine::serialize::Issue> issues;
    std::unique_ptr<Document> opened = Document::open(folder, issues);
    if (opened == nullptr) {
        report(tr("The project could not be opened:\n%1").arg(issues_text(issues)));
        return false;
    }
    document_ = std::move(opened);
    selection_ = engine::Guid{};
    selected_.clear();
    view_->set_document(document_.get());
    inspector_->show_entity(document_.get(), selection_);
    const QString assets = path_text(folder / "assets");
    asset_tree_->setRootIndex(asset_model_->setRootPath(assets));
    rebuild_outliner();
    logic_->set_document(document_.get());
    project_->set_document(document_.get());
    update_title_and_actions();
    if (!issues.empty()) {
        report(tr("Opened, with notes:\n%1").arg(issues_text(issues)));
    } else {
        statusBar()->showMessage(tr("Opened %1").arg(text_of(document_->settings().name)), 5000);
    }
    return true;
}

void MainWindow::select(engine::Guid entity) {
    select_many(entity.is_valid() ? std::vector<engine::Guid>{entity}
                                  : std::vector<engine::Guid>{});
}

void MainWindow::toggle_selected(engine::Guid entity) {
    std::vector<engine::Guid> now = selected_;
    if (contains(now, entity)) {
        std::erase_if(now, [entity](engine::Guid g) { return g == entity; });
    } else {
        now.push_back(entity);
    }
    select_many(std::move(now));
}

void MainWindow::select_many(std::vector<engine::Guid> entities) {
    selected_ = std::move(entities);
    const engine::Guid entity = selected_.empty() ? engine::Guid{} : selected_.front();
    selection_ = entity;
    view_->set_group(selected_);
    inspector_->show_entities(document_.get(), selected_);
    palette_->show_tilemap(document_.get(), entity); // also stops painting
    // The list follows, without echoing back.
    const QSignalBlocker quiet(outliner_);
    outliner_->clearSelection();
    for (QTreeWidgetItemIterator it(outliner_); *it != nullptr; ++it) {
        const std::optional<engine::Guid> guid =
            engine::Guid::parse((*it)->data(0, Qt::UserRole).toString().toStdString());
        if (guid && contains(selected_, *guid)) {
            (*it)->setSelected(true);
            if (*guid == entity) {
                outliner_->scrollToItem(*it);
            }
        }
    }
    update_title_and_actions();
}

void MainWindow::rebuild_outliner() {
    const QSignalBlocker quiet(outliner_);
    outliner_->clear();
    if (document_ == nullptr) {
        return;
    }
    const engine::world::World& world = document_->world();
    std::unordered_map<std::uint64_t, QTreeWidgetItem*> items;
    for (const engine::world::Entity e : document_->entities()) {
        const std::string_view name = world.name(e);
        auto* item = new QTreeWidgetItem(
            QStringList{name.empty() ? tr("(unnamed)") : text_of(std::string(name))});
        item->setData(0, Qt::UserRole, text_of(world.guid(e).to_string()));
        const engine::world::Entity parent = world.parent(e);
        const auto found = items.find(parent.id);
        if (parent.is_valid() && found != items.end()) {
            found->second->addChild(item);
        } else {
            outliner_->addTopLevelItem(item);
        }
        items[e.id] = item;
        if (world.guid(e) == selection_) {
            item->setSelected(true);
        }
    }
    outliner_->expandAll();
}

QTreeWidget* MainWindow::logic_view() const noexcept {
    return logic_->tree();
}

void MainWindow::changed(bool structure) {
    if (structure) {
        rebuild_outliner();
        logic_->rebuild(); // names and components decide what the logic can do
    }
    view_->refresh();
    update_title_and_actions();
}

void MainWindow::update_title_and_actions() {
    const bool open = document_ != nullptr;
    QString title = tr("Sparxel Editor");
    if (open) {
        // The game, the scene being edited, a star for unsaved changes.
        title = text_of(document_->settings().name) + QStringLiteral(": ") +
                text_of(document_->scene_path()) +
                (document_->dirty() ? QStringLiteral(" *") : QString()) + QStringLiteral(" — ") +
                title;
    }
    setWindowTitle(title);
    save_->setEnabled(open);
    undo_->setEnabled(open && document_->can_undo());
    redo_->setEnabled(open && document_->can_redo());
    undo_->setText(open && document_->can_undo()
                       ? tr("&Undo %1").arg(text_of(document_->undo_label()))
                       : tr("&Undo"));
    redo_->setText(open && document_->can_redo()
                       ? tr("&Redo %1").arg(text_of(document_->redo_label()))
                       : tr("&Redo"));
    new_entity_->setEnabled(open);
    delete_->setEnabled(open && document_->world().find(selection_).is_valid());
    play_->setEnabled(open);
    export_->setEnabled(open);
}

void MainWindow::report(const QString& message) {
    statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0), 8000);
    if (!quiet_ && !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        QMessageBox::warning(this, tr("Sparxel Editor"), message);
    }
}

bool MainWindow::save() {
    if (document_ == nullptr) {
        return false;
    }
    std::string error;
    if (!document_->save(error)) {
        report(tr("The scene could not be saved: %1").arg(text_of(error)));
        return false;
    }
    statusBar()->showMessage(tr("Saved"), 3000);
    update_title_and_actions();
    return true;
}

void MainWindow::undo() {
    if (document_ != nullptr && document_->undo()) {
        rebuild_outliner();
        reselect();
        view_->refresh();
        logic_->rebuild();
        project_->refresh();
    }
}

void MainWindow::redo() {
    if (document_ != nullptr && document_->redo()) {
        rebuild_outliner();
        reselect();
        view_->refresh();
        logic_->rebuild();
        project_->refresh();
    }
}

engine::Guid MainWindow::new_entity() {
    if (document_ == nullptr) {
        return {};
    }
    // Where the camera looks, on whole tiles.
    const engine::Guid guid = document_->create_entity("entity", std::round(view_->camera_x()),
                                                       std::round(view_->camera_y()));
    rebuild_outliner();
    select(guid);
    view_->refresh();
    return guid;
}

void MainWindow::delete_selection() {
    if (document_ != nullptr && document_->delete_entities(selected_)) {
        rebuild_outliner();
        select(engine::Guid{});
        view_->refresh();
    }
}

bool MainWindow::play() {
    if (document_ == nullptr || (document_->dirty() && !save())) {
        return false;
    }
    if (!QProcess::startDetached(path_text(player_), {path_text(document_->folder())})) {
        report(tr("The player %1 could not be started.").arg(path_text(player_)));
        return false;
    }
    statusBar()->showMessage(tr("Playing…"), 3000);
    return true;
}

bool MainWindow::export_to(const fs::path& folder) {
    if (document_ == nullptr || (document_->dirty() && !save())) {
        return false;
    }
    fs::path output = folder;
    if (output.empty()) {
        const QString chosen =
            QFileDialog::getExistingDirectory(this, tr("Export the game to an empty folder"));
        if (chosen.isEmpty()) {
            return false;
        }
        output = path_of(chosen);
    }
    std::vector<engine::serialize::Issue> issues;
    const std::optional<ExportReport> exported =
        export_game(ExportOptions{document_->folder(), player_, output}, issues);
    if (!exported) {
        report(tr("The game was not exported:\n%1").arg(issues_text(issues)));
        return false;
    }
    statusBar()->showMessage(tr("Exported: play it with %1").arg(path_text(exported->executable)),
                             8000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (document_ != nullptr && document_->dirty() &&
        !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const auto answer = QMessageBox::question(
            this, tr("Sparxel Editor"), tr("Save the changes to the scene first?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

} // namespace editor
