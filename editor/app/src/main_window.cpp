#include "main_window.h"

#include "engine/serialize/json.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "asset_browser.h"
#include "doctor_panel.h"
#include "editor/core/asset_tools.h"
#include "editor/core/new_project.h"
#include "editor/core/snapshots.h"
#include "editor/exporter/exporter.h"
#include "help_window.h"
#include "inspector.h"
#include "logic_panel.h"
#include "project_panel.h"
#include "scene_view.h"
#include "sfx_generator.h"
#include "sprite_studio.h"
#include "start_page.h"
#include "story_window.h"
#include "table_window.h"
#include "tile_palette.h"
#include "tutorials.h"

namespace editor {

namespace fs = std::filesystem;

namespace {

/// Which layout format save_layout() writes: one from another version is not restored.
constexpr int layout_version = 1;

QString text_of(const std::string& s) {
    return QString::fromStdString(s);
}

QString path_text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), // NOLINT
                             static_cast<qsizetype>(text.size()));
}

std::string utf8_of(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
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
    // With no project open, the start page; with one, the scene.
    start_ = new StartPage(this);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(start_);
    stack_->addWidget(view_);
    setCentralWidget(stack_);
    // Panels split beside and under each other anywhere, gather as tabs (with the tabs on
    // top), float as windows, and a group of tabs drags as one.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    // Left: the project's assets, and the scene's entities.
    outliner_ = new QTreeWidget(this);
    outliner_->setObjectName(QStringLiteral("outliner"));
    outliner_->setHeaderHidden(true);
    outliner_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    outliner_->setAccessibleName(tr("Entities"));
    // Right-click: put it under another (a Screen Layout lines up what is under it), or
    // back at the top.
    outliner_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(outliner_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const QTreeWidgetItem* item = outliner_->itemAt(at);
        const std::optional<engine::Guid> guid =
            item != nullptr ? engine::Guid::parse(utf8_of(item->data(0, Qt::UserRole).toString()))
                            : std::nullopt;
        if (document_ == nullptr || !guid) {
            return;
        }
        QMenu menu(this);
        QMenu* under = menu.addMenu(tr("Put under"));
        for (QTreeWidgetItemIterator it(outliner_); *it != nullptr; ++it) {
            const QString text = (*it)->data(0, Qt::UserRole).toString();
            const std::optional<engine::Guid> other = engine::Guid::parse(utf8_of(text));
            if (other && *other != *guid) {
                const engine::Guid holder = *other;
                const engine::Guid child = *guid;
                under->addAction((*it)->text(0), this,
                                 [this, child, holder] { put_under(child, holder); });
            }
        }
        menu.addAction(tr("Put at the top"), this,
                       [this, child = *guid] { put_under(child, engine::Guid{}); });
        menu.exec(outliner_->viewport()->mapToGlobal(at));
    });
    auto* entities = new QDockWidget(tr("Entities"), this);
    entities->setObjectName(QStringLiteral("entities_dock"));
    entities->setWidget(outliner_);
    addDockWidget(Qt::LeftDockWidgetArea, entities);

    asset_model_ = new QFileSystemModel(this);
    asset_tree_ = new QTreeView(this);
    asset_tree_->setObjectName(QStringLiteral("assets"));
    asset_tree_->setAccessibleName(tr("Assets"));
    asset_tree_->setModel(asset_model_);
    asset_model_->setNameFilters(asset_filters({}));
    asset_model_->setNameFilterDisables(false);
    // Pictures by thumbnails of themselves.
    asset_tree_->setItemDelegate(new AssetThumbnails(asset_tree_));
    asset_tree_->setIconSize(QSize(24, 24));
    // While searching, folders open as they are read, to show what matched in them.
    connect(asset_model_, &QFileSystemModel::directoryLoaded, this, [this](const QString& path) {
        if (asset_search_ != nullptr && !asset_search_->text().trimmed().isEmpty()) {
            asset_tree_->expand(asset_model_->index(path));
        }
    });
    // Pictures are dragged from here into the scene.
    asset_tree_->setDragEnabled(true);
    asset_tree_->setDragDropMode(QAbstractItemView::DragOnly);
    for (int column = 1; column < 4; ++column) {
        asset_tree_->hideColumn(column);
    }
    asset_tree_->header()->hide();
    // A search box over them: names with every word typed, in order.
    asset_search_ = new QLineEdit(this);
    asset_search_->setObjectName(QStringLiteral("asset_search"));
    asset_search_->setPlaceholderText(tr("Search assets"));
    asset_search_->setClearButtonEnabled(true);
    connect(asset_search_, &QLineEdit::textChanged, this,
            [this](const QString& text) { search_assets(text); });
    auto* assets_side = new QWidget(this);
    auto* assets_column = new QVBoxLayout(assets_side);
    assets_column->setContentsMargins(0, 0, 0, 0);
    assets_column->setSpacing(2);
    assets_column->addWidget(asset_search_);
    assets_column->addWidget(asset_tree_);
    auto* assets = new QDockWidget(tr("Assets"), this);
    assets->setObjectName(QStringLiteral("assets_dock"));
    assets->setWidget(assets_side);
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
    // Project Doctor, beside the project's settings.
    doctor_ = new DoctorPanel(this);
    auto* doctor_dock = new QDockWidget(tr("Doctor"), this);
    doctor_dock->setObjectName(QStringLiteral("doctor_dock"));
    doctor_dock->setWidget(doctor_);
    addDockWidget(Qt::RightDockWidgetArea, doctor_dock);
    tabifyDockWidget(project_dock, doctor_dock);
    connect(doctor_, &DoctorPanel::check_requested, this, [this] { check_project(); });
    connect(doctor_, &DoctorPanel::chosen, this, [this](const Finding& f) { go_to(f); });
    // Tutorials: steps ticked off as they are done in the editor (Help > Tutorials).
    tutorials_ = new TutorialPanel(this, this);
    auto* tutorials_dock = new QDockWidget(tr("Tutorials"), this);
    tutorials_dock->setObjectName(QStringLiteral("tutorials_dock"));
    tutorials_dock->setWidget(tutorials_);
    addDockWidget(Qt::RightDockWidgetArea, tutorials_dock);
    tabifyDockWidget(project_dock, tutorials_dock);
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
    connect(palette_, &TilePalette::variants_changed, this,
            [this](const std::vector<std::int32_t>& variants) {
                if (document_ != nullptr) {
                    document_->set_tile_variants(variants);
                }
            });
    connect(palette_, &TilePalette::edited, this, [this] { changed(false); });
    connect(palette_, &TilePalette::density_changed, this,
            [this](int density) { view_->set_scatter_density(density); });
    connect(view_, &SceneView::stop_painting, this, [this] { palette_->choose(std::nullopt); });

    build_actions();
    first_layout_ = saveState(layout_version);
    // Unsaved work is kept for recovery every minute.
    autosave_timer_ = new QTimer(this);
    autosave_timer_->setInterval(60 * 1000);
    connect(autosave_timer_, &QTimer::timeout, this, [this] { autosave(); });
    autosave_timer_->start();
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
    connect(view_, &SceneView::drag_finished, this, [this] { reselect(); });
    connect(view_, &SceneView::problem, this, &MainWindow::report);
    connect(logic_, &LogicPanel::edited, this, [this] { update_title_and_actions(); });
    connect(logic_, &LogicPanel::refused, this, &MainWindow::report);
    connect(project_, &ProjectPanel::edited, this, [this] { changed(false); });
    connect(project_, &ProjectPanel::refused, this, &MainWindow::report);
    connect(project_, &ProjectPanel::previewing, view_, &SceneView::preview_look);
    connect(view_, &SceneView::placed, this, [this](engine::Guid guid) {
        changed(true);
        select(guid);
    });
    connect(inspector_, &Inspector::edited, this, [this](bool structure) {
        changed(structure);
        // A tilemap's tileset, tile size or Auto-tile changed: its tiles to paint with too.
        if (document_ != nullptr) {
            const engine::world::Entity e = document_->world().find(selection_);
            const auto* map =
                e.is_valid() ? document_->world().get<engine::scene2d::Tilemap>(e) : nullptr;
            const QString key =
                map == nullptr
                    ? QString()
                    : text_of(map->tileset) +
                          QStringLiteral("/%1/%2").arg(map->tile_size).arg(map->autotile ? 1 : 0);
            if (key != palette_key_) {
                palette_key_ = key;
                palette_->show_tilemap(document_.get(), selection_);
            }
        }
    });
    connect(inspector_, &Inspector::refused, this, &MainWindow::report);
    connect(start_, &StartPage::open_folder, this,
            [this](const QString& folder) { open_project(path_of(folder)); });
    connect(start_, &StartPage::forget, this,
            [this](const QString& folder) { forget_recent(folder); });
    start_->set_recent(recent_);
    update_title_and_actions();
}

MainWindow::~MainWindow() = default;

std::vector<SearchItem> MainWindow::search_items() {
    std::vector<SearchItem> items;
    // The menus' commands, by the path to them ("Game ▸ Play"), without their & marks.
    const std::function<void(QMenu*, const QString&)> commands = [&](QMenu* menu,
                                                                     const QString& path) {
        for (QAction* action : menu->actions()) {
            if (action->isSeparator() || !action->isEnabled() || action->objectName() == "search") {
                continue;
            }
            QString name = action->text();
            name.remove(QLatin1Char('&'));
            name.remove(QStringLiteral("…"));
            if (QMenu* sub = action->menu()) {
                commands(sub, path + name + QStringLiteral(" ▸ "));
                continue;
            }
            const QString shortcut = action->shortcut().toString(QKeySequence::NativeText);
            items.push_back(SearchItem{
                path + name, shortcut.isEmpty() ? tr("Command") : tr("Command, %1").arg(shortcut),
                [action] { action->trigger(); }});
        }
    };
    for (QAction* top : menuBar()->actions()) {
        if (QMenu* menu = top->menu()) {
            QString name = top->text();
            name.remove(QLatin1Char('&'));
            commands(menu, name + QStringLiteral(" ▸ "));
        }
    }
    // The guide's pages.
    for (const HelpPage& page : help_pages()) {
        const QString file = page.file;
        items.push_back(SearchItem{tr("Guide ▸ %1").arg(page.title), tr("Guide"),
                                   [this, file] { show_guide(file); }});
    }
    if (document_ == nullptr) {
        return items;
    }
    // The project's assets: scenes open, pictures and sprites go to Sprite Studio, logic
    // files show in the Logic view.
    for (const engine::assets::AssetRecord& record : document_->assets().assets()) {
        const QString path = text_of(record.path);
        if (path.endsWith(QStringLiteral(".dscene"))) {
            items.push_back({path, tr("Scene"), [this, path] { open_scene(path); }});
        } else if (path.endsWith(QStringLiteral(".png")) ||
                   path.endsWith(QStringLiteral(".dsprite"))) {
            items.push_back({path,
                             path.endsWith(QStringLiteral(".png")) ? tr("Picture") : tr("Sprite"),
                             [this, path] { open_sprite(path); }});
        } else if (path.endsWith(QStringLiteral(".dtable"))) {
            items.push_back({path, tr("Table"), [this, path] { open_table(path); }});
        } else if (path.endsWith(QStringLiteral(".dtalk"))) {
            items.push_back({path, tr("Conversation"), [this, path] { open_story(path); }});
        } else if (path.endsWith(QStringLiteral(".drules"))) {
            items.push_back({path, tr("Logic"), [this, path] {
                                 for (QDockWidget* dock : findChildren<QDockWidget*>()) {
                                     if (dock->widget() == logic_) {
                                         dock->show();
                                         dock->raise();
                                     }
                                 }
                                 const QList<QTreeWidgetItem*> found =
                                     logic_view()->findItems(path, Qt::MatchExactly);
                                 if (!found.isEmpty()) {
                                     logic_view()->setCurrentItem(found.front());
                                 }
                             }});
        }
    }
    // The scene's entities: choosing one selects it.
    const engine::world::World& world = document_->world();
    for (const engine::world::Entity e : document_->entities()) {
        const std::string_view name = world.name(e);
        if (!name.empty()) {
            const engine::Guid guid = world.guid(e);
            items.push_back(
                {text_of(std::string(name)), tr("Entity"), [this, guid] { select(guid); }});
        }
    }
    return items;
}

void MainWindow::show_search() {
    if (search_ == nullptr) {
        search_ = new SearchBox(this);
    }
    search_->field()->clear();
    search_->set_items(search_items());
    search_->show();
    search_->raise();
    search_->activateWindow();
    search_->field()->setFocus();
}

TableWindow* MainWindow::open_table(const QString& asset_path) {
    if (document_ == nullptr) {
        return nullptr;
    }
    const fs::path file = (document_->folder() / "assets" / path_of(asset_path)).lexically_normal();
    const std::string key = file.generic_string();
    if (const auto found = tables_.find(key); found != tables_.end()) {
        found->second->show();
        found->second->raise();
        found->second->activateWindow();
        return found->second;
    }
    auto* window = new TableWindow(this);
    window->setWindowFlag(Qt::Window);
    window->set_quiet(quiet_);
    std::string error;
    if (!window->open(file, error)) {
        delete window;
        report(tr("%1 could not be opened: %2").arg(asset_path, text_of(error)));
        return nullptr;
    }
    connect(window, &TableWindow::saved, this, [this] { reload_assets(); });
    tables_[key] = window;
    window->show();
    return window;
}

TableWindow* MainWindow::new_table(const QString& name) {
    if (document_ == nullptr) {
        return nullptr;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')) ||
        trimmed.contains(QLatin1Char('\\')) || trimmed.startsWith(QLatin1Char('.'))) {
        report(tr("A table needs a name without slashes, such as \"items\"."));
        return nullptr;
    }
    const QString relative = QStringLiteral("data/%1.dtable").arg(trimmed);
    const fs::path file = document_->folder() / "assets" / path_of(relative);
    std::error_code ignored;
    if (fs::exists(file, ignored)) {
        report(tr("There is a table called %1 already.").arg(trimmed));
        return nullptr;
    }
    fs::create_directories(file.parent_path(), ignored);
    std::string error;
    if (!TableEditor::save(TableEditor::blank(), file, error)) {
        report(tr("The table could not be made: %1").arg(text_of(error)));
        return nullptr;
    }
    reload_assets();
    return open_table(relative);
}

bool MainWindow::take_snapshot(const QString& name) {
    if (document_ == nullptr) {
        return false;
    }
    // Saved first, so the snapshot has the work as it is; with a picture of the scene.
    if (document_->dirty() && !save()) {
        return false;
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    view_->grab()
        .scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .save(&buffer, "PNG");
    std::string error;
    const Snapshot made =
        editor::take_snapshot(document_->folder(), utf8_of(name),
                              std::vector<std::uint8_t>(png.begin(), png.end()), error);
    if (made.id.empty()) {
        report(text_of(error));
        return false;
    }
    statusBar()->showMessage(
        tr("Snapshot taken: %1 (File \u25b8 Snapshots\u2026 to go back to it)").arg(name), 8000);
    return true;
}

bool MainWindow::restore_snapshot(const QString& id) {
    if (document_ == nullptr) {
        return false;
    }
    if (document_->dirty() && !save()) {
        return false;
    }
    const fs::path folder = document_->folder();
    std::string error;
    if (!editor::restore_snapshot(folder, utf8_of(id), error)) {
        report(text_of(error));
        return false;
    }
    // Opened again, as the snapshot had it.
    const bool opened = open_project(folder);
    statusBar()->showMessage(
        tr("Back to the snapshot \"%1\". The project as it was is a snapshot too.").arg(id), 10000);
    return opened;
}

void MainWindow::show_snapshots() {
    if (document_ == nullptr) {
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("snapshots_dialog"));
    dialog.setWindowTitle(tr("Snapshots"));
    dialog.resize(560, 420);
    auto* column = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("snapshot_list"));
    list->setIconSize(QSize(160, 90));
    for (const Snapshot& s : list_snapshots(document_->folder())) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(text_of(s.name), text_of(s.when)), list);
        item->setData(Qt::UserRole, text_of(s.id));
        if (!s.picture.empty()) {
            item->setIcon(QIcon(QPixmap(path_text(s.picture))));
        }
    }
    if (list->count() > 0) {
        list->setCurrentRow(0);
    }
    auto* buttons = new QHBoxLayout();
    auto* restore = new QPushButton(tr("Go back to it"), &dialog);
    restore->setToolTip(
        tr("The project as it was then; the project as it is now is kept as a snapshot first"));
    auto* remove = new QPushButton(tr("Delete"), &dialog);
    auto* close_button = new QPushButton(tr("Close"), &dialog);
    buttons->addWidget(restore);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(close_button);
    column->addWidget(new QLabel(tr("Restore points of the whole project, the newest first. "
                                    "Take one with File \u25b8 Take a snapshot\u2026"),
                                 &dialog));
    column->addWidget(list);
    column->addLayout(buttons);
    connect(close_button, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(restore, &QPushButton::clicked, &dialog, [this, list, &dialog] {
        if (const QListWidgetItem* item = list->currentItem()) {
            const QString id = item->data(Qt::UserRole).toString();
            dialog.accept();
            restore_snapshot(id);
        }
    });
    connect(remove, &QPushButton::clicked, &dialog, [this, list] {
        QListWidgetItem* item = list->currentItem();
        if (item == nullptr ||
            QMessageBox::question(this, tr("Delete the snapshot"),
                                  tr("Delete \"%1\"? It cannot be brought back.")
                                      .arg(item->text().section(QLatin1Char('\n'), 0, 0))) !=
                QMessageBox::Yes) {
            return;
        }
        std::string failed;
        if (editor::delete_snapshot(document_->folder(),
                                    utf8_of(item->data(Qt::UserRole).toString()), failed)) {
            delete item;
        } else {
            report(text_of(failed));
        }
    });
    dialog.exec();
}

void MainWindow::search_assets(const QString& text) {
    if (asset_search_->text() != text) {
        asset_search_->setText(text); // comes back here
        return;
    }
    asset_model_->setNameFilters(asset_filters(text));
    if (!text.trimmed().isEmpty()) {
        asset_tree_->expandAll();
        // Folders not read yet open when they are (see directoryLoaded above).
        const QModelIndex root = asset_tree_->rootIndex();
        for (int row = 0; row < asset_model_->rowCount(root); ++row) {
            const QModelIndex child = asset_model_->index(row, 0, root);
            if (asset_model_->canFetchMore(child)) {
                asset_model_->fetchMore(child);
            }
        }
    }
}

std::vector<AssetUse> MainWindow::show_usages(const QString& asset) {
    if (document_ == nullptr) {
        return {};
    }
    // Saved first: usages are read from the files.
    if (document_->dirty() && !save()) {
        return {};
    }
    const std::vector<AssetUse> uses = find_asset_usages(document_->folder(), utf8_of(asset));
    if (usages_ == nullptr) {
        usages_ = new QDialog(this);
        usages_->setObjectName(QStringLiteral("usages_dialog"));
        usages_->resize(460, 320);
        auto* column = new QVBoxLayout(usages_);
        auto* list = new QListWidget(usages_);
        list->setObjectName(QStringLiteral("usages_list"));
        column->addWidget(list);
        // A scene, conversation or table double-clicked is opened.
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            const QString file = item->data(Qt::UserRole).toString();
            if (file.endsWith(QStringLiteral(".dscene"))) {
                open_scene(file);
            } else if (file.endsWith(QStringLiteral(".dtalk"))) {
                open_story(file);
            } else if (file.endsWith(QStringLiteral(".dtable"))) {
                open_table(file);
            }
        });
    }
    usages_->setWindowTitle(tr("What uses %1").arg(asset));
    auto* list = usages_->findChild<QListWidget*>(QStringLiteral("usages_list"));
    list->clear();
    for (const AssetUse& use : uses) {
        QString line = text_of(use.file);
        if (!use.where.empty()) {
            QStringList names;
            for (const std::string& name : use.where) {
                names << text_of(name);
            }
            line += QStringLiteral(" — ") + names.join(QStringLiteral(", "));
        } else if (use.count > 1) {
            line += tr(" (%1 times)").arg(use.count);
        }
        auto* item = new QListWidgetItem(line, list);
        item->setData(Qt::UserRole, text_of(use.file));
    }
    if (uses.empty()) {
        new QListWidgetItem(tr("Nothing uses it."), list);
    }
    usages_->show();
    usages_->raise();
    return uses;
}

bool MainWindow::rename_asset(const QString& asset, const QString& name) {
    if (document_ == nullptr) {
        return false;
    }
    if (document_->dirty() && !save()) {
        return false;
    }
    // The scene being edited is opened again afterwards, by its new name if it is renamed.
    const QString scene = text_of(document_->scene_path());
    QString scene_after = scene;
    for (const std::string& file : asset_group(document_->folder(), utf8_of(asset))) {
        if (text_of(file) == scene) {
            const QFileInfo was(scene);
            scene_after = (was.path() == QStringLiteral(".") ? QString() : was.path() + '/') +
                          name + '.' + was.suffix();
        }
    }
    const fs::path folder = document_->folder();
    std::vector<std::string> changed;
    std::string error;
    if (!editor::rename_asset(folder, utf8_of(asset), utf8_of(name.trimmed()), changed, error)) {
        report(tr("Not renamed: %1").arg(text_of(error)));
        return false;
    }
    if (!open_project(folder)) {
        return false;
    }
    if (text_of(document_->scene_path()) != scene_after) {
        open_scene(scene_after);
    }
    statusBar()->showMessage(changed.empty()
                                 ? tr("Renamed to %1. What uses it still finds it.").arg(name)
                                 : tr("Renamed to %1, and %2 file(s) that named it updated.")
                                       .arg(name)
                                       .arg(changed.size()),
                             8000);
    return true;
}

bool MainWindow::put_under(engine::Guid entity, engine::Guid parent) {
    if (document_ == nullptr || !document_->set_parent(entity, parent)) {
        statusBar()->showMessage(tr("It cannot go there (not under itself, nor where it is)."),
                                 5000);
        return false;
    }
    changed(true);
    return true;
}

HelpWindow* MainWindow::show_guide(const QString& page) {
    if (guide_ == nullptr) {
        guide_ = new HelpWindow(this);
        guide_->setWindowFlag(Qt::Window);
    }
    if (!page.isEmpty()) {
        guide_->show_page(page);
    }
    guide_->show();
    guide_->raise();
    guide_->activateWindow();
    return guide_;
}

TableWindow* MainWindow::collect_translations() {
    if (document_ == nullptr) {
        return nullptr;
    }
    std::string error;
    const std::int64_t added = document_->collect_translations(error);
    if (added < 0) {
        statusBar()->showMessage(tr("Texts not collected: %1").arg(text_of(error)), 8000);
        return nullptr;
    }
    reload_assets();
    statusBar()->showMessage(
        tr("%n new text(s) to translate in data/translations: fill in a column per language, "
           "or export it as CSV for translators",
           nullptr, static_cast<int>(added)),
        10000);
    return open_table(QStringLiteral("data/translations.dtable"));
}

QString MainWindow::import_aseprite(const QString& asset_path) {
    if (document_ == nullptr) {
        return {};
    }
    std::string error;
    const std::string made = document_->import_aseprite(utf8_of(asset_path), error);
    if (made.empty()) {
        statusBar()->showMessage(tr("Not imported: %1").arg(text_of(error)), 8000);
        return {};
    }
    reload_assets();
    statusBar()->showMessage(
        tr("Imported %1: drag it into the scene; its tags play as animations. Saving in "
           "Aseprite imports it again.")
            .arg(text_of(made)),
        10000);
    return text_of(made);
}

QString MainWindow::make_autotiles(const QString& asset_path) {
    if (document_ == nullptr) {
        return {};
    }
    std::string error;
    const std::string made = document_->make_autotiles(utf8_of(asset_path), error);
    if (made.empty()) {
        statusBar()->showMessage(tr("No auto-tiles made: %1").arg(text_of(error)), 8000);
        return {};
    }
    reload_assets();
    statusBar()->showMessage(
        tr("Made %1: choose it as a tilemap's tileset and turn on Auto-tile, then paint "
           "with each terrain's tile")
            .arg(text_of(made)),
        10000);
    return text_of(made);
}

StoryWindow* MainWindow::open_story(const QString& asset_path) {
    if (document_ == nullptr) {
        return nullptr;
    }
    const fs::path file = (document_->folder() / "assets" / path_of(asset_path)).lexically_normal();
    const std::string key = file.generic_string();
    if (const auto found = stories_.find(key); found != stories_.end()) {
        found->second->show();
        found->second->raise();
        found->second->activateWindow();
        return found->second;
    }
    auto* story = new StoryWindow(this);
    story->setWindowFlag(Qt::Window);
    story->set_quiet(quiet_);
    std::string error;
    if (!story->open(file, error)) {
        delete story;
        report(tr("%1 could not be opened: %2").arg(asset_path, text_of(error)));
        return nullptr;
    }
    connect(story, &StoryWindow::saved, this, [this] { reload_assets(); });
    stories_[key] = story;
    story->show();
    return story;
}

StoryWindow* MainWindow::new_story(const QString& name) {
    if (document_ == nullptr) {
        return nullptr;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')) ||
        trimmed.contains(QLatin1Char('\\')) || trimmed.startsWith(QLatin1Char('.'))) {
        report(tr("A conversation needs a name without slashes, such as \"elder\"."));
        return nullptr;
    }
    const QString relative = QStringLiteral("talk/%1.dtalk").arg(trimmed);
    const fs::path file = document_->folder() / "assets" / path_of(relative);
    std::error_code ignored;
    if (fs::exists(file, ignored)) {
        report(tr("There is a conversation called %1 already.").arg(trimmed));
        return nullptr;
    }
    fs::create_directories(file.parent_path(), ignored);
    std::string error;
    if (!StoryEditor::save(StoryEditor::blank(), file, error)) {
        report(tr("The conversation could not be made: %1").arg(text_of(error)));
        return nullptr;
    }
    reload_assets();
    return open_story(relative);
}

bool MainWindow::make_prefab(const QString& name) {
    std::string error;
    if (document_ == nullptr ||
        !document_->make_prefab(selection_, utf8_of(name.trimmed()), error)) {
        report(tr("The prefab could not be made: %1").arg(text_of(error)));
        return false;
    }
    changed(true);
    statusBar()->showMessage(
        tr("Made prefabs/%1.dprefab: drag it from the Assets into any scene").arg(name.trimmed()),
        6000);
    return true;
}

std::size_t MainWindow::update_prefab_copies() {
    if (document_ == nullptr) {
        return 0;
    }
    const std::string prefab = document_->prefab_of(selection_);
    if (prefab.empty()) {
        report(tr("The selected entity is not a copy of a prefab."));
        return 0;
    }
    std::string error;
    const std::size_t updated = document_->update_prefab_copies(prefab, error);
    if (!error.empty()) {
        report(tr("The copies could not be updated: %1").arg(text_of(error)));
    }
    rebuild_outliner();
    reselect();
    view_->refresh();
    update_title_and_actions();
    statusBar()->showMessage(tr("%n copies updated", nullptr, static_cast<int>(updated)), 5000);
    return updated;
}

SfxGenerator* MainWindow::show_sfx_generator() {
    if (document_ == nullptr) {
        return nullptr;
    }
    auto* generator = new SfxGenerator(document_->folder() / "assets" / "sfx", this);
    generator->setAttribute(Qt::WA_DeleteOnClose);
    connect(generator, &SfxGenerator::saved, this, [this](const fs::path& file) {
        reload_assets();
        statusBar()->showMessage(tr("Saved %1: drag it into the scene, or play it from logic")
                                     .arg(path_text(file.filename())),
                                 6000);
    });
    generator->show();
    return generator;
}

void MainWindow::check_project() {
    doctor_->check(document_.get());
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        if (dock->widget() == doctor_) {
            dock->show();
            dock->raise();
        }
    }
    if (document_ != nullptr) {
        statusBar()->showMessage(tr("Doctor: %1").arg(doctor_->summary()), 8000);
    }
}

void MainWindow::go_to(const Finding& finding) {
    if (document_ == nullptr) {
        return;
    }
    if (!finding.scene.empty() && finding.scene != document_->scene_path() &&
        !open_scene(text_of(finding.scene))) {
        return;
    }
    if (finding.entity.is_valid() && document_->world().find(finding.entity).is_valid()) {
        select(finding.entity);
        return;
    }
    if (!finding.logic.empty()) {
        for (QDockWidget* dock : findChildren<QDockWidget*>()) {
            if (dock->widget() == logic_) {
                dock->show();
                dock->raise();
            }
        }
        const QList<QTreeWidgetItem*> found =
            logic_view()->findItems(text_of(finding.logic), Qt::MatchExactly);
        if (!found.isEmpty()) {
            logic_view()->setCurrentItem(found.front());
        }
        return;
    }
    if (!finding.asset.empty()) {
        const QModelIndex index =
            asset_model_->index(path_text(document_->folder() / "assets" / finding.asset));
        if (index.isValid()) {
            asset_tree_->setCurrentIndex(index);
            asset_tree_->scrollTo(index);
        }
    }
}

bool MainWindow::starting() const noexcept {
    return stack_->currentWidget() == start_;
}

void MainWindow::set_settings(QSettings* settings) {
    settings_ = settings;
    if (settings_ != nullptr) {
        recent_ = settings_->value(QStringLiteral("recent/projects")).toStringList();
    }
    start_->set_recent(recent_);
}

void MainWindow::set_sample(const fs::path& folder) {
    start_->set_sample(folder.empty() ? QString() : path_text(folder));
}

void MainWindow::forget_recent(const QString& folder) {
    recent_.removeAll(folder);
    if (settings_ != nullptr) {
        settings_->setValue(QStringLiteral("recent/projects"), recent_);
    }
    start_->set_recent(recent_);
}

bool MainWindow::close_project() {
    if (document_ == nullptr) {
        return true;
    }
    if (document_->dirty() &&
        !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const auto answer = QMessageBox::question(
            this, tr("Sparxel Editor"), tr("Save the changes to the scene first?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            return false;
        }
        if (answer == QMessageBox::Discard) {
            document_->discard_recovery();
        }
    }
    if (!close_sprites()) {
        return false;
    }
    selection_ = engine::Guid{};
    selected_.clear();
    view_->set_document(nullptr);
    inspector_->show_entity(nullptr, selection_);
    palette_->show_tilemap(nullptr, selection_);
    logic_->set_document(nullptr);
    project_->set_document(nullptr);
    asset_tree_->setRootIndex(QModelIndex());
    document_.reset();
    rebuild_outliner();
    start_->set_recent(recent_);
    stack_->setCurrentWidget(start_);
    update_title_and_actions();
    return true;
}

void MainWindow::build_actions() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    QMenu* game = menuBar()->addMenu(tr("&Game"));
    // The panels: each can be shown again after it was closed, and all put back.
    QMenu* view = menuBar()->addMenu(tr("&View"));
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        view->addAction(dock->toggleViewAction());
    }
    view->addSeparator();
    QAction* reset = view->addAction(tr("&Reset layout"), this, [this] { reset_layout(); });
    reset->setObjectName(QStringLiteral("reset_layout"));
    // The guide, built in: every page searchable, offline.
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* guide = help->addAction(tr("Sparxel &guide"), this, [this] { show_guide({}); });
    guide->setShortcut(QKeySequence::HelpContents);
    guide->setObjectName(QStringLiteral("guide"));
    QAction* lessons = help->addAction(tr("&Tutorials"), this, [this] {
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("tutorials_dock"))) {
            dock->show();
            dock->raise();
        }
    });
    lessons->setObjectName(QStringLiteral("show_tutorials"));
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
                                tr("Diorama: HD-2D, in 3D with light and shadows"),
                                tr("Visual novel: a story in pictures and words"),
                                tr("Platformer: seen from the side, walking and jumping"),
                                tr("RPG: a village with people, a shop and a battle")};
        const QString kind = QInputDialog::getItem(this, tr("New project"), tr("Start from:"),
                                                   kinds, 0, false, &named);
        if (!named) {
            return;
        }
        const QString folder =
            QFileDialog::getExistingDirectory(this, tr("Choose an empty folder for %1").arg(name));
        if (!folder.isEmpty()) {
            const auto chosen =
                static_cast<std::size_t>(std::max<qsizetype>(kinds.indexOf(kind), 0));
            constexpr std::array<ProjectTemplate, 5> templates{
                ProjectTemplate::TopDown2D, ProjectTemplate::Diorama, ProjectTemplate::VisualNovel,
                ProjectTemplate::Platformer, ProjectTemplate::Rpg};
            new_project(path_of(folder), name, templates.at(chosen));
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
    QAction* sprite_action = file->addAction(tr("New s&prite…"), this, [this] {
        if (document_ == nullptr) {
            return;
        }
        QDialog dialog(this);
        dialog.setWindowTitle(tr("New sprite"));
        auto* form = new QFormLayout(&dialog);
        auto* name = new QLineEdit(tr("sprite"), &dialog);
        auto* width = new QSpinBox(&dialog);
        auto* height = new QSpinBox(&dialog);
        for (QSpinBox* box : {width, height}) {
            box->setRange(1, sprite::max_side);
            box->setValue(static_cast<int>(document_->settings().ppu)); // one tile
            box->setSuffix(tr(" pixels"));
        }
        form->addRow(tr("Name"), name);
        form->addRow(tr("Width"), width);
        form->addRow(tr("Height"), height);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            new_sprite(name->text(), width->value(), height->value());
        }
    });
    sprite_action->setObjectName(QStringLiteral("new_sprite"));
    QAction* talk_action = file->addAction(tr("New &conversation…"), this, [this] {
        bool named = false;
        const QString name =
            QInputDialog::getText(this, tr("New conversation"), tr("The conversation's name:"),
                                  QLineEdit::Normal, tr("elder"), &named);
        if (named) {
            new_story(name);
        }
    });
    talk_action->setObjectName(QStringLiteral("new_conversation"));
    QAction* table_action = file->addAction(tr("New &table…"), this, [this] {
        bool named = false;
        const QString name = QInputDialog::getText(this, tr("New table"), tr("The table's name:"),
                                                   QLineEdit::Normal, tr("items"), &named);
        if (named) {
            new_table(name);
        }
    });
    table_action->setObjectName(QStringLiteral("new_table"));
    QAction* sound_action =
        file->addAction(tr("New sound &effect…"), this, [this] { show_sfx_generator(); });
    sound_action->setObjectName(QStringLiteral("new_sound"));
    sprite_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    QAction* doctor = game->addAction(tr("Check the &project"), this, [this] { check_project(); });
    QAction* translate = game->addAction(tr("Collect texts to &translate"), this,
                                         [this] { collect_translations(); });
    translate->setToolTip(tr("Puts every text the game shows in data/translations, with a "
                             "column for each language to translate into"));
    doctor->setObjectName(QStringLiteral("check_project"));
    doctor->setShortcut(QKeySequence(QStringLiteral("F7")));
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
        // Its path in assets/, through the real paths of both: the same folder can be
        // spelled two ways (a short and a long name on Windows, a link).
        std::error_code ignored;
        const fs::path assets = fs::weakly_canonical(document_->folder() / "assets", ignored);
        const fs::path chosen = fs::weakly_canonical(path_of(file_path), ignored);
        const QString relative = text_of(chosen.lexically_proximate(assets).generic_string());
        if (file_path.endsWith(QStringLiteral(".dscene"))) {
            open_scene(relative);
        } else if (file_path.endsWith(QStringLiteral(".dtalk"))) {
            open_story(relative);
        } else if (file_path.endsWith(QStringLiteral(".dtable"))) {
            open_table(relative);
        } else if (file_path.endsWith(QStringLiteral(".aseprite"), Qt::CaseInsensitive) ||
                   file_path.endsWith(QStringLiteral(".ase"), Qt::CaseInsensitive)) {
            import_aseprite(relative);
        } else if (file_path.endsWith(QStringLiteral(".png")) ||
                   file_path.endsWith(QStringLiteral(".dsprite"))) {
            // Pictures and sprites are drawn in Sprite Studio.
            open_sprite(relative);
        }
    });
    // Right-click on a picture: make auto-tiles from it.
    asset_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(asset_tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const QModelIndex index = asset_tree_->indexAt(at);
        if (document_ == nullptr || !index.isValid()) {
            return;
        }
        const QString file_path = asset_model_->filePath(index);
        if (asset_model_->isDir(index)) {
            return;
        }
        const bool aseprite =
            file_path.endsWith(QStringLiteral(".aseprite"), Qt::CaseInsensitive) ||
            file_path.endsWith(QStringLiteral(".ase"), Qt::CaseInsensitive);
        std::error_code ignored;
        const fs::path assets = fs::weakly_canonical(document_->folder() / "assets", ignored);
        const fs::path chosen = fs::weakly_canonical(path_of(file_path), ignored);
        const QString relative = text_of(chosen.lexically_proximate(assets).generic_string());
        QMenu menu(this);
        menu.addAction(tr("Find usages"), this, [this, relative] { show_usages(relative); });
        menu.addAction(tr("Rename…"), this, [this, relative] {
            const QString stem = QFileInfo(relative).completeBaseName();
            bool named = false;
            const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                                       QLineEdit::Normal, stem, &named);
            if (named && name != stem) {
                rename_asset(relative, name);
            }
        });
        if (aseprite) {
            menu.addSeparator();
            menu.addAction(tr("Import again from Aseprite"), this,
                           [this, relative] { import_aseprite(relative); });
        }
        if (!file_path.endsWith(QStringLiteral(".png"))) {
            menu.exec(asset_tree_->viewport()->mapToGlobal(at));
            return;
        }
        menu.addSeparator();
        menu.addAction(tr("Open in Sprite Studio"), this,
                       [this, relative] { open_sprite(relative); });
        QAction* make = menu.addAction(tr("Make auto-tiles"), this,
                                       [this, relative] { make_autotiles(relative); });
        make->setToolTip(tr("Each 2 x 3 tiles of this picture are a terrain; makes a tileset "
                            "of every shape each takes, for a tilemap with Auto-tile on"));
        menu.setToolTipsVisible(true);
        menu.exec(asset_tree_->viewport()->mapToGlobal(at));
    });
    QAction* open = file->addAction(tr("&Open project…"), this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, tr("Open a project"));
        if (!folder.isEmpty()) {
            open_project(path_of(folder));
        }
    });
    open->setShortcut(QKeySequence::Open);
    // Snapshots: restore points of the whole project.
    QAction* snap_take = file->addAction(tr("Take a s&napshot…"), this, [this] {
        bool named = false;
        const QString name =
            QInputDialog::getText(this, tr("Take a snapshot"), tr("What it is before or after:"),
                                  QLineEdit::Normal, tr("Before trying something"), &named);
        if (named) {
            take_snapshot(name);
        }
    });
    snap_take->setObjectName(QStringLiteral("take_snapshot"));
    QAction* snap_list = file->addAction(tr("Snapshots…"), this, [this] { show_snapshots(); });
    snap_list->setObjectName(QStringLiteral("snapshots"));
    QAction* close = file->addAction(tr("&Close project"), this, [this] { close_project(); });
    close->setObjectName(QStringLiteral("close_project"));
    close->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(start_, &StartPage::new_project, fresh, &QAction::trigger);
    connect(start_, &StartPage::open_project, open, &QAction::trigger);
    save_ = file->addAction(tr("&Save"), this, [this] { save(); });
    save_->setShortcut(QKeySequence::Save);
    undo_ = edit->addAction(tr("&Undo"), this, [this] { undo(); });
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = edit->addAction(tr("&Redo"), this, [this] { redo(); });
    redo_->setShortcut(QKeySequence::Redo);
    edit->addSeparator();
    new_entity_ = edit->addAction(tr("&New entity"), this, [this] { new_entity(); });
    delete_ = edit->addAction(tr("&Delete"), this, [this] { delete_selection(); });
    // The clipboard: entities with everything under them, as in any engine.
    QAction* cut_action = edit->addAction(tr("Cu&t"), this, [this] {
        if (copy_selection()) {
            delete_selection();
        }
    });
    cut_action->setShortcut(QKeySequence::Cut);
    cut_action->setObjectName(QStringLiteral("cut"));
    QAction* copy_action = edit->addAction(tr("&Copy"), this, [this] { copy_selection(); });
    copy_action->setShortcut(QKeySequence::Copy);
    copy_action->setObjectName(QStringLiteral("copy"));
    QAction* paste_action =
        edit->addAction(tr("&Paste"), this, [this] { static_cast<void>(paste()); });
    paste_action->setShortcut(QKeySequence::Paste);
    paste_action->setObjectName(QStringLiteral("paste"));
    QAction* twin = edit->addAction(tr("D&uplicate"), this,
                                    [this] { static_cast<void>(duplicate_selection()); });
    twin->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    twin->setObjectName(QStringLiteral("duplicate"));
    QAction* everything = edit->addAction(tr("Select &all"), this, [this] {
        if (document_ == nullptr) {
            return;
        }
        std::vector<engine::Guid> all;
        for (const engine::world::Entity e : document_->entities()) {
            all.push_back(document_->world().guid(e));
        }
        select_many(std::move(all));
    });
    everything->setShortcut(QKeySequence::SelectAll);
    everything->setObjectName(QStringLiteral("select_all"));
    // F, while the scene has the keyboard: the camera to what is selected.
    auto* frame = new QAction(tr("&Frame the selection"), this);
    frame->setObjectName(QStringLiteral("frame_selection"));
    frame->setShortcut(QKeySequence(QStringLiteral("F")));
    frame->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(frame, &QAction::triggered, this, [this] { frame_selection(); });
    view_->addAction(frame);
    edit->addAction(frame);
    QAction* prefab = edit->addAction(tr("Make a &prefab…"), this, [this] {
        if (document_ == nullptr || !selection_.is_valid()) {
            return;
        }
        bool named = false;
        const std::string_view now = document_->world().name(document_->world().find(selection_));
        const QString name = QInputDialog::getText(
            this, tr("Make a prefab"), tr("The prefab's name:"), QLineEdit::Normal,
            now.empty() ? tr("prefab") : text_of(std::string(now)), &named);
        if (named) {
            make_prefab(name);
        }
    });
    prefab->setObjectName(QStringLiteral("make_prefab"));
    prefab->setToolTip(tr("Save the selected entity, with what is under it, as a reusable "
                          "prefab: drag it from the Assets into any scene"));
    QAction* copies = edit->addAction(tr("&Update copies of this prefab"), this,
                                      [this] { update_prefab_copies(); });
    copies->setObjectName(QStringLiteral("update_prefab"));
    delete_->setShortcut(QKeySequence::Delete);
    QAction* find = edit->addAction(tr("Search &everything…"), this, [this] { show_search(); });
    find->setObjectName(QStringLiteral("search"));
    find->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    find->setToolTip(tr("Find any command, scene, picture, logic file or entity by typing"));
    QAction* snap = edit->addAction(tr("&Snap to tiles"));
    snap->setObjectName(QStringLiteral("snap"));
    snap->setCheckable(true);
    snap->setToolTip(tr("Dragged things land on the grid: whole tiles, or finer"));
    connect(snap, &QAction::toggled, view_, &SceneView::set_snap);
    // How finely the grid divides a tile.
    QMenu* grid = edit->addMenu(tr("Snap &grid"));
    auto* grids = new QActionGroup(grid);
    grids->setExclusive(true);
    for (const auto& [divisions, label] :
         {std::pair{1, tr("&Whole tiles")}, std::pair{2, tr("&Half tiles")},
          std::pair{4, tr("&Quarter tiles")}}) {
        QAction* choice = grid->addAction(label);
        choice->setObjectName(QStringLiteral("grid:%1").arg(divisions));
        choice->setCheckable(true);
        choice->setChecked(divisions == view_->snap_divisions());
        grids->addAction(choice);
        connect(choice, &QAction::triggered, view_,
                [this, d = divisions] { view_->set_snap_divisions(d); });
    }
    play_ = game->addAction(tr("&Play"), this, [this] { play(); });
    play_->setShortcut(Qt::Key_F5);
    export_ = game->addAction(tr("&Export…"), this, [this] { export_to({}); });
    for (QAction* action :
         {open, save_, undo_, redo_, new_entity_, delete_, snap, play_, export_}) {
        tools->addAction(action);
    }
}

bool MainWindow::new_project(const fs::path& folder, const QString& name, ProjectTemplate kind) {
    std::string error;
    const QByteArray bytes = name.trimmed().toUtf8();
    if (!create_project(folder,
                        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                        error, kind)) {
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

SpriteStudio* MainWindow::open_sprite(const QString& asset_path, int frame_width,
                                      int frame_height) {
    if (document_ == nullptr) {
        return nullptr;
    }
    const QByteArray utf8 = asset_path.toUtf8();
    const std::string relative(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    const fs::path file = (document_->folder() / "assets" / path_of(asset_path)).lexically_normal();
    fs::path source = file;
    source.replace_extension(".dsprite");
    const std::string key = source.generic_string();
    if (const auto found = studios_.find(key); found != studios_.end()) {
        found->second->show();
        found->second->raise();
        found->second->activateWindow();
        return found->second;
    }
    auto* studio = new SpriteStudio(this);
    studio->setWindowFlag(Qt::Window);
    studio->set_quiet(quiet_);
    std::string error;
    bool opened = false;
    std::error_code ignored;
    if (fs::exists(source, ignored)) {
        opened = studio->open(source, error);
    } else if (file.extension() == ".png") {
        if (frame_width < 0 || frame_height < 0) {
            // The frame size the scene's sprites use with this picture, if any.
            frame_width = 0;
            frame_height = 0;
            const engine::assets::AssetRecord* record = document_->assets().find(relative);
            for (const engine::world::Entity e : document_->entities()) {
                const auto* used = document_->world().get<engine::scene2d::Sprite>(e);
                if (record != nullptr && used != nullptr &&
                    used->image == record->guid.to_string() && used->frame_width > 0 &&
                    used->frame_height > 0) {
                    frame_width = static_cast<int>(used->frame_width);
                    frame_height = static_cast<int>(used->frame_height);
                    break;
                }
            }
            if (!quiet_ && !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
                QDialog dialog(this);
                dialog.setWindowTitle(tr("Open %1 in Sprite Studio").arg(asset_path));
                auto* form = new QFormLayout(&dialog);
                form->addRow(new QLabel(tr("Cut the picture into frames of this size (0: the "
                                           "whole picture is one frame)."),
                                        &dialog));
                auto* across = new QSpinBox(&dialog);
                auto* down = new QSpinBox(&dialog);
                for (QSpinBox* box : {across, down}) {
                    box->setRange(0, sprite::max_side);
                    box->setSuffix(tr(" pixels"));
                }
                across->setValue(frame_width);
                down->setValue(frame_height);
                form->addRow(tr("Frame width"), across);
                form->addRow(tr("Frame height"), down);
                auto* buttons =
                    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
                form->addRow(buttons);
                connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                if (dialog.exec() != QDialog::Accepted) {
                    delete studio;
                    return nullptr;
                }
                frame_width = across->value();
                frame_height = down->value();
            }
        }
        opened = studio->import(file, frame_width, frame_height, error);
    } else {
        error = "Only pictures (PNG) and sprites (.dsprite) open in Sprite Studio.";
    }
    if (!opened) {
        delete studio;
        report(tr("%1 could not be opened in Sprite Studio: %2").arg(asset_path, text_of(error)));
        return nullptr;
    }
    connect(studio, &SpriteStudio::saved, this, [this] { reload_assets(); });
    studios_[key] = studio;
    studio->show();
    return studio;
}

SpriteStudio* MainWindow::new_sprite(const QString& name, int width, int height) {
    if (document_ == nullptr) {
        return nullptr;
    }
    const QString trimmed = name.trimmed();
    const bool valid = !trimmed.isEmpty() && !trimmed.contains(QLatin1Char('/')) &&
                       !trimmed.contains(QLatin1Char('\\')) &&
                       !trimmed.startsWith(QLatin1Char('.'));
    if (!valid) {
        report(tr("A sprite needs a name without slashes, such as \"slime\"."));
        return nullptr;
    }
    const QString relative = QStringLiteral("art/%1.dsprite").arg(trimmed);
    const fs::path file = document_->folder() / "assets" / path_of(relative);
    fs::path sheet = file;
    sheet.replace_extension(".png");
    std::error_code ignored;
    if (fs::exists(file, ignored) || fs::exists(sheet, ignored)) {
        report(tr("There is a sprite or picture called %1 already.").arg(trimmed));
        return nullptr;
    }
    fs::create_directories(file.parent_path(), ignored);
    std::string error;
    sprite::Sprite made = sprite::Sprite::blank(width, height);
    // With the style guide's palette, if the project keeps to one.
    if (std::vector<sprite::Color> palette = sprite::parse_palette(document_->settings().palette);
        !palette.empty()) {
        made.palette = std::move(palette);
    }
    if (!sprite::save(made, file, error)) {
        report(tr("The sprite could not be made: %1").arg(text_of(error)));
        return nullptr;
    }
    reload_assets();
    return open_sprite(relative);
}

void MainWindow::reload_assets() {
    if (document_ == nullptr) {
        return;
    }
    engine::assets::AssetChanges changes = document_->assets().refresh();
    // Aseprite files new or changed (saved in Aseprite): their sheets made again, so what
    // is drawn there shows here at once.
    std::vector<engine::Guid> made;
    for (const std::vector<engine::Guid>* list : {&changes.added, &changes.modified}) {
        for (const engine::Guid& guid : *list) {
            const engine::assets::AssetRecord* record = document_->assets().find(guid);
            if (record == nullptr) {
                continue;
            }
            const QString path = text_of(record->path);
            if (!path.endsWith(QStringLiteral(".aseprite"), Qt::CaseInsensitive) &&
                !path.endsWith(QStringLiteral(".ase"), Qt::CaseInsensitive)) {
                continue;
            }
            std::string error;
            const std::string picture = document_->import_aseprite(record->path, error);
            if (picture.empty()) {
                statusBar()->showMessage(tr("%1: %2").arg(path, text_of(error)), 8000);
                continue;
            }
            // The picture and sheet made count as changed (the import refreshed already).
            for (const std::string& file : {picture, engine::scene2d::sheet_path(picture)}) {
                if (const auto* made_record = document_->assets().find(file)) {
                    made.push_back(made_record->guid);
                }
            }
        }
    }
    if (!made.empty()) {
        changes.modified.insert(changes.modified.end(), made.begin(), made.end());
        statusBar()->showMessage(tr("Imported from Aseprite"), 4000);
    }
    if (changes.empty()) {
        return;
    }
    view_->assets_changed(changes);
    // The inspector's lists of pictures, and the tiles of a tileset that changed.
    inspector_->show_entities(document_.get(), selected_);
    palette_->show_tilemap(document_.get(), selection_);
}

bool MainWindow::close_sprites() {
    for (auto it = studios_.begin(); it != studios_.end();) {
        if (!it->second->close()) {
            return false;
        }
        it->second->deleteLater();
        it = studios_.erase(it);
    }
    for (auto it = stories_.begin(); it != stories_.end();) {
        if (!it->second->close()) {
            return false;
        }
        it->second->deleteLater();
        it = stories_.erase(it);
    }
    for (auto it = tables_.begin(); it != tables_.end();) {
        if (!it->second->close()) {
            return false;
        }
        it->second->deleteLater();
        it = tables_.erase(it);
    }
    return true;
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) {
            // Back from drawing in another program: its pictures show as they are now.
            reload_assets();
        } else {
            // Going to another program: the unsaved work is kept first.
            autosave();
        }
    }
}

bool MainWindow::autosave() {
    if (document_ == nullptr) {
        return false;
    }
    std::string error;
    if (!document_->write_recovery(error)) {
        statusBar()->showMessage(text_of(error), 8000);
        return false;
    }
    return true;
}

bool MainWindow::restore_recovery() {
    std::string error;
    if (document_ == nullptr || !document_->restore_recovery(error)) {
        report(tr("The unsaved work could not be restored: %1").arg(text_of(error)));
        return false;
    }
    show_scene();
    statusBar()->showMessage(tr("Your unsaved work is back. Save to keep it."), 8000);
    return true;
}

bool MainWindow::open_project(const fs::path& folder) {
    if (!close_sprites()) {
        return false;
    }
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
    stack_->setCurrentWidget(view_);
    // To the top of the recent projects.
    std::error_code ignored;
    const QString opened_folder = path_text(fs::weakly_canonical(folder, ignored));
    recent_.removeAll(opened_folder);
    recent_.prepend(opened_folder);
    while (recent_.size() > 10) {
        recent_.removeLast();
    }
    if (settings_ != nullptr) {
        settings_->setValue(QStringLiteral("recent/projects"), recent_);
    }
    start_->set_recent(recent_);
    update_title_and_actions();
    if (!issues.empty()) {
        report(tr("Opened, with notes:\n%1").arg(issues_text(issues)));
    } else {
        statusBar()->showMessage(tr("Opened %1").arg(text_of(document_->settings().name)), 5000);
    }
    // Work a crash left unsaved is offered back.
    if (const std::optional<std::string> scene = recovery_scene(folder);
        scene && !quiet_ && !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const QFileInfo kept(path_text(folder / recovery_file));
        const qint64 minutes =
            std::max<qint64>(0, kept.lastModified().secsTo(QDateTime::currentDateTime()) / 60);
        const auto answer = QMessageBox::question(
            this, tr("Sparxel Editor"),
            tr("Sparxel closed before your changes to %1 were saved. They were kept %2 "
               "minute(s) earlier.\n\nRestore them?")
                .arg(text_of(*scene))
                .arg(minutes),
            QMessageBox::Yes | QMessageBox::Discard, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            restore_recovery();
        } else {
            document_->discard_recovery();
        }
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

bool MainWindow::copy_selection() {
    if (document_ == nullptr || selected_.empty()) {
        return false;
    }
    const std::string text = document_->copy_entities(selected_);
    auto* clip = new QMimeData();
    const QByteArray bytes(text.data(), static_cast<qsizetype>(text.size()));
    clip->setData(QStringLiteral("application/x-sparxel-entities"), bytes);
    clip->setText(QString::fromUtf8(bytes));
    QGuiApplication::clipboard()->setMimeData(clip);
    statusBar()->showMessage(tr("Copied %n entity(s)", nullptr, static_cast<int>(selected_.size())),
                             3000);
    return true;
}

std::vector<engine::Guid> MainWindow::paste() {
    if (document_ == nullptr) {
        return {};
    }
    const QMimeData* clip = QGuiApplication::clipboard()->mimeData();
    const QByteArray bytes = clip == nullptr ? QByteArray()
                             : clip->hasFormat(QStringLiteral("application/x-sparxel-entities"))
                                 ? clip->data(QStringLiteral("application/x-sparxel-entities"))
                                 : clip->text().toUtf8();
    const std::string text(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    // Where the camera looks: the first of them lands there, the rest around it.
    double dx = 0.0;
    double dy = 0.0;
    std::vector<engine::serialize::Issue> ignored;
    if (const std::optional<engine::serialize::Json> json = engine::serialize::parse(text, ignored);
        json && json->is_object() && json->contains("entities") && (*json)["entities"].is_array() &&
        !(*json)["entities"].empty()) {
        const engine::serialize::Json& first = (*json)["entities"][0];
        if (first.contains("components") && first["components"].contains("transform_2d")) {
            const engine::serialize::Json& at = first["components"]["transform_2d"];
            const double x = at.contains("x") && at["x"].is_number() ? at["x"].get<double>() : 0.0;
            const double y = at.contains("y") && at["y"].is_number() ? at["y"].get<double>() : 0.0;
            const double ppu = std::max(1U, document_->settings().ppu);
            dx = (std::round(view_->camera_x() * ppu) / ppu) - x;
            dy = (std::round(view_->camera_y() * ppu) / ppu) - y;
        }
    }
    std::string error;
    std::vector<engine::Guid> pasted = document_->paste_entities(text, dx, dy, error);
    if (pasted.empty()) {
        statusBar()->showMessage(text_of(error), 4000);
        return pasted;
    }
    changed(true);
    select_many(pasted);
    return pasted;
}

std::vector<engine::Guid> MainWindow::duplicate_selection() {
    if (document_ == nullptr) {
        return {};
    }
    std::string error;
    std::vector<engine::Guid> copies = document_->duplicate(selected_, error);
    if (copies.empty()) {
        statusBar()->showMessage(text_of(error), 4000);
        return copies;
    }
    changed(true);
    select_many(copies);
    return copies;
}

void MainWindow::frame_selection() {
    if (document_ == nullptr || selected_.empty()) {
        return;
    }
    // The middle of what is selected and has a place.
    double x = 0.0;
    double y = 0.0;
    int placed = 0;
    for (const engine::Guid guid : selected_) {
        const engine::world::Entity e = document_->world().find(guid);
        if (const auto* at =
                e.is_valid() ? document_->world().get<engine::scene2d::Transform2D>(e) : nullptr) {
            x += at->x;
            y += at->y;
            ++placed;
        }
    }
    if (placed > 0) {
        view_->set_camera(x / placed, y / placed);
    }
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
    // --debug: F1 opens the debug menu, F3 shows timings, while testing (not in exports).
    if (!QProcess::startDetached(path_text(player_),
                                 {path_text(document_->folder()), QStringLiteral("--debug")})) {
        report(tr("The player %1 could not be started.").arg(path_text(player_)));
        return false;
    }
    ++plays_;
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

void MainWindow::save_layout(QSettings& settings) const {
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState(layout_version));
}

bool MainWindow::restore_layout(QSettings& settings) {
    const QByteArray state = settings.value(QStringLiteral("window/state")).toByteArray();
    if (state.isEmpty() || !restoreState(state, layout_version)) {
        return false;
    }
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    return true;
}

void MainWindow::reset_layout() {
    restoreState(first_layout_, layout_version);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!close_sprites()) {
        event->ignore();
        return;
    }
    if (document_ != nullptr && document_->dirty() &&
        !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const auto answer = QMessageBox::question(
            this, tr("Sparxel Editor"), tr("Save the changes to the scene first?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Discard) {
            document_->discard_recovery(); // left unsaved by choice: nothing to offer back
        }
    }
    event->accept();
}

} // namespace editor
