// The editor application, driven as a person would, on Qt's offscreen platform (no display
// needed): open the sample, select, edit in the inspector, click and drag in the scene,
// undo, add entities and components, save and export.

#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileSystemModel>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "inspector.h"
#include "logic_panel.h"
#include "main_window.h"
#include "project_panel.h"
#include "scene_view.h"
#include "tile_palette.h"

namespace fs = std::filesystem;
namespace game = engine::game;
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
    CHECK(e.window.windowTitle() == QStringLiteral("Hello Sparxel — Sparxel Editor"));
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
    // The logic, as plain language: one file; its three Rules and a Brain.
    QTreeWidget* logic = e.window.logic_view();
    REQUIRE(logic->topLevelItemCount() == 1);
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
    CHECK(e.window.windowTitle().startsWith(QStringLiteral("Hello Sparxel *")));
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

    // Clicking where nothing is drawn selects nothing; the ground is under most places.
    mouse(view, QEvent::MouseButtonPress, view->from_map(3.0, 3.0), Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, view->from_map(3.0, 3.0), Qt::LeftButton);
    CHECK(e.window.selection() == e.guid("ground"));
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

    // Escape stops painting; so does selecting something else.
    path = palette->findChild<QToolButton*>(QStringLiteral("tile:1"));
    REQUIRE(path != nullptr);
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
    const Editor e;
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

    // A sound is not placed; the status bar says why.
    const std::unique_ptr<QMimeData> chime = drag_data(e.sample.path / "assets/sfx/chime.wav");
    drop(*chime, view->from_map(6.5, 8.0));
    CHECK(e.window.statusBar()->currentMessage().startsWith(
        QStringLiteral("Only pictures (PNG) can be placed")));
    CHECK(e.window.document()->undo_label() != "Place chime");

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
    CHECK(e.window.windowTitle() == QStringLiteral("Hello Sparxel * — Sparxel Editor"));
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
    CHECK(e.window.windowTitle() == QStringLiteral("Hello Sparxel * — Sparxel Editor"));
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
