// The editor application, driven as a person would, on Qt's offscreen platform (no display
// needed): open the sample, select, edit in the inspector, click and drag in the scene,
// undo, add entities and components, save and export.

#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTreeWidget>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "inspector.h"
#include "main_window.h"
#include "scene_view.h"

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
    if (const char* shot = std::getenv("SPARXEL_EDITOR_SCREENSHOT")) {
        e.window.select(e.guid("elder"));
        QApplication::processEvents();
        e.window.grab().save(QString::fromUtf8(shot));
    }
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
