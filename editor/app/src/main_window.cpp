#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QKeySequence>
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

#include "editor/core/logic_text.h"
#include "editor/exporter/exporter.h"
#include "inspector.h"
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
                                  QStringLiteral("*.mp3"), QStringLiteral("*.flac"),
                                  QStringLiteral("*.dscene"), QStringLiteral("*.drules")});
    asset_model_->setNameFilterDisables(false);
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
    logic_ = new QTreeWidget(this);
    logic_->setObjectName(QStringLiteral("logic"));
    logic_->setHeaderHidden(true);
    logic_->setAccessibleName(tr("Logic"));
    logic_->setWordWrap(true);
    auto* logic_dock = new QDockWidget(tr("Logic"), this);
    logic_dock->setObjectName(QStringLiteral("logic_dock"));
    logic_dock->setWidget(logic_);
    addDockWidget(Qt::RightDockWidgetArea, logic_dock);
    tabifyDockWidget(inspector_dock, logic_dock);
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
    connect(view_, &SceneView::stop_painting, this, [this] { palette_->choose(std::nullopt); });

    build_actions();
    connect(advanced, &QCheckBox::toggled, inspector_, &Inspector::set_show_advanced);
    connect(outliner_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const QList<QTreeWidgetItem*> items = outliner_->selectedItems();
        const std::optional<engine::Guid> guid =
            items.isEmpty() ? std::nullopt
                            : engine::Guid::parse(
                                  items.front()->data(0, Qt::UserRole).toString().toStdString());
        const engine::Guid chosen = guid.value_or(engine::Guid{});
        if (chosen != selection_) {
            select(chosen);
        }
    });
    connect(view_, &SceneView::selected, this, [this](engine::Guid guid) { select(guid); });
    connect(view_, &SceneView::moved, this, [this] { changed(false); });
    connect(view_, &SceneView::problem, this, &MainWindow::report);
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

bool MainWindow::open_project(const fs::path& folder) {
    std::vector<engine::serialize::Issue> issues;
    std::unique_ptr<Document> opened = Document::open(folder, issues);
    if (opened == nullptr) {
        report(tr("The project could not be opened:\n%1").arg(issues_text(issues)));
        return false;
    }
    document_ = std::move(opened);
    selection_ = engine::Guid{};
    view_->set_document(document_.get());
    inspector_->show_entity(document_.get(), selection_);
    const QString assets = path_text(folder / "assets");
    asset_tree_->setRootIndex(asset_model_->setRootPath(assets));
    rebuild_outliner();
    rebuild_logic();
    update_title_and_actions();
    if (!issues.empty()) {
        report(tr("Opened, with notes:\n%1").arg(issues_text(issues)));
    } else {
        statusBar()->showMessage(tr("Opened %1").arg(text_of(document_->settings().name)), 5000);
    }
    return true;
}

void MainWindow::select(engine::Guid entity) {
    selection_ = entity;
    view_->set_selection(entity);
    inspector_->show_entity(document_.get(), entity);
    palette_->show_tilemap(document_.get(), entity); // also stops painting
    // The list follows, without echoing back.
    const QSignalBlocker quiet(outliner_);
    outliner_->clearSelection();
    const QString wanted = text_of(entity.to_string());
    for (QTreeWidgetItemIterator it(outliner_); *it != nullptr; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == wanted) {
            (*it)->setSelected(true);
            outliner_->scrollToItem(*it);
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

void MainWindow::rebuild_logic() {
    logic_->clear();
    if (document_ == nullptr) {
        return;
    }
    std::vector<engine::serialize::Issue> issues;
    for (const LogicFile& file : logic_files(*document_, issues)) {
        auto* top = new QTreeWidgetItem(QStringList{text_of(file.path)});
        logic_->addTopLevelItem(top);
        // Each line nests under the last line one level up.
        std::vector<QTreeWidgetItem*> parents{top};
        for (const LogicLine& line : describe(file.asset, *document_)) {
            const auto depth = static_cast<std::size_t>(line.depth);
            parents.resize(std::min(parents.size(), depth + 1));
            auto* item = new QTreeWidgetItem(QStringList{text_of(line.text)});
            item->setData(0, Qt::UserRole,
                          QVariant::fromValue(static_cast<qulonglong>(line.block.value)));
            parents.back()->addChild(item);
            parents.push_back(item);
        }
    }
    for (const engine::serialize::Issue& issue : issues) {
        logic_->addTopLevelItem(new QTreeWidgetItem(
            QStringList{text_of(issue.path) + QStringLiteral(": ") + text_of(issue.message)}));
    }
    logic_->expandAll();
}

void MainWindow::changed(bool structure) {
    if (structure) {
        rebuild_outliner();
    }
    view_->refresh();
    update_title_and_actions();
}

void MainWindow::update_title_and_actions() {
    const bool open = document_ != nullptr;
    QString title = tr("Sparxel Editor");
    if (open) {
        title = text_of(document_->settings().name) +
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
    if (!QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
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
        select(document_->world().find(selection_).is_valid() ? selection_ : engine::Guid{});
        view_->refresh();
    }
}

void MainWindow::redo() {
    if (document_ != nullptr && document_->redo()) {
        rebuild_outliner();
        select(document_->world().find(selection_).is_valid() ? selection_ : engine::Guid{});
        view_->refresh();
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
    if (document_ != nullptr && document_->delete_entity(selection_)) {
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
