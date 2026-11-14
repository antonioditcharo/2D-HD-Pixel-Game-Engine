// The editor application, driven as a person would, on Qt's offscreen platform (no display
// needed): open the sample, select, edit in the inspector, click and drag in the scene,
// undo, add entities and components, save and export.

#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileSystemModel>
#include <QGroupBox>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "asset_browser.h"
#include "doctor_panel.h"
#include "editor/core/asset_tools.h"
#include "editor/core/new_project.h"
#include "editor/core/snapshots.h"
#include "inspector.h"
#include "logic_panel.h"
#include "main_window.h"
#include "project_panel.h"
#include "recolor_dialog.h"
#include "scene_view.h"
#include "search.h"
#include "start_page.h"
#include "support/aseprite.h"
#include "tile_palette.h"
#include "tutorials.h"

namespace fs = std::filesystem;
namespace game = engine::game;
namespace reflect = engine::reflect;
namespace scene2d = engine::scene2d;
using engine::Guid;

namespace {

struct Sample {
    Sample() {
        path = fs::temp_directory_path() / ("sparxel-editor-app-" + Guid::generate().to_string());
        fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", path, fs::copy_options::recursive);
    }
    Sample(const Sample&) = delete;
    Sample& operator=(const Sample&) = delete;
    Sample(Sample&&) = delete;
    Sample& operator=(Sample&&) = delete;
    ~Sample() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

/// A window on a copy of the sample, sized and shown so the scene view has room.
struct Editor {
    Editor() : window(SPARXEL_PLAYER) {
        window.resize(1400, 900);
        window.show();
        REQUIRE(window.open_project(sample.path));
        QApplication::processEvents();
    }
    [[nodiscard]] Guid guid(std::string_view name) const {
        const auto found = window.document()->world().find_named(name);
        REQUIRE(found.size() == 1);
        return window.document()->world().guid(found[0]);
    }
    Sample sample;
    editor::MainWindow window;
};

void mouse(QWidget* widget, QEvent::Type type, QPoint at, Qt::MouseButton button) {
    QMouseEvent event(type, QPointF(at), widget->mapToGlobal(QPointF(at)), button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : button, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

} // namespace

TEST_CASE("The editor opens the sample: its entities, its scene, its name") {
    Editor e;
    CHECK(e.window.windowTitle() ==
          QStringLiteral("Hello Sparxel: maps/village.dscene — Sparxel Editor"));
    QTreeWidget* list = e.window.outliner();
    REQUIRE(list->topLevelItemCount() == 7);
    CHECK(list->topLevelItem(2)->text(0) == QStringLiteral("hero"));
    const QImage& scene = e.window.scene_view()->image();
    CHECK(scene.width() == 320);
    CHECK(scene.height() == 180);
    // A picture for the docs (docs/dev/editor-v0.1.png), when asked for.
    // (Qt's portable getter: MSVC deprecates std::getenv.)
    if (const QString shot = qEnvironmentVariable("SPARXEL_EDITOR_SCREENSHOT"); !shot.isEmpty()) {
        e.window.select(e.guid("elder"));
        QApplication::processEvents();
        e.window.grab().save(shot);
    }
    // The logic, as plain language: the village's file (its three Rules and a Brain),
    // and the road's.
    QTreeWidget* logic = e.window.logic_view();
    REQUIRE(logic->topLevelItemCount() == 2);
    CHECK(logic->topLevelItem(1)->text(0) == QStringLiteral("logic/road.drules"));
    CHECK(logic->topLevelItem(1)->childCount() == 3);
    QTreeWidgetItem* file = logic->topLevelItem(0);
    CHECK(file->text(0) == QStringLiteral("logic/main.drules"));
    REQUIRE(file->childCount() == 4);
    CHECK(file->child(0)->text(0) == QStringLiteral("When the game starts"));
    CHECK(file->child(1)->text(0) == QStringLiteral("When [who] interacts with it (elder)"));
    CHECK(file->child(1)->childCount() == 7); // its steps; the If holds its own
    CHECK(file->child(3)->text(0) == QStringLiteral("Brain weather"));
    CHECK(file->child(3)->child(0)->text(0) == QStringLiteral("State sunny"));
    CHECK_FALSE(e.window.open_project(e.sample.path / "missing"));
    CHECK(e.window.document() != nullptr); // the open project stays open
}

TEST_CASE("File > New project makes a starter game and opens it") {
    Editor e;
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-new-app-" + Guid::generate().to_string());
    REQUIRE(e.window.findChild<QAction*>(QStringLiteral("new_project")) != nullptr);
    // Over a project: refused, and the open project stays open.
    CHECK_FALSE(e.window.new_project(e.sample.path, QStringLiteral("Over it")));
    CHECK(e.window.statusBar()->currentMessage() ==
          QStringLiteral("The project could not be made:"));
    CHECK(e.window.document()->settings().name == "Hello Sparxel");
    // In a new folder: made, opened, listed and drawn.
    REQUIRE(e.window.new_project(folder, QStringLiteral("  My Quest ")));
    QApplication::processEvents();
    REQUIRE(e.window.document() != nullptr);
    CHECK(e.window.document()->settings().name == "My Quest");
    CHECK(e.window.windowTitle().contains(QStringLiteral("My Quest")));
    CHECK(e.window.document()->entities().size() == 3);
    CHECK(e.window.logic_view()->topLevelItemCount() == 0); // no logic yet
    CHECK_FALSE(e.window.scene_view()->image().isNull());
    CHECK(e.window.scene_view()->image().size() == QSize(320, 180));
    // It is a project like any other: the hero is there to select.
    e.window.select(e.guid("hero"));
    CHECK(e.window.selection() == e.guid("hero"));
    // The Diorama starter: the same game, opened in the 3D style.
    const fs::path folder_3d =
        fs::temp_directory_path() / ("sparxel-new-app-" + Guid::generate().to_string());
    REQUIRE(e.window.new_project(folder_3d, QStringLiteral("My Diorama"),
                                 editor::ProjectTemplate::Diorama));
    QApplication::processEvents();
    CHECK(e.window.document()->settings().style == engine::game::RenderStyle::Diorama);
    CHECK(e.window.document()->settings().look.tilt_shift);
    CHECK(e.window.document()->entities().size() == 3);
    CHECK_FALSE(e.window.scene_view()->image().isNull());
    // The visual novel: a backdrop and Mira, and a story the Logic view reads and checks.
    const fs::path folder_vn =
        fs::temp_directory_path() / ("sparxel-new-app-" + Guid::generate().to_string());
    REQUIRE(e.window.new_project(folder_vn, QStringLiteral("A Story"),
                                 editor::ProjectTemplate::VisualNovel));
    QApplication::processEvents();
    CHECK(e.window.document()->settings().style == engine::game::RenderStyle::Classic2D);
    CHECK(e.window.document()->entities().size() == 2);
    QTreeWidget* story = e.window.logic_view();
    REQUIRE(story->topLevelItemCount() == 1);
    CHECK(story->topLevelItem(0)->text(0) == QStringLiteral("logic/story.drules"));
    REQUIRE(story->topLevelItem(0)->childCount() == 1);
    QTreeWidgetItem* starts = story->topLevelItem(0)->child(0);
    CHECK(starts->text(0) == QStringLiteral("When the game starts"));
    // Say, Ask, Wait until, If and its Otherwise (each holding its own reply).
    REQUIRE(starts->childCount() == 5);
    CHECK(starts->child(3)->childCount() == 1);
    CHECK(starts->child(4)->text(0) == QStringLiteral("Otherwise:"));
    CHECK(starts->child(4)->childCount() == 1);
    CHECK(starts->child(0)->text(0) ==
          QStringLiteral("Say “Mira: Oh! You came after all.” (dialogue)"));
    story->setCurrentItem(starts->child(0));
    QApplication::processEvents();
    auto* problems = e.window.logic_panel()->findChild<QLabel*>(QStringLiteral("logic_problems"));
    REQUIRE(problems != nullptr);
    CHECK(problems->text() == QStringLiteral("logic/story.drules runs: no problems."));
    // The platformer: the level, the hero, who jumps, and coins.
    const fs::path folder_side =
        fs::temp_directory_path() / ("sparxel-new-app-" + Guid::generate().to_string());
    REQUIRE(e.window.new_project(folder_side, QStringLiteral("Up We Go"),
                                 editor::ProjectTemplate::Platformer));
    QApplication::processEvents();
    CHECK(e.window.document()->entities().size() == 5); // with three coins
    e.window.select(e.guid("hero"));
    CHECK(e.window.document()->world().get<engine::game::Jumping>(
              e.window.document()->world().find(e.guid("hero"))) != nullptr);
    CHECK_FALSE(e.window.scene_view()->image().isNull());
    // A picture of it, when asked for.
    if (const QString shot = qEnvironmentVariable("SPARXEL_STARTER_SCREENSHOT"); !shot.isEmpty()) {
        e.window.scene_view()->image().save(shot);
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    fs::remove_all(folder_3d, ignored);
    fs::remove_all(folder_vn, ignored);
    fs::remove_all(folder_side, ignored);
}

TEST_CASE("Scenes from the window: a new one, back by a double-click, and where the game starts") {
    Editor e;
    REQUIRE(e.window.findChild<QAction*>(QStringLiteral("new_scene")) != nullptr);
    QTreeWidget* list = e.window.outliner();
    const int village = list->topLevelItemCount();
    // An unsaved change is saved on the way to the new scene, not lost.
    e.window.select(e.guid("hero"));
    const Guid made = e.window.new_entity();
    REQUIRE(made.is_valid());
    REQUIRE(e.window.document()->dirty());
    REQUIRE(e.window.new_scene(QStringLiteral(" cave ")));
    QApplication::processEvents();
    CHECK(e.window.document()->scene_path() == "maps/cave.dscene");
    CHECK(e.window.windowTitle() ==
          QStringLiteral("Hello Sparxel: maps/cave.dscene — Sparxel Editor"));
    CHECK(list->topLevelItemCount() == 0);
    CHECK_FALSE(e.window.selection().is_valid());
    CHECK(e.window.statusBar()->currentMessage() == QStringLiteral("Editing maps/cave.dscene"));
    // The same name again is refused, with why; the cave stays open.
    CHECK_FALSE(e.window.new_scene(QStringLiteral("cave")));
    CHECK(e.window.statusBar()->currentMessage().startsWith(
        QStringLiteral("The scene could not be made: There is already a file")));
    // Something in the cave; then the village again, by a double-click in the assets.
    REQUIRE(e.window.new_entity().is_valid());
    CHECK(list->topLevelItemCount() == 1);
    auto* assets = e.window.findChild<QTreeView*>(QStringLiteral("assets"));
    REQUIRE(assets != nullptr);
    auto* model = qobject_cast<QFileSystemModel*>(assets->model());
    REQUIRE(model != nullptr);
    const QModelIndex scene = model->index(
        QString::fromStdU16String((e.sample.path / "assets/maps/village.dscene").u16string()));
    REQUIRE(scene.isValid());
    Q_EMIT assets->doubleClicked(scene);
    QApplication::processEvents();
    CHECK(e.window.document()->scene_path() == "maps/village.dscene");
    CHECK(list->topLevelItemCount() == village + 1); // with the entity made before leaving
    CHECK_FALSE(e.window.document()->dirty());
    CHECK_FALSE(e.window.scene_view()->image().isNull());
    // The cave kept what was made in it.
    REQUIRE(e.window.open_scene(QStringLiteral("maps/cave.dscene")));
    CHECK(list->topLevelItemCount() == 1);
    // The game starts where it is told to: an undo step, shown in the status bar.
    REQUIRE(e.window.start_here());
    CHECK(e.window.document()->settings().start_scene == "maps/cave.dscene");
    CHECK(e.window.statusBar()->currentMessage() ==
          QStringLiteral("The game starts in maps/cave.dscene"));
    CHECK(e.window.document()->undo_label() == "Start the game in maps/cave.dscene");
    // What is not a scene is refused.
    CHECK_FALSE(e.window.open_scene(QStringLiteral("art/hero.png")));
    CHECK(e.window.document()->scene_path() == "maps/cave.dscene");
}

TEST_CASE("The panels' layout is kept between sessions, and can be put back") {
    const fs::path file =
        fs::temp_directory_path() / ("sparxel-layout-" + Guid::generate().to_string() + ".ini");
    const QString ini = QString::fromStdU16String(file.u16string());
    {
        Editor e;
        QDockWidget* tiles = e.window.findChild<QDockWidget*>(QStringLiteral("tiles_dock"));
        REQUIRE(tiles != nullptr);
        CHECK_FALSE(tiles->isHidden());
        // Every panel is in the View menu, so a closed one can be opened again.
        CHECK(tiles->toggleViewAction()->isChecked());
        // Panels split beside each other and gather as tabs, anywhere.
        CHECK((e.window.dockOptions() & QMainWindow::AllowNestedDocks) != 0);
        CHECK((e.window.dockOptions() & QMainWindow::GroupedDragging) != 0);
        auto* entities = e.window.findChild<QDockWidget*>(QStringLiteral("entities_dock"));
        auto* assets = e.window.findChild<QDockWidget*>(QStringLiteral("assets_dock"));
        auto* inspector = e.window.findChild<QDockWidget*>(QStringLiteral("inspector_dock"));
        REQUIRE(entities != nullptr);
        REQUIRE(assets != nullptr);
        REQUIRE(inspector != nullptr);
        e.window.splitDockWidget(entities, assets, Qt::Horizontal); // Assets beside Entities
        QApplication::processEvents();
        CHECK(assets->geometry().left() >= entities->geometry().right());
        e.window.tabifyDockWidget(inspector, entities); // Entities among the right's tabs
        QApplication::processEvents();
        CHECK(e.window.tabifiedDockWidgets(inspector).contains(entities));
        CHECK(e.window.dockWidgetArea(entities) == Qt::RightDockWidgetArea);
        tiles->close();
        QApplication::processEvents();
        CHECK(tiles->isHidden());
        CHECK_FALSE(tiles->toggleViewAction()->isChecked());
        QSettings settings(ini, QSettings::IniFormat);
        e.window.save_layout(settings);
        settings.sync();
    }
    // A new session: the Tiles panel is still closed; reset puts it back.
    Editor again;
    QDockWidget* back = again.window.findChild<QDockWidget*>(QStringLiteral("tiles_dock"));
    REQUIRE(back != nullptr);
    CHECK_FALSE(back->isHidden());
    QSettings settings(ini, QSettings::IniFormat);
    REQUIRE(again.window.restore_layout(settings));
    QApplication::processEvents();
    CHECK(back->isHidden());
    // The tabs come back as they were.
    auto* entities_again = again.window.findChild<QDockWidget*>(QStringLiteral("entities_dock"));
    auto* inspector_again = again.window.findChild<QDockWidget*>(QStringLiteral("inspector_dock"));
    REQUIRE(entities_again != nullptr);
    REQUIRE(inspector_again != nullptr);
    CHECK(again.window.dockWidgetArea(entities_again) == Qt::RightDockWidgetArea);
    CHECK(again.window.tabifiedDockWidgets(inspector_again).contains(entities_again));
    auto* reset = again.window.findChild<QAction*>(QStringLiteral("reset_layout"));
    REQUIRE(reset != nullptr);
    reset->trigger();
    QApplication::processEvents();
    CHECK_FALSE(back->isHidden());
    // Nothing saved yet, or something unreadable: nothing changes.
    QSettings empty(
        QString::fromStdU16String(
            (fs::temp_directory_path() / ("sparxel-none-" + Guid::generate().to_string() + ".ini"))
                .u16string()),
        QSettings::IniFormat);
    CHECK_FALSE(again.window.restore_layout(empty));
    settings.setValue(QStringLiteral("window/state"), QByteArray("not a layout"));
    CHECK_FALSE(again.window.restore_layout(settings));
    CHECK_FALSE(back->isHidden());
    std::error_code ignored;
    fs::remove(file, ignored);
}

TEST_CASE("Selecting in the list fills the inspector, whose fields edit the scene") {
    Editor e;
    QTreeWidget* list = e.window.outliner();
    list->topLevelItem(2)->setSelected(true); // the hero
    QApplication::processEvents();
    const Guid hero = e.guid("hero");
    CHECK(e.window.selection() == hero);
    auto* speed = e.window.inspector()->findChild<QDoubleSpinBox*>(
        QStringLiteral("property:top_down_mover/speed"));
    REQUIRE(speed != nullptr);
    CHECK(speed->value() == 4.0);
    CHECK(e.window.inspector()->findChild<QGroupBox*>(QStringLiteral("component:sprite")) !=
          nullptr);
    // Every field has its label: the property's display name.
    auto* position =
        e.window.inspector()->findChild<QGroupBox*>(QStringLiteral("component:transform_2d"));
    REQUIRE(position != nullptr);
    QStringList labels;
    for (const QLabel* label : position->findChildren<QLabel*>()) {
        labels << label->text();
    }
    CHECK(labels.join(QLatin1Char(',')).toStdString() == "Across,Down");
    for (const QLabel* label : position->findChildren<QLabel*>()) {
        CHECK(label->width() >= label->sizeHint().width()); // never squeezed out of sight
    }
    speed->setValue(6.5);
    const auto mover = [&] {
        return e.window.document()->world().get<game::TopDownMover>(
            e.window.document()->world().find(hero));
    };
    CHECK(mover()->speed == 6.5);
    CHECK(
        e.window.windowTitle().startsWith(QStringLiteral("Hello Sparxel: maps/village.dscene *")));
    e.window.undo();
    QApplication::processEvents();
    CHECK(mover()->speed == 4.0);
    // The inspector shows the value as it is now.
    speed = e.window.inspector()->findChild<QDoubleSpinBox*>(
        QStringLiteral("property:top_down_mover/speed"));
    REQUIRE(speed != nullptr);
    CHECK(speed->value() == 4.0);
    e.window.redo();
    CHECK(mover()->speed == 6.5);

    // Renaming in the inspector renames in the list.
    auto* name = e.window.inspector()->findChild<QLineEdit*>(QStringLiteral("name"));
    REQUIRE(name != nullptr);
    name->setText(QStringLiteral("heroine"));
    Q_EMIT name->editingFinished();
    QApplication::processEvents();
    CHECK(list->topLevelItem(2)->text(0) == QStringLiteral("heroine"));
}

TEST_CASE("Clicking in the scene selects what is on top; dragging moves it, in one undo") {
    Editor e;
    editor::SceneView* view = e.window.scene_view();
    const Guid hero = e.guid("hero");
    // The hero's picture covers (9.5, 8) to (10.5, 9): click its middle.
    const QPoint at = view->from_map(10.0, 8.5);
    const QPointF back = view->to_map(at);
    CHECK(std::abs(back.x() - 10.0) < 0.1);
    CHECK(std::abs(back.y() - 8.5) < 0.1);
    mouse(view, QEvent::MouseButtonPress, at, Qt::LeftButton);
    CHECK(e.window.selection() == hero);
    // Drag 2 tiles right and 1 down, in steps.
    const QPoint to = view->from_map(12.0, 9.5);
    for (int i = 1; i <= 4; ++i) {
        mouse(view, QEvent::MouseMove, at + ((to - at) * i) / 4, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, to, Qt::LeftButton);
    const auto* transform = e.window.document()->world().get<scene2d::Transform2D>(
        e.window.document()->world().find(hero));
    CHECK(std::abs(transform->x - 12.0) < 0.07); // to the nearest pixel at 16 per tile
    CHECK(std::abs(transform->y - 10.0) < 0.07);
    CHECK(e.window.document()->undo_label() == "Move hero");
    e.window.undo();
    transform = e.window.document()->world().get<scene2d::Transform2D>(
        e.window.document()->world().find(hero));
    CHECK(transform->x == 10.0);
    CHECK(transform->y == 9.0);
    CHECK_FALSE(e.window.document()->can_undo()); // the whole drag was one step

    // The move handles: dragging the arrow across moves the hero across only, however the
    // cursor wanders; the arrow down, down only. Each drag one undo step.
    REQUIRE(e.window.selection() == hero);
    REQUIRE(view->handles().has_value());
    const editor::SceneView::Handles handles =
        view->handles().value_or(editor::SceneView::Handles{});
    const auto by_handle = [&](QPoint head, QPoint by) {
        mouse(view, QEvent::MouseButtonPress, head, Qt::LeftButton);
        for (int i = 1; i <= 4; ++i) {
            mouse(view, QEvent::MouseMove, head + (by * i) / 4, Qt::LeftButton);
        }
        mouse(view, QEvent::MouseButtonRelease, head + by, Qt::LeftButton);
    };
    const QPoint tile = view->from_map(11.0, 10.0) - view->from_map(10.0, 9.0); // one tile each way
    by_handle(handles.across.center(), QPoint(tile.x() * 2, tile.y()));
    transform = e.window.document()->world().get<scene2d::Transform2D>(
        e.window.document()->world().find(hero));
    CHECK(std::abs(transform->x - 12.0) < 0.07);
    CHECK(transform->y == 9.0);
    CHECK(e.window.document()->undo_label() == "Move hero");
    REQUIRE(view->handles().has_value());
    const editor::SceneView::Handles moved = view->handles().value_or(editor::SceneView::Handles{});
    by_handle(moved.down.center(), QPoint(-tile.x() * 3, tile.y()));
    transform = e.window.document()->world().get<scene2d::Transform2D>(
        e.window.document()->world().find(hero));
    CHECK(std::abs(transform->x - 12.0) < 0.07);
    CHECK(std::abs(transform->y - 10.0) < 0.07);
    e.window.undo();
    e.window.undo();
    transform = e.window.document()->world().get<scene2d::Transform2D>(
        e.window.document()->world().find(hero));
    CHECK(transform->x == 10.0);
    CHECK(transform->y == 9.0);

    // Turning: the knob above the hero, dragged a quarter of the way round its feet
    // (clockwise) with snapping on, turns it exactly 90 degrees. Scaling: the square at its
    // top-right corner, dragged twice as far from its feet, doubles it. One undo step each.
    REQUIRE(view->handles().has_value());
    const editor::SceneView::Handles shaping =
        view->handles().value_or(editor::SceneView::Handles{});
    REQUIRE_FALSE(shaping.turn.isNull());
    REQUIRE_FALSE(shaping.stretch.isNull());
    CHECK(shaping.turn.center().y() < shaping.origin.y() - 20); // above the picture
    view->set_snap(true);
    const QPointF feet(shaping.origin);
    const QPointF up = QPointF(shaping.turn.center()) - feet;
    const double radius = std::hypot(up.x(), up.y());
    mouse(view, QEvent::MouseButtonPress, shaping.turn.center(), Qt::LeftButton);
    QPoint round_to;
    for (int i = 1; i <= 6; ++i) {
        const double angle = (3.14159265358979 / 2.0) * i / 6.0;
        round_to = (feet + QPointF(std::sin(angle), -std::cos(angle)) * radius).toPoint();
        mouse(view, QEvent::MouseMove, round_to, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, round_to, Qt::LeftButton);
    const auto sprite = [&] {
        return *e.window.document()->world().get<scene2d::Sprite>(
            e.window.document()->world().find(hero));
    };
    CHECK(sprite().rotation == 90.0);
    CHECK(e.window.document()->undo_label() == "Turn hero");
    REQUIRE(view->handles().has_value());
    const QPoint corner = view->handles().value_or(editor::SceneView::Handles{}).stretch.center();
    const QPoint far = shaping.origin + ((corner - shaping.origin) * 2);
    mouse(view, QEvent::MouseButtonPress, corner, Qt::LeftButton);
    for (int i = 1; i <= 4; ++i) {
        mouse(view, QEvent::MouseMove, corner + ((far - corner) * i) / 4, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, far, Qt::LeftButton);
    CHECK(sprite().scale_x == 2.0);
    CHECK(sprite().scale_y == 2.0);
    CHECK(sprite().rotation == 90.0);
    CHECK(e.window.document()->undo_label() == "Scale hero");
    e.window.undo();
    CHECK(sprite().scale_x == 1.0);
    CHECK(sprite().rotation == 90.0);
    e.window.undo();
    CHECK(sprite().rotation == 0.0);

    // The pivot ring, on the hero's feet, dragged to the middle of its picture with
    // snapping on: the pivot is the middle, the position goes there, and the picture stays.
    REQUIRE(view->handles().has_value());
    const QRect ring = view->handles().value_or(editor::SceneView::Handles{}).pivot;
    REQUIRE_FALSE(ring.isNull());
    CHECK(ring.contains(view->from_map(10.0, 9.0)));
    const QPoint middle_of_picture = view->from_map(10.0, 8.48);
    mouse(view, QEvent::MouseButtonPress, ring.center(), Qt::LeftButton);
    mouse(view, QEvent::MouseMove, (ring.center() + middle_of_picture) / 2, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, middle_of_picture, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, middle_of_picture, Qt::LeftButton);
    CHECK(sprite().pivot_x == 0.5);
    CHECK(sprite().pivot_y == 0.5);
    transform = e.window.document()->world().get<scene2d::Transform2D>(
        e.window.document()->world().find(hero));
    CHECK(transform->x == 10.0);
    CHECK(transform->y == 8.5);
    CHECK(e.window.document()->undo_label() == "Move the pivot of hero");
    e.window.undo();
    CHECK(sprite().pivot_y == 1.0);
    view->set_snap(false);

    // Clicking where nothing is drawn selects nothing; the ground is under most places.
    mouse(view, QEvent::MouseButtonPress, view->from_map(3.0, 3.0), Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, view->from_map(3.0, 3.0), Qt::LeftButton);
    CHECK(e.window.selection() == e.guid("ground"));
}

TEST_CASE("Several pictures turned and scaled together by the handles") {
    Editor e;
    editor::SceneView* view = e.window.scene_view();
    const Guid hero = e.guid("hero");
    const Guid elder = e.guid("elder");
    REQUIRE(e.window.document()->turn_sprite(elder, 30.0));
    e.window.document()->end_merge();
    const auto click = [&](QPointF map, Qt::KeyboardModifiers held) {
        const QPoint at = view->from_map(map.x(), map.y());
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), view->mapToGlobal(QPointF(at)),
                          Qt::LeftButton, Qt::LeftButton, held);
        QApplication::sendEvent(view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), view->mapToGlobal(QPointF(at)),
                            Qt::LeftButton, Qt::NoButton, held);
        QApplication::sendEvent(view, &release);
    };
    click(QPointF(10.0, 8.5), Qt::NoModifier);
    click(QPointF(6.0, 4.5), Qt::ControlModifier);
    REQUIRE(e.window.selected() == std::vector<Guid>{hero, elder});
    const auto sprite = [&](Guid who) {
        return *e.window.document()->world().get<scene2d::Sprite>(
            e.window.document()->world().find(who));
    };
    const auto at = [&](Guid who) {
        return *e.window.document()->world().get<scene2d::Transform2D>(
            e.window.document()->world().find(who));
    };
    // The hero's knob dragged a quarter turn round the pair's middle, (8, 7), with snapping
    // on: they swing round it, the hero from (10, 9) to (6, 9) and the elder from (6, 5) to
    // (10, 5), and each picture turns as much, the hero to 90 degrees and the elder from 30
    // to 120. One step.
    view->set_snap(true);
    REQUIRE(view->handles().has_value());
    const editor::SceneView::Handles shaping =
        view->handles().value_or(editor::SceneView::Handles{});
    const std::optional<QPoint> middle_point = view->on_screen(8.0, 7.0);
    REQUIRE(middle_point.has_value());
    const QPointF middle(middle_point.value_or(QPoint{}));
    const QPointF knob = QPointF(shaping.turn.center()) - middle;
    mouse(view, QEvent::MouseButtonPress, shaping.turn.center(), Qt::LeftButton);
    QPoint round_to;
    for (int i = 1; i <= 6; ++i) {
        const double angle = (3.14159265358979 / 2.0) * i / 6.0; // clockwise, y down
        const QPointF turned((knob.x() * std::cos(angle)) - (knob.y() * std::sin(angle)),
                             (knob.x() * std::sin(angle)) + (knob.y() * std::cos(angle)));
        round_to = (middle + turned).toPoint();
        mouse(view, QEvent::MouseMove, round_to, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, round_to, Qt::LeftButton);
    CHECK(sprite(hero).rotation == 90.0);
    CHECK(sprite(elder).rotation == 120.0);
    CHECK(at(hero).x == 6.0);
    CHECK(at(hero).y == 9.0);
    CHECK(at(elder).x == 10.0);
    CHECK(at(elder).y == 5.0);
    CHECK(e.window.document()->undo_label() == "Turn 2 entities");
    CHECK(e.window.selected() == std::vector<Guid>{hero, elder}); // still both
    // The hero's square, dragged twice as far from the middle: both doubled, and twice as
    // far from it, the hero at (4, 11) and the elder at (12, 3).
    REQUIRE(view->handles().has_value());
    const QPoint corner = view->handles().value_or(editor::SceneView::Handles{}).stretch.center();
    const QPointF far = middle + ((QPointF(corner) - middle) * 2.0);
    mouse(view, QEvent::MouseButtonPress, corner, Qt::LeftButton);
    for (int i = 1; i <= 4; ++i) {
        mouse(view, QEvent::MouseMove,
              (QPointF(corner) + ((far - QPointF(corner)) * i / 4.0)).toPoint(), Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, far.toPoint(), Qt::LeftButton);
    CHECK(sprite(hero).scale_x == 2.0);
    CHECK(sprite(elder).scale_y == 2.0);
    CHECK(at(hero).x == 4.0);
    CHECK(at(hero).y == 11.0);
    CHECK(at(elder).x == 12.0);
    CHECK(at(elder).y == 3.0);
    CHECK(e.window.document()->undo_label() == "Scale 2 entities");
    e.window.undo();
    e.window.undo();
    CHECK(sprite(hero).rotation == 0.0);
    CHECK(sprite(elder).rotation == 30.0);
    CHECK(sprite(elder).scale_x == 1.0);
    CHECK(at(hero).x == 10.0);
    CHECK(at(elder).y == 5.0);
    view->set_snap(false);
}

TEST_CASE("Several entities selected: Ctrl-click and the list, dragged and deleted together") {
    Editor e;
    editor::SceneView* view = e.window.scene_view();
    const Guid hero = e.guid("hero");
    const Guid elder = e.guid("elder");
    const auto click = [&](QPointF map, Qt::KeyboardModifiers held) {
        const QPoint at = view->from_map(map.x(), map.y());
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), view->mapToGlobal(QPointF(at)),
                          Qt::LeftButton, Qt::LeftButton, held);
        QApplication::sendEvent(view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), view->mapToGlobal(QPointF(at)),
                            Qt::LeftButton, Qt::NoButton, held);
        QApplication::sendEvent(view, &release);
    };
    const auto at = [&](Guid guid) {
        return *e.window.document()->world().get<scene2d::Transform2D>(
            e.window.document()->world().find(guid));
    };
    // The hero, then Ctrl-click the elder: both, the hero first (in the inspector).
    click(QPointF(10.0, 8.5), Qt::NoModifier);
    click(QPointF(6.0, 4.5), Qt::ControlModifier);
    CHECK(e.window.selected() == std::vector<Guid>{hero, elder});
    CHECK(e.window.selection() == hero);
    CHECK(e.window.outliner()->selectedItems().size() == 2);
    // Dragging the elder moves both, by the same amount, as one undo step.
    const QPoint from = view->from_map(6.0, 4.5);
    const QPoint to = view->from_map(7.0, 5.5);
    mouse(view, QEvent::MouseButtonPress, from, Qt::LeftButton);
    CHECK(e.window.selected().size() == 2); // pressing on one of the group keeps the group
    for (int i = 1; i <= 4; ++i) {
        mouse(view, QEvent::MouseMove, from + ((to - from) * i) / 4, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, to, Qt::LeftButton);
    CHECK(std::abs(at(hero).x - 11.0) < 0.07);
    CHECK(std::abs(at(hero).y - 10.0) < 0.07);
    CHECK(std::abs(at(elder).x - 7.0) < 0.07);
    CHECK(std::abs(at(elder).y - 6.0) < 0.07);
    CHECK(e.window.document()->undo_label() == "Move 2 entities");
    e.window.undo();
    CHECK(at(hero).x == 10.0);
    CHECK(at(elder).x == 6.0);
    CHECK_FALSE(e.window.document()->can_undo());
    // Snapping on: the hero, grabbed with the elder a little off its own tile, lands on
    // whole tiles; the elder moves by as much, keeping its place beside it.
    auto* snap = e.window.findChild<QAction*>(QStringLiteral("snap"));
    REQUIRE(snap != nullptr);
    snap->setChecked(true);
    CHECK(view->snap());
    e.window.document()->move_entities({{elder, {6.25, 5.0}}});
    e.window.document()->end_merge();
    const QPoint grab = view->from_map(10.0, 8.5);
    const QPoint drop = view->from_map(11.3, 9.1); // 1.3 across and 0.6 down
    mouse(view, QEvent::MouseButtonPress, grab, Qt::LeftButton);
    for (int i = 1; i <= 3; ++i) {
        mouse(view, QEvent::MouseMove, grab + ((drop - grab) * i) / 3, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, drop, Qt::LeftButton);
    CHECK(at(hero).x == 11.0);
    CHECK(at(hero).y == 10.0);
    CHECK(at(elder).x == 7.25);
    CHECK(at(elder).y == 6.0);
    e.window.undo();
    // On a grid of quarter tiles, dragged 1.24 across and 0.76 down: the hero lands on
    // (11.25, 9.75), and the elder moves by as much.
    CHECK(view->snap_divisions() == 1);
    CHECK_FALSE(view->set_snap_divisions(3)); // not a grid offered
    auto* quarter = e.window.findChild<QAction*>(QStringLiteral("grid:4"));
    auto* whole = e.window.findChild<QAction*>(QStringLiteral("grid:1"));
    REQUIRE(quarter != nullptr);
    REQUIRE(whole != nullptr);
    CHECK(whole->isChecked());
    quarter->trigger();
    CHECK(view->snap_divisions() == 4);
    CHECK(quarter->isChecked());
    CHECK_FALSE(whole->isChecked());
    const QPoint finer = view->from_map(11.24, 9.26);
    mouse(view, QEvent::MouseButtonPress, grab, Qt::LeftButton);
    for (int i = 1; i <= 3; ++i) {
        mouse(view, QEvent::MouseMove, grab + ((finer - grab) * i) / 3, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, finer, Qt::LeftButton);
    CHECK(at(hero).x == 11.25);
    CHECK(at(hero).y == 9.75);
    CHECK(at(elder).x == 7.5);
    CHECK(at(elder).y == 5.75);
    whole->trigger();
    CHECK(view->snap_divisions() == 1);
    e.window.undo(); // the drag
    e.window.undo(); // the elder put off its tile
    snap->setChecked(false);
    CHECK(at(elder).x == 6.0);
    CHECK(e.window.selected().size() == 2); // still both after undo
    // Ctrl-click the hero again: out; Shift-click: back in, last.
    click(QPointF(10.0, 8.5), Qt::ControlModifier);
    CHECK(e.window.selected() == std::vector<Guid>{elder});
    click(QPointF(10.0, 8.5), Qt::ShiftModifier);
    CHECK(e.window.selected() == std::vector<Guid>{elder, hero});
    // Delete removes both, as one step; undo brings both back, still selected.
    const int listed = e.window.outliner()->topLevelItemCount();
    e.window.delete_selection();
    CHECK(e.window.outliner()->topLevelItemCount() == listed - 2);
    CHECK(e.window.selected().empty());
    CHECK(e.window.document()->undo_label() == "Delete 2 entities");
    e.window.undo();
    CHECK(e.window.outliner()->topLevelItemCount() == listed);
    // A plain click on one of them: that one alone.
    click(QPointF(10.0, 8.5), Qt::NoModifier);
    CHECK(e.window.selected() == std::vector<Guid>{hero});
    // In the list: several items selected select them all, the current one first.
    QTreeWidget* list = e.window.outliner();
    QTreeWidgetItem* hero_item = nullptr;
    QTreeWidgetItem* lamp_item = nullptr;
    for (QTreeWidgetItemIterator it(list); *it != nullptr; ++it) {
        if ((*it)->text(0) == QStringLiteral("hero")) {
            hero_item = *it;
        } else if ((*it)->text(0) == QStringLiteral("lamp")) {
            lamp_item = *it;
        }
    }
    REQUIRE(hero_item != nullptr);
    REQUIRE(lamp_item != nullptr);
    list->setCurrentItem(lamp_item, 0, QItemSelectionModel::Select);
    CHECK(e.window.selected() == std::vector<Guid>{e.guid("lamp"), hero});
    CHECK(e.window.selection() == e.guid("lamp"));

    // A box dragged from the ground selects every picture it touches, in the scene's order;
    // over no picture, nothing. A click on the ground still selects the ground.
    const auto drag_box = [&](QPointF corner, QPointF opposite) {
        const QPoint a = view->from_map(corner.x(), corner.y());
        const QPoint b = view->from_map(opposite.x(), opposite.y());
        mouse(view, QEvent::MouseButtonPress, a, Qt::LeftButton);
        for (int i = 1; i <= 3; ++i) {
            mouse(view, QEvent::MouseMove, a + ((b - a) * i) / 3, Qt::LeftButton);
        }
        mouse(view, QEvent::MouseButtonRelease, b, Qt::LeftButton);
    };
    drag_box(QPointF(4.0, 3.0), QPointF(11.0, 9.5));
    CHECK(e.window.selected() == std::vector<Guid>{hero, elder});
    // The inspector shows what both have, marks what differs, and changes both at once.
    editor::Inspector* inspector = e.window.inspector();
    QApplication::processEvents();
    auto* many = inspector->findChild<QLabel*>(QStringLiteral("many"));
    REQUIRE(many != nullptr);
    CHECK(many->text() == QStringLiteral("2 selected. A change here changes them all."));
    CHECK(inspector->findChild<QLineEdit*>(QStringLiteral("name")) == nullptr);
    CHECK(inspector->findChild<QGroupBox*>(QStringLiteral("component:sprite")) != nullptr);
    CHECK(inspector->findChild<QGroupBox*>(QStringLiteral("component:top_down_mover")) ==
          nullptr); // the elder has none
    auto* across = inspector->findChild<QDoubleSpinBox*>(QStringLiteral("property:transform_2d/x"));
    REQUIRE(across != nullptr);
    CHECK(across->value() == 10.0); // the first's
    bool marked = false;
    for (const QLabel* label : inspector->findChildren<QLabel*>()) {
        marked = marked || label->text() == QStringLiteral("Across (differs)");
    }
    CHECK(marked);
    across->setValue(8.0);
    QApplication::processEvents();
    CHECK(at(hero).x == 8.0);
    CHECK(at(elder).x == 8.0);
    CHECK(e.window.document()->undo_label() == "Change Across of 2 entities");
    e.window.undo();
    QApplication::processEvents();
    CHECK(at(hero).x == 10.0);
    CHECK(at(elder).x == 6.0);
    CHECK(e.window.selected() == std::vector<Guid>{hero, elder});
    // One name for both, numbered; listed so; undone together.
    auto* name_all = inspector->findChild<QLineEdit*>(QStringLiteral("name_all"));
    REQUIRE(name_all != nullptr);
    name_all->setText(QStringLiteral(" pal "));
    Q_EMIT name_all->editingFinished();
    QApplication::processEvents();
    CHECK(e.window.document()->world().name(e.window.document()->world().find(hero)) == "pal_1");
    CHECK(e.window.document()->world().name(e.window.document()->world().find(elder)) == "pal_2");
    bool renamed_listed = false;
    for (QTreeWidgetItemIterator it(e.window.outliner()); *it != nullptr; ++it) {
        renamed_listed = renamed_listed || (*it)->text(0) == QStringLiteral("pal_2");
    }
    CHECK(renamed_listed);
    CHECK(e.window.document()->undo_label() == "Rename 2 entities");
    e.window.undo();
    QApplication::processEvents();
    CHECK(e.window.document()->world().name(e.window.document()->world().find(hero)) == "hero");
    inspector = e.window.inspector();
    // A component added to both at once (only the elder lacked a Top-down Mover), then
    // removed from both: one undo step each.
    const auto has_mover = [&](Guid g) {
        return e.window.document()->world().get<engine::game::TopDownMover>(
                   e.window.document()->world().find(g)) != nullptr;
    };
    auto* add = inspector->findChild<QComboBox*>(QStringLiteral("add_component"));
    REQUIRE(add != nullptr);
    CHECK(add->itemText(0) == QStringLiteral("Add a component to all\u2026"));
    CHECK(add->findText(QStringLiteral("Sprite")) < 0); // both have one
    const int mover = add->findText(QStringLiteral("Top-down Mover"));
    REQUIRE(mover > 0);
    Q_EMIT add->activated(mover);
    QApplication::processEvents();
    CHECK(has_mover(hero));
    CHECK(has_mover(elder));
    CHECK(e.window.document()->undo_label() == "Add Top-down Mover to elder");
    auto* remove = inspector->findChild<QPushButton*>(QStringLiteral("remove:top_down_mover"));
    REQUIRE(remove != nullptr);
    CHECK(remove->text() == QStringLiteral("Remove Top-down Mover from all"));
    remove->click();
    QApplication::processEvents();
    CHECK_FALSE(has_mover(hero));
    CHECK_FALSE(has_mover(elder));
    CHECK(e.window.document()->undo_label() == "Remove Top-down Mover from 2 entities");
    e.window.undo();
    e.window.undo();
    QApplication::processEvents();
    CHECK(has_mover(hero));
    CHECK_FALSE(has_mover(elder));
    CHECK(at(hero).x == 10.0); // selecting moved nothing
    CHECK(at(elder).x == 6.0);
    drag_box(QPointF(13.0, 2.0), QPointF(15.0, 3.5));
    CHECK(e.window.selected().empty());
    click(QPointF(3.0, 3.0), Qt::NoModifier);
    CHECK(e.window.selected() == std::vector<Guid>{e.guid("ground")});
    // A press and a release with no drag between them is a click, even if they land apart
    // (on Windows the view can change size between them: the tile palette appears).
    mouse(view, QEvent::MouseButtonPress, view->from_map(13.0, 2.0), Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, view->from_map(9.0, 9.5), Qt::LeftButton);
    CHECK(e.window.selected() == std::vector<Guid>{e.guid("ground")});
    CHECK_FALSE(e.window.document()->dirty());
}

TEST_CASE("New entities get components from the inspector, and saving and exporting work") {
    Editor e;
    const Guid made = e.window.new_entity();
    CHECK(e.window.selection() == made);
    CHECK(e.window.outliner()->topLevelItemCount() == 8);
    auto* add = e.window.inspector()->findChild<QComboBox*>(QStringLiteral("add_component"));
    REQUIRE(add != nullptr);
    const int light = add->findText(QStringLiteral("Light"));
    REQUIRE(light > 0);
    add->setCurrentIndex(light);
    Q_EMIT add->activated(light);
    QApplication::processEvents();
    CHECK(e.window.document()->world().get<scene2d::Light>(
              e.window.document()->world().find(made)) != nullptr);
    CHECK(e.window.inspector()->findChild<QDoubleSpinBox*>(
              QStringLiteral("property:light/radius")) != nullptr);

    // Saved: the scene file now has the new entity, and the title no star.
    REQUIRE(e.window.save());
    CHECK_FALSE(e.window.windowTitle().contains(QLatin1Char('*')));
    std::vector<engine::serialize::Issue> issues;
    const auto reopened = editor::Document::open(e.sample.path, issues);
    REQUIRE(reopened != nullptr);
    CHECK(reopened->world().find(made).is_valid());

    // Deleting the selection, then exporting the game.
    e.window.delete_selection();
    CHECK(e.window.outliner()->topLevelItemCount() == 7);
    const fs::path out =
        e.sample.path.parent_path() / ("sparxel-editor-export-" + Guid::generate().to_string());
    CHECK(e.window.export_to(out)); // saves first, since the deletion is not saved
    CHECK_FALSE(e.window.document()->dirty());
    CHECK(fs::exists(out / "game" / "project.sparxel.json"));
    std::error_code ignored;
    fs::remove_all(out, ignored);
    // Exporting where something already is refuses, saying why, and changes nothing.
    CHECK_FALSE(e.window.export_to(e.sample.path));
}

TEST_CASE("Painting a tilemap from the palette, a stroke at a time") {
    Editor e;
    const Guid ground = e.guid("ground");
    e.window.select(ground);
    QApplication::processEvents();
    editor::TilePalette* palette = e.window.tile_palette();
    // The sample's tileset has three tiles (grass, path, stone), and the eraser.
    auto* path = palette->findChild<QToolButton*>(QStringLiteral("tile:1"));
    REQUIRE(path != nullptr);
    CHECK(palette->findChild<QToolButton*>(QStringLiteral("tile:2")) != nullptr);
    CHECK(palette->findChild<QToolButton*>(QStringLiteral("tile:3")) == nullptr);
    REQUIRE(palette->findChild<QToolButton*>(QStringLiteral("tile:erase")) != nullptr);
    path->click();
    editor::SceneView* view = e.window.scene_view();
    CHECK(view->painting());
    CHECK(path->isChecked());

    const auto cell = [&](std::uint32_t x, std::uint32_t y) {
        std::string error;
        const scene2d::TileGrid* grid =
            e.window.document()
                ->world()
                .get<scene2d::Tilemap>(e.window.document()->world().find(ground))
                ->grid(error);
        REQUIRE(grid != nullptr);
        return grid->at(x, y);
    };
    // A stroke from (3.5, 2.5) to (6.5, 2.5) paints a path four tiles long.
    const QPoint from = view->from_map(3.5, 2.5);
    const QPoint to = view->from_map(6.5, 2.5);
    mouse(view, QEvent::MouseButtonPress, from, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i) {
        mouse(view, QEvent::MouseMove, from + ((to - from) * i) / 12, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, to, Qt::LeftButton);
    for (std::uint32_t x = 3; x <= 6; ++x) {
        CHECK(cell(x, 2) == 1);
    }
    CHECK(cell(7, 2) == 0);
    CHECK(e.window.selection() == ground); // painting does not select
    // The right button erases, as its own stroke.
    mouse(view, QEvent::MouseButtonPress, view->from_map(4.5, 2.5), Qt::RightButton);
    mouse(view, QEvent::MouseButtonRelease, view->from_map(4.5, 2.5), Qt::RightButton);
    CHECK(cell(4, 2) == -1);
    e.window.undo(); // the erasing
    CHECK(cell(4, 2) == 1);
    e.window.undo(); // the whole first stroke
    CHECK(cell(3, 2) == 0);
    CHECK(cell(6, 2) == 0);

    // The rectangle tool: dragged from (3.5, 2.5) to (5.5, 4.5), painted on release, 3 by
    // 3 cells as one undo step.
    const auto tool = [&](int number) {
        auto* button =
            palette->findChild<QToolButton*>(QStringLiteral("paint_tool:%1").arg(number));
        REQUIRE(button != nullptr);
        CHECK(button->isVisibleTo(palette));
        button->click();
    };
    const auto start_painting = [&] {
        path = palette->findChild<QToolButton*>(QStringLiteral("tile:1"));
        REQUIRE(path != nullptr);
        if (!view->painting()) {
            path->click();
        }
        REQUIRE(view->painting());
    };
    start_painting();
    tool(1);
    CHECK(view->paint_tool() == editor::SceneView::PaintTool::Rectangle);
    mouse(view, QEvent::MouseButtonPress, view->from_map(3.5, 2.5), Qt::LeftButton);
    mouse(view, QEvent::MouseMove, view->from_map(4.5, 3.5), Qt::LeftButton);
    CHECK(cell(3, 2) == 0); // nothing yet: on release
    mouse(view, QEvent::MouseButtonRelease, view->from_map(5.5, 4.5), Qt::LeftButton);
    for (std::uint32_t y = 2; y <= 4; ++y) {
        for (std::uint32_t x = 3; x <= 5; ++x) {
            CHECK(cell(x, y) == 1);
        }
    }
    CHECK(cell(6, 4) == 0);
    CHECK(e.window.document()->undo_label() == "Paint a rectangle of ground");
    e.window.undo();
    CHECK(cell(4, 3) == 0);
    // The fill tool: a ring of path, then one click inside fills what the ring holds.
    start_painting();
    mouse(view, QEvent::MouseButtonPress, view->from_map(3.5, 2.5), Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, view->from_map(5.5, 4.5), Qt::LeftButton);
    start_painting();
    auto* stone = palette->findChild<QToolButton*>(QStringLiteral("tile:2"));
    REQUIRE(stone != nullptr);
    stone->click();
    tool(2);
    mouse(view, QEvent::MouseButtonPress, view->from_map(4.5, 3.5), Qt::RightButton); // a hole
    mouse(view, QEvent::MouseButtonRelease, view->from_map(4.5, 3.5), Qt::RightButton);
    CHECK(cell(4, 3) == -1);
    CHECK(cell(3, 2) == -1); // the right button erases the whole joined area
    e.window.undo();
    CHECK(cell(4, 3) == 1);
    start_painting();
    stone = palette->findChild<QToolButton*>(QStringLiteral("tile:2"));
    REQUIRE(stone != nullptr);
    stone->click();
    mouse(view, QEvent::MouseButtonPress, view->from_map(4.5, 3.5), Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, view->from_map(4.5, 3.5), Qt::LeftButton);
    CHECK(cell(3, 2) == 2);
    CHECK(cell(5, 4) == 2);
    CHECK(cell(6, 4) == 0); // the grass around is not joined to it
    CHECK(e.window.document()->undo_label() == "Fill ground");
    e.window.undo();
    e.window.undo();
    CHECK(cell(4, 3) == 0);
    tool(0);

    // Scatter, with path and stone as random variants, at full density: the 3 x 3 cells
    // around the cursor, each one of the two.
    start_painting();
    palette->toggle_variant(2);
    CHECK(e.window.document()->tile_variants() == std::vector<std::int32_t>{1, 2});
    auto* density = palette->findChild<QSpinBox*>(QStringLiteral("scatter_density"));
    REQUIRE(density != nullptr);
    density->setValue(100);
    tool(5);
    CHECK(view->paint_tool() == editor::SceneView::PaintTool::Scatter);
    mouse(view, QEvent::MouseButtonPress, view->from_map(10.5, 6.5), Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, view->from_map(10.5, 6.5), Qt::LeftButton);
    for (std::uint32_t y = 5; y <= 7; ++y) {
        for (std::uint32_t x = 9; x <= 11; ++x) {
            const bool varied = cell(x, y) == 1 || cell(x, y) == 2;
            CHECK(varied);
        }
    }
    CHECK(cell(12, 6) == 0);
    e.window.undo();
    CHECK(cell(10, 6) == 0);
    tool(0);

    // Escape stops painting; so does selecting something else.
    path = palette->findChild<QToolButton*>(QStringLiteral("tile:1"));
    REQUIRE(path != nullptr);
    if (view->painting()) {
        path->click();
    }
    path->click();
    CHECK(view->painting());
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(view, &escape);
    CHECK_FALSE(view->painting());
    CHECK_FALSE(path->isChecked());
    path->click();
    e.window.select(e.guid("hero"));
    CHECK_FALSE(view->painting());
}

TEST_CASE("In the Diorama style: click through the camera, drag along the ground, paint") {
    const Sample sample;
    {
        // The sample, switched to the Diorama style.
        std::vector<engine::serialize::Issue> issues;
        game::ProjectSettings settings =
            game::read_project(sample.path, issues).value_or(game::ProjectSettings{});
        settings.style = game::RenderStyle::Diorama;
        std::ofstream(sample.path / game::project_file, std::ios::binary)
            << engine::serialize::to_text(game::project_to_json(settings));
    }
    editor::MainWindow window(SPARXEL_PLAYER);
    window.resize(1400, 900);
    window.show();
    REQUIRE(window.open_project(sample.path));
    QApplication::processEvents();
    editor::Document* d = window.document();
    editor::SceneView* view = window.scene_view();
    const float aspect =
        static_cast<float>(view->image().width()) / static_cast<float>(view->image().height());
    const scene2d::DioramaCamera camera =
        editor::diorama_camera(d->settings(), view->camera_x(), view->camera_y());
    // Where a map point on the ground shows in the view.
    const auto on_screen = [&](double x, double y) {
        const auto clip = scene2d::diorama_view_projection(camera, aspect)
                              .transform(engine::render::Vec3{static_cast<float>(x), 0.0F,
                                                              static_cast<float>(y)});
        return view->from_ndc(clip[0] / clip[3], clip[1] / clip[3]);
    };
    const engine::world::Entity hero = d->world().find_named("hero").at(0);
    const Guid hero_guid = d->world().guid(hero);
    const auto rect = editor::diorama_sprite_rect(d->world(), d->assets(), hero, camera, aspect)
                          .value_or(std::array<double, 4>{});
    const QPoint middle = view->from_ndc((rect[0] + rect[2]) / 2.0, (rect[1] + rect[3]) / 2.0);
    mouse(view, QEvent::MouseButtonPress, middle, Qt::LeftButton);
    CHECK(window.selection() == hero_guid);
    mouse(view, QEvent::MouseButtonRelease, middle, Qt::LeftButton);
    // Dragging by the feet (just above them, on the billboard): the ground under the cursor
    // goes where the cursor goes, from (10, 9) to (12, 7), 2 tiles east and 2 north.
    const QPoint up(0, -2);
    const QPoint feet = on_screen(10.0, 9.0) + up;
    const QPoint to = on_screen(12.0, 7.0) + up;
    mouse(view, QEvent::MouseButtonPress, feet, Qt::LeftButton);
    CHECK(window.selection() == hero_guid);
    mouse(view, QEvent::MouseMove, (feet + to) / 2, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, to, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, to, Qt::LeftButton);
    const auto* t = d->world().get<scene2d::Transform2D>(d->world().find(hero_guid));
    CHECK(std::abs(t->x - 12.0) < 0.07); // to the nearest picture pixel (1/16 tile)
    CHECK(std::abs(t->y - 7.0) < 0.07);
    CHECK(d->undo_label() == "Move hero");
    window.undo();
    t = d->world().get<scene2d::Transform2D>(d->world().find(hero_guid));
    CHECK(t->x == 10.0);
    CHECK(t->y == 9.0);

    // The move handles lie along the ground, from where the hero stands: the arrow across
    // points the way the map's x goes on screen. Dragged by it, the hero moves across only.
    REQUIRE(view->handles().has_value());
    const editor::SceneView::Handles handles =
        view->handles().value_or(editor::SceneView::Handles{});
    CHECK(handles.origin == on_screen(10.0, 9.0));
    const QPoint across_way = on_screen(11.0, 9.0) - on_screen(10.0, 9.0);
    const QPoint head_way = handles.across.center() - handles.origin;
    CHECK(QPoint::dotProduct(across_way, head_way) > 0); // the same way
    const QPoint grab = handles.across.center();
    const QPoint by = on_screen(12.0, 11.0) - on_screen(10.0, 9.0); // two across, two down
    mouse(view, QEvent::MouseButtonPress, grab, Qt::LeftButton);
    for (int i = 1; i <= 4; ++i) {
        mouse(view, QEvent::MouseMove, grab + (by * i) / 4, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, grab + by, Qt::LeftButton);
    t = d->world().get<scene2d::Transform2D>(d->world().find(hero_guid));
    CHECK(t->x > 11.5); // about two across (perspective: not exactly)
    CHECK(t->x < 12.5);
    CHECK(t->y == 9.0); // and not down at all
    window.undo();

    // Turning and scaling around the billboard: the knob above it, dragged a quarter turn
    // round the hero's feet with snapping on, turns it 90 degrees in its own plane; the
    // square at its top-right corner, dragged twice as far from its feet, doubles it.
    REQUIRE(view->handles().has_value());
    const editor::SceneView::Handles shaping =
        view->handles().value_or(editor::SceneView::Handles{});
    REQUIRE_FALSE(shaping.turn.isNull());
    REQUIRE_FALSE(shaping.stretch.isNull());
    CHECK(shaping.turn.center().y() < view->from_ndc((rect[0] + rect[2]) / 2.0, rect[3]).y());
    view->set_snap(true);
    const QPointF base(shaping.origin);
    const QPointF up_to_knob = QPointF(shaping.turn.center()) - base;
    const double radius = std::hypot(up_to_knob.x(), up_to_knob.y());
    mouse(view, QEvent::MouseButtonPress, shaping.turn.center(), Qt::LeftButton);
    QPoint round_to;
    for (int i = 1; i <= 6; ++i) {
        const double angle = (3.14159265358979 / 2.0) * i / 6.0;
        round_to = (base + QPointF(std::sin(angle), -std::cos(angle)) * radius).toPoint();
        mouse(view, QEvent::MouseMove, round_to, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, round_to, Qt::LeftButton);
    const auto sprite = [&] {
        return *d->world().get<scene2d::Sprite>(d->world().find(hero_guid));
    };
    CHECK(sprite().rotation == 90.0);
    CHECK(d->undo_label() == "Turn hero");
    REQUIRE(view->handles().has_value());
    const QPoint corner = view->handles().value_or(editor::SceneView::Handles{}).stretch.center();
    const QPoint far = shaping.origin + ((corner - shaping.origin) * 2);
    mouse(view, QEvent::MouseButtonPress, corner, Qt::LeftButton);
    for (int i = 1; i <= 4; ++i) {
        mouse(view, QEvent::MouseMove, corner + ((far - corner) * i) / 4, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, far, Qt::LeftButton);
    CHECK(sprite().scale_x == 2.0);
    CHECK(sprite().scale_y == 2.0);
    window.undo();
    window.undo();
    CHECK(sprite().rotation == 0.0);
    CHECK(sprite().scale_x == 1.0);
    view->set_snap(false);

    // Painting a tile on the ground through the camera.
    window.select(d->world().guid(d->world().find_named("ground").at(0)));
    auto* path = window.tile_palette()->findChild<QToolButton*>(QStringLiteral("tile:1"));
    REQUIRE(path != nullptr);
    path->click();
    const QPoint cell = on_screen(5.5, 6.5);
    mouse(view, QEvent::MouseButtonPress, cell, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, cell, Qt::LeftButton);
    std::string error;
    const scene2d::TileGrid* grid =
        d->world().get<scene2d::Tilemap>(d->world().find_named("ground").at(0))->grid(error);
    REQUIRE(grid != nullptr);
    CHECK(grid->at(5, 6) == 1);
    CHECK(grid->at(4, 6) == 0);
}

TEST_CASE("Pictures are dragged from the assets into the scene") {
    Editor e;
    auto* tree = e.window.findChild<QTreeView*>(QStringLiteral("assets"));
    REQUIRE(tree != nullptr);
    auto* model = qobject_cast<QFileSystemModel*>(tree->model());
    REQUIRE(model != nullptr);
    CHECK(tree->dragEnabled());
    const auto drag_data = [&](const fs::path& file) {
        const QModelIndex index = model->index(QString::fromStdU16String(file.u16string()));
        REQUIRE(index.isValid());
        CHECK(model->flags(index).testFlag(Qt::ItemIsDragEnabled));
        std::unique_ptr<QMimeData> data(model->mimeData({index}));
        REQUIRE(data != nullptr);
        REQUIRE(data->hasUrls());
        return data;
    };
    editor::SceneView* view = e.window.scene_view();
    const auto drop = [&](const QMimeData& data, QPoint at) {
        QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &enter);
        CHECK(enter.isAccepted());
        QDropEvent dropped(QPointF(at), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &dropped);
        QApplication::processEvents();
    };

    // The elder's picture, dropped at map point (6.5, 8): a new "elder" there, selected
    // and listed, as one undo step.
    QTreeWidget* list = e.window.outliner();
    const int listed = list->topLevelItemCount();
    const std::unique_ptr<QMimeData> elder = drag_data(e.sample.path / "assets/art/elder.png");
    drop(*elder, view->from_map(6.5, 8.0));
    const Guid placed = e.window.selection();
    REQUIRE(placed.is_valid());
    const engine::world::World& world = e.window.document()->world();
    const engine::world::Entity entity = world.find(placed);
    CHECK(world.name(entity) == "elder");
    CHECK(world.get<scene2d::Transform2D>(entity)->x == doctest::Approx(6.5).epsilon(1.0 / 16));
    CHECK(world.get<scene2d::Transform2D>(entity)->y == doctest::Approx(8.0).epsilon(1.0 / 16));
    CHECK(world.get<scene2d::Sprite>(entity)->image ==
          e.window.document()->assets().find("art/elder.png")->guid.to_string());
    CHECK(list->topLevelItemCount() == listed + 1);
    CHECK(e.window.document()->undo_label() == "Place elder");
    REQUIRE(e.window.document()->undo());

    // A sound dropped there becomes a Sound Source, selected; the inspector lists the
    // project's sounds for it, with this one chosen.
    const std::unique_ptr<QMimeData> chime = drag_data(e.sample.path / "assets/sfx/chime.wav");
    drop(*chime, view->from_map(6.5, 8.0));
    CHECK(e.window.document()->undo_label() == "Place chime");
    const engine::world::Entity hum = world.find(e.window.selection());
    REQUIRE(hum.is_valid());
    const auto* source = world.get<engine::game::SoundSource>(hum);
    REQUIRE(source != nullptr);
    const std::string chime_guid =
        e.window.document()->assets().find("sfx/chime.wav")->guid.to_string();
    CHECK(source->sound == chime_guid);
    bool listed_sound = false;
    for (const QComboBox* box : e.window.findChildren<QComboBox*>()) {
        if (box->findText(QStringLiteral("sfx/chime.wav")) >= 0) {
            listed_sound = true;
            CHECK(box->currentText() == QStringLiteral("sfx/chime.wav"));
            CHECK(box->findText(QStringLiteral("art/hero.png")) < 0); // sounds only
        }
    }
    CHECK(listed_sound);
    REQUIRE(e.window.document()->undo());

    // A scene dropped there becomes a door to it: selected, and its Rule and its door
    // back's (made in the road), in a new logic/doors.drules, shown in the Logic view. One
    // undo takes it all away.
    const std::unique_ptr<QMimeData> road = drag_data(e.sample.path / "assets/maps/road.dscene");
    drop(*road, view->from_map(6.5, 8.0));
    CHECK(e.window.document()->undo_label() == "Place a door to road");
    CHECK(e.window.selection() == e.guid("door_to_road"));
    const auto shows_doors = [&] {
        QTreeWidget* logic = e.window.logic_view();
        for (int i = 0; i < logic->topLevelItemCount(); ++i) {
            if (logic->topLevelItem(i)->text(0) == QStringLiteral("logic/doors.drules")) {
                return logic->topLevelItem(i)->childCount() == 2;
            }
        }
        return false;
    };
    CHECK(shows_doors());
    e.window.undo();
    CHECK_FALSE(shows_doors());

    // Other kinds of file are not placed; the status bar says why.
    const std::unique_ptr<QMimeData> rules = drag_data(e.sample.path / "assets/logic/main.drules");
    drop(*rules, view->from_map(6.5, 8.0));
    CHECK(e.window.statusBar()->currentMessage().startsWith(
        QStringLiteral("Only pictures (PNG) and sounds")));
    CHECK(e.window.document()->undo_label() != "Place main");

    // Nor is a file from outside the assets.
    QMimeData outside;
    outside.setUrls({QUrl::fromLocalFile(
        QString::fromStdU16String((e.sample.path / "project.sparxel.json").u16string()))});
    drop(outside, view->from_map(6.5, 8.0));
    CHECK(e.window.statusBar()->currentMessage() ==
          QStringLiteral("Only files from the project's assets can be placed."));
}

TEST_CASE("The Logic view edits logic: values typed in place, steps added, moved, deleted") {
    Editor e;
    editor::LogicPanel* panel = e.window.logic_panel();
    QTreeWidget* tree = e.window.logic_view();
    const auto item = [&](const QString& text) -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
            if ((*it)->text(0) == text) {
                return *it;
            }
        }
        return nullptr;
    };
    const QString welcome =
        QStringLiteral("Say “Elder: Welcome, traveller. The road east leads home.” (dialogue)");
    QTreeWidgetItem* say = item(welcome);
    REQUIRE(say != nullptr);
    auto* problems = panel->findChild<QLabel*>(QStringLiteral("logic_problems"));
    REQUIRE(problems != nullptr);

    // Selecting the step shows its text in a field; typing changes the line as you type.
    tree->setCurrentItem(say);
    QApplication::processEvents();
    CHECK(problems->text() == QStringLiteral("logic/main.drules runs: no problems."));
    auto* text = panel->findChild<QLineEdit*>(QStringLiteral("logic_slot:0"));
    REQUIRE(text != nullptr);
    CHECK(text->text() == QStringLiteral("Elder: Welcome, traveller. The road east leads home."));
    text->setFocus();
    text->selectAll();
    QKeyEvent typed(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("Elder: Hi."));
    QApplication::sendEvent(text, &typed);
    QApplication::processEvents();
    CHECK(say->text(0) == QStringLiteral("Say “Elder: Hi.” (dialogue)"));
    CHECK(e.window.document()->undo_label() == "Change Text in logic/main.drules");
    CHECK(e.window.windowTitle() ==
          QStringLiteral("Hello Sparxel: maps/village.dscene * — Sparxel Editor"));
    // The lines below are buttons' business: only steps can be deleted or moved.
    auto* remove = panel->findChild<QPushButton*>(QStringLiteral("logic_delete"));
    auto* up = panel->findChild<QPushButton*>(QStringLiteral("logic_up"));
    REQUIRE(remove != nullptr);
    REQUIRE(up != nullptr);
    CHECK(remove->isEnabled());
    tree->setCurrentItem(item(QStringLiteral("Brain weather")));
    CHECK(remove->isEnabled()); // any block can go
    tree->setCurrentItem(item(QStringLiteral("logic/main.drules")));
    CHECK_FALSE(remove->isEnabled()); // the file's own line is not a block

    // Adding a step after the greeting, from the Add step menu: a chime, selected.
    tree->setCurrentItem(item(QStringLiteral("Say “Elder: Hi.” (dialogue)")));
    auto* add = panel->findChild<QToolButton*>(QStringLiteral("logic_add"));
    REQUIRE(add != nullptr);
    Q_EMIT add->menu()->aboutToShow(); // fills it, as opening it does
    auto* chime = add->menu()->findChild<QAction*>(QStringLiteral("action:audio/play_sound"));
    REQUIRE(chime != nullptr);
    chime->trigger();
    QApplication::processEvents();
    QTreeWidgetItem* added = tree->currentItem();
    REQUIRE(added != nullptr);
    CHECK(added->text(0) == QStringLiteral("Play sound “” (audio)"));
    QTreeWidgetItem* parent = added->parent();
    REQUIRE(parent != nullptr);
    const int at = parent->indexOfChild(added);
    CHECK(parent->child(at - 1)->text(0) == QStringLiteral("Say “Elder: Hi.” (dialogue)"));
    // Moved up, it stays selected, now before the greeting.
    up->click();
    QApplication::processEvents();
    REQUIRE(tree->currentItem() != nullptr);
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Play sound “” (audio)"));
    CHECK(tree->currentItem()->parent()->child(at)->text(0) ==
          QStringLiteral("Say “Elder: Hi.” (dialogue)"));
    // Deleted.
    remove->click();
    QApplication::processEvents();
    CHECK(item(QStringLiteral("Play sound “” (audio)")) == nullptr);

    // Undo goes back through it all, with the scene's undo, to the elder's words.
    while (e.window.document()->can_undo()) {
        e.window.undo();
    }
    QApplication::processEvents();
    CHECK(item(welcome) != nullptr);
    CHECK(item(QStringLiteral("Say “Elder: Hi.” (dialogue)")) == nullptr);

    // A rename that leaves the elder's Rule naming nobody is caught as you go.
    tree->setCurrentItem(item(welcome));
    REQUIRE(e.window.document()->rename(e.guid("elder"), "sage"));
    panel->rebuild();
    CHECK(problems->text().startsWith(QStringLiteral("logic/main.drules does not run")));
    // The problem is on its line: the elder's Rule (whose event cannot be found now), in
    // red, saying what is wrong; lines without problems stay as they were.
    std::vector<QTreeWidgetItem*> marked;
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        if (!(*it)->toolTip(0).isEmpty()) {
            marked.push_back(*it);
        }
    }
    REQUIRE(marked.size() == 1);
    CHECK(marked[0]->text(0).endsWith(QStringLiteral("(elder)")));
    CHECK(marked[0]->foreground(0).color() == QColor(200, 40, 40));
    CHECK(marked[0]->toolTip(0) == QStringLiteral("There is no object called 'elder'."));
}

TEST_CASE("The Logic view authors logic from its Add menu") {
    Editor e;
    editor::LogicPanel* panel = e.window.logic_panel();
    QTreeWidget* tree = e.window.logic_view();
    auto* add = panel->findChild<QToolButton*>(QStringLiteral("logic_add"));
    REQUIRE(add != nullptr);
    const auto choose = [&](const QString& name) {
        Q_EMIT add->menu()->aboutToShow();
        auto* entry = add->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        QApplication::processEvents();
    };
    const auto current = [&] {
        REQUIRE(tree->currentItem() != nullptr);
        return tree->currentItem()->text(0);
    };

    // A new file, selected; a Rule on the game starting; a step in it; a Sequence; a
    // Brain with a second state and a "Go to" between them.
    choose(QStringLiteral("add_file"));
    CHECK(current() == QStringLiteral("logic/logic_1.drules"));
    choose(QStringLiteral("rule:/game_start"));
    CHECK(current() == QStringLiteral("When the game starts"));
    choose(QStringLiteral("action:/wait"));
    CHECK(current() == QStringLiteral("Wait 1 seconds"));
    choose(QStringLiteral("if:game/has_save"));
    CHECK(current() == QStringLiteral("If slot 1 has a save (game):"));
    choose(QStringLiteral("action:/log")); // inside the If
    CHECK(current() == QStringLiteral("Log \u201c\u201d"));
    CHECK(tree->currentItem()->parent()->text(0) == QStringLiteral("If slot 1 has a save (game):"));
    tree->setCurrentItem(tree->currentItem()->parent()->parent()->parent()); // the file
    choose(QStringLiteral("add_brain"));
    CHECK(current() == QStringLiteral("Brain brain_1"));
    choose(QStringLiteral("add_state"));
    CHECK(current() == QStringLiteral("State state_1"));
    // The state's name is a value: renamed in place.
    auto* name = panel->findChild<QLineEdit*>(QStringLiteral("logic_slot:0"));
    REQUIRE(name != nullptr);
    name->setFocus();
    name->selectAll();
    QKeyEvent typed(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("alert"));
    QApplication::sendEvent(name, &typed);
    QApplication::processEvents();
    CHECK(current() == QStringLiteral("State alert"));
    choose(QStringLiteral("add_transition"));
    CHECK(current() == QStringLiteral("Go to idle after 1 seconds"));
    auto* problems = panel->findChild<QLabel*>(QStringLiteral("logic_problems"));
    REQUIRE(problems != nullptr);
    CHECK(problems->text() == QStringLiteral("logic/logic_1.drules runs: no problems."));

    // Saving writes the new file, which the game then reads.
    REQUIRE(e.window.save());
    CHECK(fs::exists(e.sample.path / "assets/logic/logic_1.drules"));
    CHECK(fs::exists(e.sample.path / "assets/logic/logic_1.drules.meta"));
}

TEST_CASE("The Project view changes the settings, and the scene shows them at once") {
    Editor e;
    editor::ProjectPanel* panel = e.window.project_panel();
    REQUIRE(panel != nullptr);
    auto* style = panel->findChild<QComboBox*>(QStringLiteral("setting:render_style"));
    auto* sun = panel->findChild<QDoubleSpinBox*>(QStringLiteral("setting:sun_angle"));
    auto* exposure = panel->findChild<QDoubleSpinBox*>(QStringLiteral("setting:exposure"));
    REQUIRE(style != nullptr);
    REQUIRE(sun != nullptr);
    REQUIRE(exposure != nullptr);
    CHECK(style->currentIndex() == 0); // the sample is Classic 2D
    const QImage flat = e.window.scene_view()->image();

    // Switching to the Diorama style redraws the scene in 3D.
    style->setCurrentIndex(1);
    Q_EMIT style->activated(1);
    QApplication::processEvents();
    CHECK(e.window.document()->settings().style == game::RenderStyle::Diorama);
    CHECK(e.window.scene_view()->image() != flat);
    CHECK(e.window.windowTitle() ==
          QStringLiteral("Hello Sparxel: maps/village.dscene * — Sparxel Editor"));
    // A number, as it spins.
    sun->setValue(90.0);
    QApplication::processEvents();
    CHECK(e.window.document()->settings().sun_angle == 90.0);
    // Exposure 2 brightens the picture.
    const QImage before = e.window.scene_view()->image();
    exposure->setValue(2.0);
    QApplication::processEvents();
    CHECK(e.window.scene_view()->image() != before);

    // Undo puts the settings and the fields back: the numbers spun one after the other
    // (one step, as nothing ended it between them), then the style (its own step).
    const double original_sun = game::ProjectSettings{}.sun_angle;
    e.window.undo();
    QApplication::processEvents();
    CHECK(e.window.document()->settings().style == game::RenderStyle::Diorama);
    CHECK(e.window.document()->settings().sun_angle == original_sun);
    auto* sun_again = panel->findChild<QDoubleSpinBox*>(QStringLiteral("setting:sun_angle"));
    REQUIRE(sun_again != nullptr);
    CHECK(sun_again->value() == original_sun);
    e.window.undo();
    QApplication::processEvents();
    CHECK(e.window.document()->settings().style == game::RenderStyle::Classic2D);
    auto* again = panel->findChild<QComboBox*>(QStringLiteral("setting:render_style"));
    REQUIRE(again != nullptr);
    CHECK(again->currentIndex() == 0);
    CHECK(e.window.scene_view()->image() == flat);
    // The fade between scenes: its field shows the default and sets the setting.
    auto* fade = panel->findChild<QDoubleSpinBox*>(QStringLiteral("setting:scene_fade"));
    REQUIRE(fade != nullptr);
    CHECK(fade->value() == doctest::Approx(0.4));
    CHECK(fade->maximum() == 3.0);
    fade->setValue(1.0);
    QApplication::processEvents();
    CHECK(e.window.document()->settings().scene_fade == 1.0);
}

TEST_CASE("The Logic view turns conditions around, matches any of them, and adds Otherwise") {
    const Editor e;
    editor::LogicPanel* panel = e.window.logic_panel();
    QTreeWidget* tree = e.window.logic_view();
    auto* add = panel->findChild<QToolButton*>(QStringLiteral("logic_add"));
    auto* turn = panel->findChild<QPushButton*>(QStringLiteral("logic_not"));
    auto* any = panel->findChild<QCheckBox*>(QStringLiteral("logic_any"));
    auto* problems = panel->findChild<QLabel*>(QStringLiteral("logic_problems"));
    REQUIRE(add != nullptr);
    REQUIRE(turn != nullptr);
    REQUIRE(any != nullptr);
    REQUIRE(problems != nullptr);
    const auto choose = [&](const QString& name) {
        Q_EMIT add->menu()->aboutToShow();
        auto* entry = add->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        QApplication::processEvents();
    };
    const auto item = [&](const QString& text) -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
            if ((*it)->text(0) == text) {
                return *it;
            }
        }
        return nullptr;
    };
    choose(QStringLiteral("add_file"));
    choose(QStringLiteral("rule:/game_start"));
    QTreeWidgetItem* rule = tree->currentItem();
    REQUIRE(rule != nullptr);
    CHECK(any->isVisibleTo(panel)); // a Rule: all or any of its conditions
    CHECK_FALSE(turn->isEnabled());
    choose(QStringLiteral("condition:dialogue/is_open"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("If a message is open (dialogue)"));
    CHECK(turn->isEnabled());
    turn->click();
    QApplication::processEvents();
    CHECK(item(QStringLiteral("If not a message is open (dialogue)")) != nullptr);

    // A second condition, then any of them.
    tree->setCurrentItem(item(QStringLiteral("When the game starts")));
    choose(QStringLiteral("condition:game/has_save"));
    tree->setCurrentItem(item(QStringLiteral("When the game starts")));
    CHECK_FALSE(any->isChecked());
    any->click();
    QApplication::processEvents();
    QTreeWidgetItem* group = item(QStringLiteral("If any of these:"));
    REQUIRE(group != nullptr);
    CHECK(group->childCount() == 2);
    CHECK(group->child(0)->text(0) == QStringLiteral("not a message is open (dialogue)"));
    tree->setCurrentItem(item(QStringLiteral("When the game starts")));
    CHECK(any->isChecked());

    // An Otherwise for the Rule, from the menu; a step on the "Otherwise:" line joins it.
    choose(QStringLiteral("otherwise:/wait"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Wait 1 seconds"));
    QTreeWidgetItem* otherwise = tree->currentItem()->parent();
    REQUIRE(otherwise != nullptr);
    CHECK(otherwise->text(0) == QStringLiteral("Otherwise:"));
    tree->setCurrentItem(otherwise);
    choose(QStringLiteral("action:/log"));
    CHECK(tree->currentItem()->parent()->text(0) == QStringLiteral("Otherwise:"));
    CHECK(tree->currentItem()->parent()->childCount() == 2);
    CHECK(problems->text() == QStringLiteral("logic/logic_1.drules runs: no problems."));
}

TEST_CASE("The Logic view groups conditions inside groups and splits them") {
    const Editor e;
    editor::LogicPanel* panel = e.window.logic_panel();
    QTreeWidget* tree = e.window.logic_view();
    auto* add = panel->findChild<QToolButton*>(QStringLiteral("logic_add"));
    auto* group = panel->findChild<QPushButton*>(QStringLiteral("logic_group"));
    auto* any = panel->findChild<QCheckBox*>(QStringLiteral("logic_any"));
    auto* problems = panel->findChild<QLabel*>(QStringLiteral("logic_problems"));
    REQUIRE(add != nullptr);
    REQUIRE(group != nullptr);
    REQUIRE(any != nullptr);
    REQUIRE(problems != nullptr);
    const auto choose = [&](const QString& name) {
        Q_EMIT add->menu()->aboutToShow();
        auto* entry = add->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        QApplication::processEvents();
    };
    const auto item = [&](const QString& text) -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
            if ((*it)->text(0) == text) {
                return *it;
            }
        }
        return nullptr;
    };
    const QString start = QStringLiteral("When the game starts");
    choose(QStringLiteral("add_file"));
    choose(QStringLiteral("rule:/game_start"));
    CHECK_FALSE(group->isEnabled()); // a Rule is not grouped
    choose(QStringLiteral("condition:dialogue/is_open"));
    tree->setCurrentItem(item(start));
    choose(QStringLiteral("condition:game/has_save"));
    tree->setCurrentItem(item(start));
    choose(QStringLiteral("condition:dialogue/is_open"));
    CHECK_FALSE(group->isEnabled()); // the last condition: none after it

    // The first with the second: "any of these", selected, with Split and its own switch.
    tree->setCurrentItem(item(start)->child(0));
    REQUIRE(tree->currentItem()->text(0) == QStringLiteral("If a message is open (dialogue)"));
    CHECK(group->isEnabled());
    CHECK(group->text() == QStringLiteral("Group"));
    group->click();
    QApplication::processEvents();
    QTreeWidgetItem* outer = tree->currentItem();
    REQUIRE(outer != nullptr);
    CHECK(outer->text(0) == QStringLiteral("If any of these:"));
    CHECK(outer->childCount() == 2);
    CHECK(group->text() == QStringLiteral("Split"));
    CHECK(any->isVisibleTo(panel));
    CHECK(any->isChecked());
    CHECK(any->text() == QStringLiteral("Holds when any of its conditions does (not all)"));

    // A condition added with the group selected joins it; two of its three, "all of these".
    choose(QStringLiteral("condition:game/has_save"));
    outer = item(QStringLiteral("If any of these:"));
    REQUIRE(outer != nullptr);
    REQUIRE(outer->childCount() == 3);
    tree->setCurrentItem(outer->child(1));
    group->click();
    QApplication::processEvents();
    QTreeWidgetItem* inner = tree->currentItem();
    REQUIRE(inner != nullptr);
    CHECK(inner->text(0) == QStringLiteral("All of these:"));
    CHECK(inner->parent() == item(QStringLiteral("If any of these:")));
    CHECK(inner->childCount() == 2);
    CHECK_FALSE(any->isChecked());
    // Matched the other way, with its switch.
    any->click();
    QApplication::processEvents();
    inner = item(QStringLiteral("Any of these:"));
    REQUIRE(inner != nullptr);
    CHECK(problems->text() == QStringLiteral("logic/logic_1.drules runs: no problems."));
    // Split: its two take its place.
    tree->setCurrentItem(inner);
    CHECK(group->text() == QStringLiteral("Split"));
    group->click();
    QApplication::processEvents();
    CHECK(item(QStringLiteral("Any of these:")) == nullptr);
    CHECK(item(QStringLiteral("If any of these:"))->childCount() == 3);
    REQUIRE(tree->currentItem() != nullptr); // the first of its conditions, where it was
    CHECK(tree->currentItem() == item(QStringLiteral("If any of these:"))->child(1));
    CHECK(problems->text() == QStringLiteral("logic/logic_1.drules runs: no problems."));
}

TEST_CASE("The Logic view makes variables, compares them and sets them") {
    const Editor e;
    editor::LogicPanel* panel = e.window.logic_panel();
    QTreeWidget* tree = e.window.logic_view();
    auto* add = panel->findChild<QToolButton*>(QStringLiteral("logic_add"));
    REQUIRE(add != nullptr);
    const auto choose = [&](const QString& name) {
        Q_EMIT add->menu()->aboutToShow();
        auto* entry = add->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        QApplication::processEvents();
    };
    const auto settle = [] {
        for (int i = 0; i < 3; ++i) { // the deferred rebuild
            QApplication::processEvents();
        }
    };
    choose(QStringLiteral("add_file"));
    choose(QStringLiteral("variable:int"));
    // Renamed in its field; its start set.
    auto* name = panel->findChild<QLineEdit*>(QStringLiteral("variable_name:variable_1"));
    REQUIRE(name != nullptr);
    name->setText(QStringLiteral("talks"));
    Q_EMIT name->editingFinished();
    settle();
    auto* start = panel->findChild<QSpinBox*>(QStringLiteral("variable_start:talks"));
    REQUIRE(start != nullptr);
    start->setValue(2);
    QApplication::processEvents();
    const editor::LogicFile& file = e.window.document()->logic().at(0);
    REQUIRE(file.path == "logic/logic_1.drules");
    REQUIRE(file.asset.variables.size() == 1);
    CHECK(file.asset.variables[0].id == "talks");
    CHECK(file.asset.variables[0].initial ==
          std::optional<engine::reflect::Value>(std::int64_t{2}));

    // A Rule on the game starting, compared with talks, and a step setting it.
    choose(QStringLiteral("rule:/game_start"));
    choose(QStringLiteral("compare:talks"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("If talks = 0"));
    tree->setCurrentItem(tree->currentItem()->parent());
    choose(QStringLiteral("action:/set_variable"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set talks to 0"));
    auto* problems = panel->findChild<QLabel*>(QStringLiteral("logic_problems"));
    REQUIRE(problems != nullptr);
    CHECK(problems->text() == QStringLiteral("logic/logic_1.drules runs: no problems."));

    // Used, it cannot be deleted; the status bar says why.
    auto* remove = panel->findChild<QPushButton*>(QStringLiteral("variable_delete:talks"));
    REQUIRE(remove != nullptr);
    remove->click();
    settle();
    CHECK(e.window.statusBar()->currentMessage() ==
          QStringLiteral("talks is still used: delete the blocks that use it first."));
    CHECK(e.window.document()->logic().at(0).asset.variables.size() == 1);
}

TEST_CASE("The Logic view takes values from the event, variables and sums") {
    const Editor e;
    editor::LogicPanel* panel = e.window.logic_panel();
    QTreeWidget* tree = e.window.logic_view();
    auto* add = panel->findChild<QToolButton*>(QStringLiteral("logic_add"));
    REQUIRE(add != nullptr);
    const auto choose = [&](const QString& name) {
        Q_EMIT add->menu()->aboutToShow();
        auto* entry = add->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        QApplication::processEvents();
    };
    const auto from = [&](const QString& name) {
        auto* button = panel->findChild<QToolButton*>(QStringLiteral("logic_source:0"));
        REQUIRE(button != nullptr);
        auto* entry = button->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        for (int i = 0; i < 3; ++i) { // the deferred rebuild
            QApplication::processEvents();
        }
    };
    choose(QStringLiteral("add_file"));
    choose(QStringLiteral("variable:int"));
    choose(QStringLiteral("variable:text")); // variable_2, naming a thing
    choose(QStringLiteral("rule:dialogue/answered"));
    choose(QStringLiteral("action:/set_variable"));
    REQUIRE(tree->currentItem() != nullptr);
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set variable_1 to 0"));
    // From an object, with something to fill in: the number of a thing, its name a field
    // beside the value, which reads as it is typed.
    from(QStringLiteral("source:0:inventory/count_of"));
    REQUIRE(tree->currentItem() != nullptr);
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to the number of \u201c\u201d (inventory)"));
    auto* item_name = panel->findChild<QLineEdit*>(QStringLiteral("logic_inside:0:0"));
    REQUIRE(item_name != nullptr);
    item_name->setText(QStringLiteral("coin"));
    Q_EMIT item_name->textEdited(item_name->text());
    QApplication::processEvents();
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to the number of \u201ccoin\u201d (inventory)"));
    auto* whole = panel->findChild<QLabel*>(QStringLiteral("logic_slot:0"));
    REQUIRE(whole != nullptr);
    CHECK(whole->text() == QStringLiteral("the number of \u201ccoin\u201d (inventory)"));
    // The thing's name from elsewhere: a text variable, by the button beside its field.
    const auto inside_from = [&](const QString& name) {
        auto* button = panel->findChild<QToolButton*>(QStringLiteral("logic_inside_source:0:0"));
        REQUIRE(button != nullptr);
        auto* entry = button->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        for (int i = 0; i < 3; ++i) { // the deferred rebuild
            QApplication::processEvents();
        }
    };
    inside_from(QStringLiteral("inside_source:0:0:/variable_2"));
    REQUIRE(tree->currentItem() != nullptr);
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to the number of variable_2 (inventory)"));
    CHECK(panel->findChild<QLineEdit*>(QStringLiteral("logic_inside:0:0")) == nullptr);
    auto* named = panel->findChild<QLabel*>(QStringLiteral("logic_inside:0:0"));
    REQUIRE(named != nullptr);
    CHECK(named->text() == QStringLiteral("variable_2"));
    // And written again, empty, to type.
    inside_from(QStringLiteral("inside_source:0:0:/"));
    REQUIRE(tree->currentItem() != nullptr);
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to the number of \u201c\u201d (inventory)"));
    CHECK(panel->findChild<QLineEdit*>(QStringLiteral("logic_inside:0:0")) != nullptr);
    // From the event: the answer picked.
    from(QStringLiteral("source:0:/answer"));
    REQUIRE(tree->currentItem() != nullptr);
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set variable_1 to [answer]"));
    auto* shown = panel->findChild<QLabel*>(QStringLiteral("logic_slot:0"));
    REQUIRE(shown != nullptr);
    CHECK(shown->text() == QStringLiteral("[answer]"));
    // From the variable itself, plus a number: the number is a field again.
    from(QStringLiteral("source:0:/variable_1"));
    const auto source_menu = [&] {
        auto* button = panel->findChild<QToolButton*>(QStringLiteral("logic_source:0"));
        REQUIRE(button != nullptr);
        return button->menu();
    };
    CHECK(source_menu()->findChild<QMenu*>(QStringLiteral("sum_more:0")) == nullptr); // no sum
    from(QStringLiteral("sum:0:add"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set variable_1 to variable_1 + 1"));
    auto* number = panel->findChild<QSpinBox*>(QStringLiteral("logic_slot:0"));
    REQUIRE(number != nullptr);
    number->setValue(3);
    QApplication::processEvents();
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set variable_1 to variable_1 + 3"));
    // Plus the answer instead of a written number; then written again.
    from(QStringLiteral("with:0:/answer"));
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to variable_1 + [answer]"));
    from(QStringLiteral("with:0:/"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set variable_1 to variable_1 + 1"));
    // A third part, then times a number: a field again, beside all that comes before it.
    from(QStringLiteral("more:0:multiply"));
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to (variable_1 + 1) \u00d7 1"));
    number = panel->findChild<QSpinBox*>(QStringLiteral("logic_slot:0"));
    REQUIRE(number != nullptr);
    number->setValue(4);
    QApplication::processEvents();
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to (variable_1 + 1) \u00d7 4"));
    bool beside = false;
    for (const QLabel* label : panel->findChildren<QLabel*>()) {
        beside = beside || label->text() == QStringLiteral("(variable_1 + 1) \u00d7");
    }
    CHECK(beside);
    // The last part in brackets of its own: the field and Sum with now work inside them.
    REQUIRE(source_menu()->findChild<QMenu*>(QStringLiteral("sum_bracket:0")) != nullptr);
    from(QStringLiteral("bracket:0:add"));
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to (variable_1 + 1) × (4 + 1)"));
    number = panel->findChild<QSpinBox*>(QStringLiteral("logic_slot:0"));
    REQUIRE(number != nullptr);
    number->setValue(2);
    QApplication::processEvents();
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to (variable_1 + 1) × (4 + 2)"));
    beside = false;
    for (const QLabel* label : panel->findChildren<QLabel*>()) {
        beside = beside || label->text() == QStringLiteral("(variable_1 + 1) × (4 +");
    }
    CHECK(beside);
    from(QStringLiteral("with:0:/answer"));
    CHECK(tree->currentItem()->text(0) ==
          QStringLiteral("Set variable_1 to (variable_1 + 1) × (4 + [answer])"));
    // Without the last part takes the bracket's away first, then the sum's.
    from(QStringLiteral("sum:0:none"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set variable_1 to (variable_1 + 1) × 4"));
    // Without the last part: back to two.
    QAction* drop = source_menu()->findChild<QAction*>(QStringLiteral("sum:0:none"));
    REQUIRE(drop != nullptr);
    CHECK(drop->text() == QStringLiteral("Without the last part"));
    from(QStringLiteral("sum:0:none"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("Set variable_1 to variable_1 + 1"));
    // Compared: the answer, from the Compare menu, on the Rule.
    tree->setCurrentItem(tree->currentItem()->parent());
    choose(QStringLiteral("compare:answer"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("If [answer] = 0"));
    tree->setCurrentItem(tree->currentItem()->parent());
    choose(QStringLiteral("compare:hero/speed"));
    CHECK(tree->currentItem()->text(0) == QStringLiteral("If speed of hero = 0"));
    auto* problems = panel->findChild<QLabel*>(QStringLiteral("logic_problems"));
    REQUIRE(problems != nullptr);
    CHECK(problems->text() == QStringLiteral("logic/logic_1.drules runs: no problems."));
}

TEST_CASE("The Logic view moves a step by dragging it onto another line") {
    const Editor e;
    editor::LogicPanel* panel = e.window.logic_panel();
    QTreeWidget* tree = e.window.logic_view();
    auto* add = panel->findChild<QToolButton*>(QStringLiteral("logic_add"));
    REQUIRE(add != nullptr);
    const auto choose = [&](const QString& name) {
        Q_EMIT add->menu()->aboutToShow();
        auto* entry = add->menu()->findChild<QAction*>(name);
        REQUIRE(entry != nullptr);
        entry->trigger();
        QApplication::processEvents();
    };
    const auto item = [&](const QString& text) -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
            if ((*it)->text(0) == text) {
                return *it;
            }
        }
        return nullptr;
    };
    choose(QStringLiteral("add_file"));
    choose(QStringLiteral("rule:/game_start"));
    choose(QStringLiteral("action:/repeat"));
    tree->setCurrentItem(item(QStringLiteral("When the game starts")));
    choose(QStringLiteral("action:/log")); // first under the Rule, before the Repeat
    QTreeWidgetItem* log = item(QStringLiteral("Log “”"));
    QTreeWidgetItem* repeat = item(QStringLiteral("Repeat 2 times:"));
    REQUIRE(log != nullptr);
    REQUIRE(repeat != nullptr);
    CHECK(log->parent()->indexOfChild(log) == 0);

    // Dragged onto the Repeat: it goes inside it, and stays selected.
    tree->setCurrentItem(log);
    tree->scrollToItem(repeat);
    const QPoint at = tree->visualItemRect(repeat).center();
    QMimeData lines;
    lines.setData(QStringLiteral("application/x-qabstractitemmodeldatalist"), QByteArray());
    QDragEnterEvent enter(at, Qt::MoveAction, &lines, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &enter);
    CHECK(enter.isAccepted());
    QDropEvent dropped(QPointF(at), Qt::MoveAction, &lines, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &dropped);
    for (int i = 0; i < 3; ++i) { // the deferred move
        QApplication::processEvents();
    }
    QTreeWidgetItem* moved = item(QStringLiteral("Log “”"));
    REQUIRE(moved != nullptr);
    REQUIRE(moved->parent() != nullptr);
    CHECK(moved->parent()->text(0) == QStringLiteral("Repeat 2 times:"));
    CHECK(tree->currentItem() == moved);
    CHECK(e.window.document()->undo_label() == "Move a step");

    // Conditions drag too: the second dropped on the first's new group goes into it.
    const QString start = QStringLiteral("When the game starts");
    tree->setCurrentItem(item(start));
    choose(QStringLiteral("condition:dialogue/is_open"));
    tree->setCurrentItem(item(start));
    choose(QStringLiteral("condition:game/has_save"));
    tree->setCurrentItem(item(start));
    choose(QStringLiteral("condition:game/has_save"));
    auto* group = panel->findChild<QPushButton*>(QStringLiteral("logic_group"));
    REQUIRE(group != nullptr);
    tree->setCurrentItem(item(start)->child(0));
    group->click();
    QApplication::processEvents();
    QTreeWidgetItem* any = item(QStringLiteral("If any of these:"));
    REQUIRE(any != nullptr);
    REQUIRE(any->childCount() == 2);
    QTreeWidgetItem* loose = item(QStringLiteral("If slot 1 has a save (game)"));
    REQUIRE(loose != nullptr);
    tree->setCurrentItem(loose);
    tree->scrollToItem(any);
    const QPoint onto = tree->visualItemRect(any).center();
    QDragEnterEvent entered(onto, Qt::MoveAction, &lines, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &entered);
    QDropEvent grouped(QPointF(onto), Qt::MoveAction, &lines, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(tree->viewport(), &grouped);
    for (int i = 0; i < 3; ++i) {
        QApplication::processEvents();
    }
    any = item(QStringLiteral("If any of these:"));
    REQUIRE(any != nullptr);
    CHECK(any->childCount() == 3);
    CHECK(tree->currentItem() == any->child(0)); // first in the group, still selected
    CHECK(e.window.document()->undo_label() == "Move a condition");
}

TEST_CASE("Unsaved work is kept, and restored in a new window after a crash") {
    Editor e;
    const Guid hero = e.guid("hero");
    REQUIRE(e.window.document()->move_entity(hero, 7.0, 2.0));
    e.window.document()->end_merge();
    REQUIRE(e.window.autosave());
    CHECK(fs::exists(e.sample.path / editor::recovery_file));
    // A second window, as after the editor was closed by a crash: the work comes back.
    editor::MainWindow again(SPARXEL_PLAYER);
    REQUIRE(again.open_project(e.sample.path));
    const auto x = [&again, hero] {
        const auto& world = again.document()->world();
        return world.get<scene2d::Transform2D>(world.find(hero))->x;
    };
    CHECK(x() != 7.0);
    REQUIRE(again.restore_recovery());
    CHECK(x() == 7.0);
    CHECK(again.windowTitle().endsWith(QStringLiteral(" * — Sparxel Editor")));
    REQUIRE(again.save());
    CHECK_FALSE(fs::exists(e.sample.path / editor::recovery_file));
}

TEST_CASE("The start page offers new, open and recent projects, and comes back on closing") {
    const Sample sample;
    const fs::path ini =
        fs::temp_directory_path() / ("sparxel-recent-" + Guid::generate().to_string() + ".ini");
    {
        QSettings settings(QString::fromStdString(ini.string()), QSettings::IniFormat);
        editor::MainWindow window(SPARXEL_PLAYER);
        window.set_settings(&settings);
        window.set_sample(sample.path);
        window.show();
        CHECK(window.starting());
        CHECK(window.windowTitle() == QStringLiteral("Sparxel Editor"));
        editor::StartPage* start = window.start_page();
        CHECK(start->sample_button()->isVisibleTo(start));
        CHECK(start->recent_list()->count() == 1); // what to do first
        CHECK_FALSE(start->recent_list()->item(0)->flags().testFlag(Qt::ItemIsEnabled));
        // Trying the sample opens it, and it becomes a recent project.
        start->sample_button()->click();
        REQUIRE(window.document() != nullptr);
        CHECK_FALSE(window.starting());
        REQUIRE(window.recent_projects().size() == 1);
        // Closing goes back to the start page, which lists it by its game's name.
        REQUIRE(window.close_project());
        CHECK(window.starting());
        CHECK(window.document() == nullptr);
        CHECK(window.windowTitle() == QStringLiteral("Sparxel Editor"));
        REQUIRE(start->recent_list()->count() == 1);
        CHECK(start->recent_list()->item(0)->text().startsWith(QStringLiteral("Hello Sparxel\n")));
        // Opening it from the list.
        Q_EMIT start->recent_list()->itemActivated(start->recent_list()->item(0));
        CHECK(window.document() != nullptr);
        REQUIRE(window.close_project());
    }
    {
        // Kept for next time; a project that is gone is listed as missing, and can be left off.
        QSettings settings(QString::fromStdString(ini.string()), QSettings::IniFormat);
        settings.setValue(QStringLiteral("recent/projects"),
                          settings.value(QStringLiteral("recent/projects")).toStringList()
                              << QStringLiteral("C:/no/such/project"));
        editor::MainWindow window(SPARXEL_PLAYER);
        window.set_settings(&settings);
        editor::StartPage* start = window.start_page();
        REQUIRE(start->recent_list()->count() == 2);
        CHECK(
            start->recent_list()->item(1)->text().startsWith(QStringLiteral("project (missing)")));
        window.forget_recent(QStringLiteral("C:/no/such/project"));
        CHECK(start->recent_list()->count() == 1);
        CHECK(settings.value(QStringLiteral("recent/projects")).toStringList().size() == 1);
    }
    std::error_code ignored;
    fs::remove(ini, ignored);
}

TEST_CASE("Search everything scores letters in order, and finds commands, assets and entities") {
    CHECK(editor::search_score(QStringLiteral("vlg"), QStringLiteral("maps/village.dscene")) > 0);
    CHECK(editor::search_score(QStringLiteral("xyz"), QStringLiteral("village")) < 0);
    CHECK(editor::search_score(QStringLiteral("gl"), QStringLiteral("village")) < 0); // in order
    CHECK(editor::search_score(QString(), QStringLiteral("anything")) == 0);
    // Starts of the text and of words beat letters inside them.
    CHECK(editor::search_score(QStringLiteral("play"), QStringLiteral("Game ▸ Play")) >
          editor::search_score(QStringLiteral("play"), QStringLiteral("Display the player")));
    CHECK(editor::search_score(QStringLiteral("hero"), QStringLiteral("hero")) >
          editor::search_score(QStringLiteral("hero"), QStringLiteral("art/hero.png")));

    Editor e;
    e.window.show_search();
    editor::SearchBox* box = e.window.search_box();
    REQUIRE(box != nullptr);
    CHECK(box->isVisible());
    CHECK(box->list()->count() > 10);
    // An entity: choosing it selects it.
    box->field()->setText(QStringLiteral("elder"));
    REQUIRE(box->list()->count() >= 1);
    CHECK(box->list()->item(0)->text().startsWith(QStringLiteral("elder")));
    REQUIRE(box->run_current());
    CHECK_FALSE(box->isVisible());
    CHECK(e.window.selection() == e.guid("elder"));
    // A scene: choosing it opens it.
    e.window.show_search();
    box->field()->setText(QStringLiteral("road dsc"));
    REQUIRE(box->list()->count() >= 1);
    CHECK(box->list()->item(0)->text().startsWith(QStringLiteral("maps/road.dscene")));
    REQUIRE(box->run_current());
    CHECK(e.window.document()->scene_path() == "maps/road.dscene");
    // A command, by its menu: New entity.
    e.window.show_search();
    box->field()->setText(QStringLiteral("new ent"));
    REQUIRE(box->list()->count() >= 1);
    CHECK(box->list()->item(0)->text().startsWith(QStringLiteral("Edit ▸ New entity")));
    const std::size_t before = e.window.document()->entities().size();
    REQUIRE(box->run_current());
    CHECK(e.window.document()->entities().size() == before + 1);
    // Nothing found: nothing runs.
    e.window.show_search();
    box->field()->setText(QStringLiteral("qqqqzz"));
    CHECK(box->list()->count() == 0);
    CHECK_FALSE(box->run_current());
}

TEST_CASE("Check the project lists what Project Doctor finds, and goes to it") {
    Editor e;
    e.window.check_project();
    editor::DoctorPanel* doctor = e.window.doctor();
    REQUIRE(doctor != nullptr);
    const bool nothing_wrong = doctor->summary().startsWith(QStringLiteral("All good")) ||
                               doctor->summary().startsWith(QStringLiteral("0 problem"));
    CHECK(nothing_wrong);
    // A picture gone from the elder: listed first, and double-clicking selects the elder.
    const Guid elder = e.guid("elder");
    e.window.document()
        ->world()
        .get<scene2d::Sprite>(e.window.document()->world().find(elder))
        ->image = Guid::generate().to_string();
    e.window.check_project();
    REQUIRE(doctor->list()->topLevelItemCount() >= 1);
    CHECK(doctor->list()->topLevelItem(0)->text(0) == QStringLiteral("Problem"));
    Q_EMIT doctor->list()->itemActivated(doctor->list()->topLevelItem(0), 0);
    CHECK(e.window.selection() == elder);
    CHECK(e.window.statusBar()->currentMessage().startsWith(QStringLiteral("Doctor: 1 problem")));
}

TEST_CASE("Look presets: hovering shows one in the scene, clicking takes it as one undo step") {
    Editor e;
    const QImage before = e.window.scene_view()->image();
    const float own = e.window.document()->settings().look.saturation;
    editor::ProjectPanel* panel = e.window.project_panel();
    auto* noir = panel->findChild<QPushButton*>(QStringLiteral("look_preset:6"));
    REQUIRE(noir != nullptr);
    CHECK(noir->text() == QStringLiteral("Noir"));
    // Hovering: the scene shows it, the project is unchanged.
    QEvent enter(QEvent::Enter);
    QApplication::sendEvent(noir, &enter);
    const QImage hovered = e.window.scene_view()->image();
    CHECK(hovered != before);
    CHECK(e.window.document()->settings().look.saturation == own);
    // Noir is grey: red, green and blue alike everywhere.
    bool grey = true;
    for (int y = 0; y < hovered.height(); y += 7) {
        for (int x = 0; x < hovered.width(); x += 7) {
            const QColor c = hovered.pixelColor(x, y);
            grey =
                grey && std::abs(c.red() - c.green()) <= 2 && std::abs(c.green() - c.blue()) <= 2;
        }
    }
    CHECK(grey);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(noir, &leave);
    CHECK(e.window.scene_view()->image() == before);
    // Clicking: taken, one undo step.
    noir->click();
    QApplication::processEvents();
    CHECK(e.window.document()->settings().look.saturation == 0.0F);
    CHECK(e.window.document()->can_undo());
    e.window.undo();
    CHECK(e.window.document()->settings().look.saturation == own);
}

TEST_CASE("Tutorials tick off each step as it is done in the editor") {
    Editor e;
    editor::TutorialPanel* panel = e.window.tutorials();
    REQUIRE(panel != nullptr);
    panel->start(0); // Your first game
    // A project is open: the first step is done already.
    CHECK(panel->step() == 1);
    e.window.select(e.guid("hero"));
    panel->check();
    CHECK(panel->step() == 2);
    const reflect::TypeInfo* mover =
        e.window.document()->registry().find_type<engine::game::TopDownMover>();
    REQUIRE(mover != nullptr);
    const reflect::PropertyInfo* speed = nullptr;
    for (const reflect::PropertyInfo& p : mover->properties) {
        speed = p.id == "speed" ? &p : speed;
    }
    REQUIRE(speed != nullptr);
    std::string error;
    REQUIRE(e.window.document()->set_property(e.guid("hero"), *mover, *speed, reflect::Value{7.0},
                                              error));
    panel->check();
    CHECK(panel->step() == 3);
    REQUIRE(e.window.document()->paint_tile(e.guid("ground"), 3.5, 2.5, 1));
    panel->check();
    CHECK(panel->step() == 4);
    REQUIRE(e.window.save());
    panel->check();
    CHECK(panel->step() == 5); // Play, which this test does not start
    CHECK_FALSE(panel->finished());
    panel->skip();
    CHECK(panel->finished());
    // Steps are listed, with the done ones ticked.
    REQUIRE(panel->step_list()->count() == 6);
    CHECK(panel->step_list()->item(0)->text().startsWith(QStringLiteral("✓")));
}

TEST_CASE("An Aseprite file saved again is imported again when the editor reloads assets") {
    Editor e;
    const fs::path file = e.sample.path / "assets/art/blob.aseprite";
    const auto write = [&file](std::array<std::uint8_t, 4> colour) {
        const std::vector<std::uint8_t> bytes = test_support::aseprite(2, 2, {colour});
        std::ofstream(file, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), // NOLINT
                   static_cast<std::streamsize>(bytes.size()));
    };
    const auto first_red = [&e] {
        const engine::assets::AssetRecord* png = e.window.document()->assets().find("art/blob.png");
        REQUIRE(png != nullptr);
        const std::optional<std::vector<std::uint8_t>> bytes =
            e.window.document()->assets().read(png->guid);
        REQUIRE(bytes.has_value());
        std::string error;
        const std::optional<engine::assets::Image> image =
            engine::assets::decode_png(bytes.value_or(std::vector<std::uint8_t>{}), error);
        REQUIRE(image.has_value());
        return image ? int{image->rgba[0]} : -1;
    };
    write({255, 0, 0, 255});
    e.window.reload_assets(); // new: imported
    CHECK(first_red() == 255);
    write({0, 255, 0, 255}); // saved again in Aseprite
    e.window.reload_assets();
    CHECK(first_red() == 0);
}

TEST_CASE("The Animation's Tag offers the tags of the sprite's sheet") {
    const Sample sample;
    {
        scene2d::SheetInfo sheet;
        sheet.frame_width = 1;
        sheet.frame_height = 1;
        sheet.durations_ms = {100, 100};
        sheet.tags = {{"walk", 0, 1, scene2d::TagDirection::Forward},
                      {"idle", 0, 0, scene2d::TagDirection::Forward}};
        std::ofstream(sample.path / "assets/art/hero.sheet", std::ios::binary)
            << scene2d::sheet_to_text(sheet);
    }
    editor::MainWindow window(SPARXEL_PLAYER);
    window.show();
    REQUIRE(window.open_project(sample.path));
    const auto heroes = window.document()->world().find_named("hero");
    REQUIRE(heroes.size() == 1);
    const engine::Guid hero = window.document()->world().guid(heroes[0]);
    const reflect::TypeInfo* animation =
        window.document()->registry().find_type<scene2d::SpriteAnimation>();
    REQUIRE(animation != nullptr);
    REQUIRE(window.document()->add_component(hero, *animation));
    window.select(hero);
    QApplication::processEvents();
    auto* tag =
        window.inspector()->findChild<QComboBox*>(QStringLiteral("property:sprite_animation/tag"));
    REQUIRE(tag != nullptr);
    CHECK(tag->findText(QStringLiteral("walk")) > 0);
    CHECK(tag->findText(QStringLiteral("idle")) > 0);
    tag->setCurrentIndex(tag->findText(QStringLiteral("walk")));
    Q_EMIT tag->activated(tag->currentIndex());
    CHECK(window.document()->world().get<scene2d::SpriteAnimation>(heroes[0])->tag == "walk");
}

TEST_CASE("The scene view shows Screen Text where the game will") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-hud-" + engine::Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "HUD", error, editor::ProjectTemplate::Rpg));
    {
        editor::MainWindow window(SPARXEL_PLAYER);
        window.resize(1400, 900);
        window.show();
        REQUIRE(window.open_project(folder));
        QApplication::processEvents();
        const QImage shot = window.scene_view()->grab().toImage();
        if (const QString path = qEnvironmentVariable("SPARXEL_HUD_SCREENSHOT"); !path.isEmpty()) {
            shot.save(path);
        }
        // Words in white with a shadow: the template's "Gold {gold}" and "HP ..." labels.
        int white = 0;
        for (int y = 0; y < shot.height(); ++y) {
            for (int x = 0; x < shot.width(); ++x) {
                // Near white: fonts differ between systems (the bars' frames are greyer).
                const QColor c = shot.pixelColor(x, y);
                white += std::min({c.red(), c.green(), c.blue()}) >= 240 ? 1 : 0;
            }
        }
        CHECK(white > 20);
        // The gold's panel, a Screen Picture: its light rim and dark middle, top right.
        int rim = 0;
        int middle = 0;
        for (int y = 0; y < shot.height() / 3; ++y) {
            for (int x = shot.width() / 2; x < shot.width(); ++x) {
                const QColor c = shot.pixelColor(x, y);
                rim += c == QColor(230, 232, 240) ? 1 : 0;
                middle += c.blue() > c.red() + 15 && c.red() < 40 ? 1 : 0;
            }
        }
        CHECK(rim > 40);
        CHECK(middle > 100);
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

TEST_CASE("File > Snapshots: one taken with a picture of the scene, listed, and gone back to") {
    Editor e;
    REQUIRE(e.window.take_snapshot(QStringLiteral("before the cave")));
    std::vector<editor::Snapshot> all = editor::list_snapshots(e.sample.path);
    REQUIRE(all.size() == 1);
    CHECK(all[0].name == "before the cave");
    REQUIRE_FALSE(all[0].picture.empty());
    CHECK_FALSE(QImage(QString::fromStdU16String(all[0].picture.u16string())).isNull());

    // The dialog lists it with its picture.
    int listed = -1;
    bool pictured = false;
    QTimer::singleShot(0, [&] {
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (top->objectName() == QStringLiteral("snapshots_dialog")) {
                const auto* list = top->findChild<QListWidget*>(QStringLiteral("snapshot_list"));
                listed = list->count();
                pictured = listed > 0 && !list->item(0)->icon().isNull();
                qobject_cast<QDialog*>(top)->reject();
            }
        }
    });
    e.window.show_snapshots();
    CHECK(listed == 1);
    CHECK(pictured);

    // Something made after it goes when it is gone back to; the project opens again.
    std::ofstream(e.sample.path / "maps" / "extra.dscene") << "{}";
    REQUIRE(e.window.restore_snapshot(QString::fromStdString(all[0].id)));
    CHECK_FALSE(fs::exists(e.sample.path / "maps" / "extra.dscene"));
    REQUIRE(e.window.document() != nullptr);
    CHECK(e.window.outliner()->topLevelItemCount() == 7);
    CHECK(editor::list_snapshots(e.sample.path).size() == 2); // with "Before restoring"
}

TEST_CASE("Recolour: the hero's colours turned round the wheel, drawn so, in one undo") {
    Editor e;
    e.window.outliner()->topLevelItem(2)->setSelected(true); // the hero
    QApplication::processEvents();
    const auto sprite = [&] {
        return e.window.document()->world().get<scene2d::Sprite>(
            e.window.document()->world().find(e.guid("hero")));
    };
    REQUIRE(sprite()->recolor.empty());
    const QImage before = e.window.scene_view()->image();
    auto* pick = e.window.inspector()->findChild<QPushButton*>(QStringLiteral("recolor_button"));
    REQUIRE(pick != nullptr);
    std::size_t colours = 0;
    QTimer::singleShot(0, [&] {
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<editor::RecolorDialog*>(top)) {
                colours = dialog->colors().size();
                dialog->set_hue_shift(120);
                if (const QString shot = qEnvironmentVariable("SPARXEL_RECOLOR_SCREENSHOT");
                    !shot.isEmpty()) {
                    dialog->grab().save(shot);
                }
                dialog->accept();
            }
        }
    });
    pick->click();
    QApplication::processEvents();
    CHECK(colours >= 3);
    const std::vector<scene2d::ColorSwap> swaps = scene2d::parse_recolor(sprite()->recolor);
    CHECK(swaps.size() >= 2); // greys have no hue to turn
    CHECK(e.window.scene_view()->image() != before);
    e.window.undo();
    QApplication::processEvents();
    CHECK(sprite()->recolor.empty());
    CHECK(e.window.scene_view()->image() == before);
}

TEST_CASE("The Recolour dialog: picked colours, a reset, and only changes kept") {
    engine::assets::Image picture;
    picture.width = 2;
    picture.height = 1;
    picture.rgba = {200, 40, 40, 255, 128, 128, 128, 255};
    editor::RecolorDialog dialog(picture, "#c82828>#2828c8");
    REQUIRE(dialog.colors().size() == 2);
    CHECK(dialog.target(0) == std::array<std::uint8_t, 3>{40, 40, 200});
    CHECK(dialog.text() == "#c82828>#2828c8");
    dialog.set_target(1, {0, 0, 0});
    CHECK(dialog.text() == "#c82828>#2828c8, #808080>#000000");
    CHECK(dialog.preview().pixelColor(dialog.preview().width() - 1, 0) == QColor(0, 0, 0));
    dialog.reset();
    CHECK(dialog.text().empty());
    dialog.set_hue_shift(240); // red to blue; the grey stays
    CHECK(dialog.text().find("#808080") == std::string::npos);
    CHECK(dialog.target(0)[2] > dialog.target(0)[0]);
}

TEST_CASE("The Assets panel: searched, pictures as thumbnails, usages found, renamed safely") {
    Editor e;
    // A search lists only names with its words.
    CHECK(editor::asset_filters({}).contains(QStringLiteral("*.png")));
    CHECK(editor::asset_filters(QStringLiteral("ro sc")).contains(QStringLiteral("*ro*sc*.png")));
    CHECK(editor::asset_filters(QStringLiteral("a*")).front() == QStringLiteral("*a[*]*.png"));
    e.window.search_assets(QStringLiteral("road"));
    auto* search = e.window.findChild<QLineEdit*>(QStringLiteral("asset_search"));
    REQUIRE(search != nullptr);
    CHECK(search->text() == QStringLiteral("road"));
    if (const QString shot = qEnvironmentVariable("SPARXEL_ASSETS_SCREENSHOT"); !shot.isEmpty()) {
        e.window.search_assets(QStringLiteral("e"));
        for (int i = 0; i < 20; ++i) {
            QApplication::processEvents();
            QThread::msleep(10);
        }
        search->parentWidget()->grab().save(shot);
    }
    e.window.search_assets({});

    // A picture's thumbnail is itself, scaled by whole pixels.
    const editor::AssetThumbnails thumbnails;
    const QString hero =
        QString::fromStdU16String((e.sample.path / "assets" / "art" / "hero.png").u16string());
    const QIcon icon = thumbnails.thumbnail(hero, 32);
    REQUIRE_FALSE(icon.isNull());
    CHECK_FALSE(icon.pixmap(32, 32).toImage().pixelColor(16, 16) == QColor(Qt::transparent));
    CHECK(thumbnails
              .thumbnail(QString::fromStdU16String(
                             (e.sample.path / "assets" / "maps" / "road.dscene").u16string()),
                         32)
              .isNull());

    // Find usages: the hero's picture, in both scenes.
    const std::vector<editor::AssetUse> uses = e.window.show_usages(QStringLiteral("art/hero.png"));
    CHECK(uses.size() == 2);
    auto* dialog = e.window.findChild<QDialog*>(QStringLiteral("usages_dialog"));
    REQUIRE(dialog != nullptr);
    CHECK(dialog->isVisible());
    auto* list = dialog->findChild<QListWidget*>(QStringLiteral("usages_list"));
    REQUIRE(list->count() == 2);
    CHECK(list->item(1)->text().contains(QStringLiteral("hero")));
    // Double-clicking a scene opens it.
    Q_EMIT list->itemDoubleClicked(list->item(0));
    CHECK(e.window.document()->scene_path() == "maps/road.dscene");

    // Renaming the scene being edited: it stays open by its new name, and the village's
    // logic, which goes there, follows.
    REQUIRE(e.window.rename_asset(QStringLiteral("maps/road.dscene"), QStringLiteral("forest")));
    CHECK(e.window.document()->scene_path() == "maps/forest.dscene");
    CHECK(fs::exists(e.sample.path / "assets" / "maps" / "forest.dscene"));
    CHECK_FALSE(editor::find_asset_usages(e.sample.path, "maps/forest.dscene").empty());
    CHECK(editor::find_asset_usages(e.sample.path, "maps/road.dscene").empty());
    CHECK_FALSE(
        e.window.rename_asset(QStringLiteral("maps/forest.dscene"), QStringLiteral("village")));
}

TEST_CASE("On-screen elements: clicked to select, dragged from their Place, in one undo") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-hud-drag-" + engine::Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "HUD", error, editor::ProjectTemplate::Rpg));
    {
        editor::MainWindow window(SPARXEL_PLAYER);
        window.resize(1400, 900);
        window.show();
        REQUIRE(window.open_project(folder));
        QApplication::processEvents();
        editor::SceneView* view = window.scene_view();
        static_cast<void>(view->grab()); // painted, so its elements are where they show
        const auto panel_entity = window.document()->world().find_named("gold_panel").at(0);
        const engine::Guid panel = window.document()->world().guid(panel_entity);
        const auto found =
            std::ranges::find_if(view->hud_items(), [&](const editor::SceneView::HudItem& i) {
                return i.entity == panel;
            });
        REQUIRE(found != view->hud_items().end());
        // A point of the panel's rim, away from the words drawn over it.
        const QPoint on = found->at.toRect().topLeft() + QPoint(2, 2);
        const auto picture = [&] {
            return window.document()->world().get<game::ScreenPicture>(
                window.document()->world().find(panel));
        };
        CHECK(picture()->x == 1.0);
        // Dragged 10 screen pixels left and 6 down: further from the right, and the top.
        const int s = static_cast<int>(std::lround(found->at.width() / 60.0));
        REQUIRE(s >= 1);
        mouse(view, QEvent::MouseButtonPress, on, Qt::LeftButton);
        CHECK(window.selection() == panel);
        mouse(view, QEvent::MouseMove, on + QPoint(-5 * s, 3 * s), Qt::LeftButton);
        mouse(view, QEvent::MouseMove, on + QPoint(-10 * s, 6 * s), Qt::LeftButton);
        mouse(view, QEvent::MouseButtonRelease, on + QPoint(-10 * s, 6 * s), Qt::LeftButton);
        QApplication::processEvents();
        CHECK(picture()->x == 11.0);
        CHECK(picture()->y == 7.0);
        // The inspector shows it, and one undo puts it back.
        auto* x_field = window.inspector()->findChild<QDoubleSpinBox*>(
            QStringLiteral("property:screen_picture/x"));
        REQUIRE(x_field != nullptr);
        CHECK(x_field->value() == 11.0);
        window.undo();
        CHECK(picture()->x == 1.0);
        CHECK(picture()->y == 1.0);
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

TEST_CASE("Screen Layouts in the editor: children put under it, stacked, dragged as one") {
    Editor e;
    editor::Document& d = *e.window.document();
    const reflect::TypeInfo* layout_type = d.registry().find_type<game::ScreenLayout>();
    const reflect::TypeInfo* bar_type = d.registry().find_type<game::ScreenBar>();
    REQUIRE(layout_type != nullptr);
    REQUIRE(bar_type != nullptr);
    const Guid party = d.create_entity("party", 0.0, 0.0);
    REQUIRE(d.add_component(party, *layout_type));
    std::vector<Guid> bars;
    for (const char* name : {"hp_one", "hp_two"}) {
        const Guid bar = d.create_entity(name, 0.0, 0.0);
        REQUIRE(d.add_component(bar, *bar_type));
        REQUIRE(e.window.put_under(bar, party));
        bars.push_back(bar);
    }
    CHECK(d.undo_label() == "Put hp_two under party");
    CHECK_FALSE(e.window.put_under(party, bars[0])); // not under its own child
    e.window.scene_view()->refresh();
    static_cast<void>(e.window.scene_view()->grab());
    // Both bars, one under the other, each clicked as the layout.
    std::vector<QRectF> at;
    for (const editor::SceneView::HudItem& item : e.window.scene_view()->hud_items()) {
        if (item.entity == party) {
            at.push_back(item.at);
        }
    }
    REQUIRE(at.size() == 2);
    CHECK(at[0].left() == at[1].left());
    CHECK(at[1].top() > at[0].bottom());
    // Dragging one moves the layout.
    const QPoint on = at[1].center().toPoint();
    editor::SceneView* view = e.window.scene_view();
    const int s = static_cast<int>(std::lround(at[0].width() / 50.0));
    REQUIRE(s >= 1);
    mouse(view, QEvent::MouseButtonPress, on, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, on + QPoint(10 * s, 0), Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, on + QPoint(10 * s, 0), Qt::LeftButton);
    CHECK(e.window.selection() == party);
    CHECK(d.world().get<game::ScreenLayout>(d.world().find(party))->x == 14.0);
    // Undo, twice: the drag, then the second bar back at the top.
    e.window.undo();
    e.window.undo();
    CHECK_FALSE(d.world().parent(d.world().find(bars[1])).is_valid());
}

TEST_CASE("Tile shapes from the palette: a wall tile made a half, then whole again") {
    Editor e;
    e.window.select(e.guid("walls"));
    QApplication::processEvents();
    editor::TilePalette* palette = e.window.tile_palette();
    const auto shapes = [&] {
        return e.window.document()
            ->world()
            .get<scene2d::Tilemap>(e.window.document()->world().find(e.guid("walls")))
            ->shapes;
    };
    REQUIRE(palette->set_tile_shape(2, scene2d::TileShape::Top));
    CHECK(shapes() == "2 top");
    REQUIRE(palette->set_tile_shape(5, scene2d::TileShape::None));
    CHECK(shapes() == "2 top, 5 none");
    REQUIRE(palette->set_tile_shape(2, scene2d::TileShape::Full));
    CHECK(shapes() == "5 none"); // all of it is left out
    CHECK(e.window.document()->undo_label() == "Change Tile shapes of walls");
    e.window.undo();
    CHECK(shapes() == "2 top, 5 none");
    // Not on a map that is not solid.
    e.window.select(e.guid("ground"));
    QApplication::processEvents();
    CHECK_FALSE(palette->set_tile_shape(1, scene2d::TileShape::Top));
}

TEST_CASE("Edit commands: copy and paste where the camera looks, duplicate, select all, frame") {
    Editor e;
    editor::Document& d = *e.window.document();
    const Guid hero = e.guid("hero");
    e.window.select(hero);
    REQUIRE(e.window.copy_selection());
    CHECK(QGuiApplication::clipboard()->mimeData()->hasFormat(
        QStringLiteral("application/x-sparxel-entities")));
    // Pasted where the camera looks, and selected.
    e.window.scene_view()->set_camera(4.0, 3.0);
    const std::vector<Guid> pasted = e.window.paste();
    REQUIRE(pasted.size() == 1);
    CHECK(e.window.selection() == pasted[0]);
    const auto* at = d.world().get<scene2d::Transform2D>(d.world().find(pasted[0]));
    CHECK(at->x == 4.0);
    CHECK(at->y == 3.0);
    // Duplicate: a copy of what is selected, selected in turn.
    const std::vector<Guid> twins = e.window.duplicate_selection();
    REQUIRE(twins.size() == 1);
    CHECK(e.window.selection() == twins[0]);
    CHECK(d.world().name(d.world().find(twins[0])) == "hero_3");
    // Select all, then F frames them.
    e.window.findChild<QAction*>(QStringLiteral("select_all"))->trigger();
    CHECK(e.window.selected().size() == d.entities().size());
    e.window.select(hero);
    e.window.findChild<QAction*>(QStringLiteral("frame_selection"))->trigger();
    const auto* hero_at = d.world().get<scene2d::Transform2D>(d.world().find(hero));
    CHECK(e.window.scene_view()->camera_x() == hero_at->x);
    CHECK(e.window.scene_view()->camera_y() == hero_at->y);
    // Cut: copied, then gone.
    e.window.select(twins[0]);
    e.window.findChild<QAction*>(QStringLiteral("cut"))->trigger();
    CHECK_FALSE(d.world().find(twins[0]).is_valid());
}
