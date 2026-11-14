// Sprite Studio, driven as a person would on Qt's offscreen platform: a new sprite drawn
// with the mouse and saved, a picture of the sample opened and cut into the frames its
// scene uses, the layers and timeline, and the scene showing what was saved (hot reload).

#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTableWidget>
#include <QTreeWidget>
#include <filesystem>
#include <string>

#include "main_window.h"
#include "project_panel.h"
#include "scene_view.h"
#include "sfx_generator.h"
#include "sprite_studio.h"

namespace fs = std::filesystem;
namespace sprite = editor::sprite;
using engine::Guid;

namespace {

struct Project {
    Project() : window(SPARXEL_PLAYER) {
        path = fs::temp_directory_path() / ("sparxel-studio-app-" + Guid::generate().to_string());
        fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", path, fs::copy_options::recursive);
        window.resize(1400, 900);
        window.show();
        REQUIRE(window.open_project(path));
        QApplication::processEvents();
    }
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;
    Project(Project&&) = delete;
    Project& operator=(Project&&) = delete;
    ~Project() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
    editor::MainWindow window;
};

/// The middle of a pixel of the canvas, in the widget.
QPoint at_pixel(const editor::SpriteCanvas* canvas, int x, int y) {
    const QPointF corner = canvas->point_of(x, y);
    const double half = canvas->zoom() / 2.0;
    return {static_cast<int>(corner.x() + half), static_cast<int>(corner.y() + half)};
}

void mouse(QWidget* widget, QEvent::Type type, QPoint at, Qt::MouseButton button,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, QPointF(at), widget->mapToGlobal(QPointF(at)), button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : button, modifiers);
    QApplication::sendEvent(widget, &event);
}

/// A drag on the canvas between two pixels.
void drag(editor::SpriteCanvas* canvas, QPoint from, QPoint to,
          Qt::MouseButton button = Qt::LeftButton) {
    mouse(canvas, QEvent::MouseButtonPress, at_pixel(canvas, from.x(), from.y()), button);
    mouse(canvas, QEvent::MouseMove, at_pixel(canvas, to.x(), to.y()), button);
    mouse(canvas, QEvent::MouseButtonRelease, at_pixel(canvas, to.x(), to.y()), button);
}

} // namespace

TEST_CASE("File > New sprite makes a sprite, drawn with the mouse and saved with its sheet") {
    Project p;
    REQUIRE(p.window.findChild<QAction*>(QStringLiteral("new_sprite")) != nullptr);
    editor::SpriteStudio* studio = p.window.new_sprite(QStringLiteral("slime"), 8, 8);
    REQUIRE(studio != nullptr);
    studio->resize(1000, 700);
    QApplication::processEvents();
    CHECK(fs::exists(p.path / "assets" / "art" / "slime.dsprite"));
    CHECK(fs::exists(p.path / "assets" / "art" / "slime.png"));
    CHECK(p.window.document()->assets().find("art/slime.png") != nullptr);
    CHECK(studio->windowTitle().startsWith(QStringLiteral("slime.dsprite (8 × 8, 1 frames)")));
    // Names that would leave the art folder, and ones taken, are refused.
    CHECK(p.window.new_sprite(QStringLiteral("../x"), 8, 8) == nullptr);
    CHECK(p.window.new_sprite(QStringLiteral("slime"), 8, 8) == nullptr);

    editor::SpriteCanvas* canvas = studio->canvas();
    canvas->fit();
    REQUIRE(canvas->zoom() > 1);
    // The left button draws with the first colour, along the drag.
    const sprite::Color first = studio->primary();
    drag(canvas, {1, 1}, {4, 1});
    sprite::Editor* model = studio->editor();
    CHECK(model->sprite().pixel(0, 0, 1, 1) == first);
    CHECK(model->sprite().pixel(0, 0, 4, 1) == first);
    CHECK(model->undo_text() == "Draw with the pencil");
    CHECK(studio->windowTitle().contains(QStringLiteral("slime.dsprite *")));
    // The right button, the second colour; Alt-click picks a colour.
    const sprite::Color second = studio->secondary();
    drag(canvas, {6, 6}, {6, 6}, Qt::RightButton);
    CHECK(model->sprite().pixel(0, 0, 6, 6) == second);
    mouse(canvas, QEvent::MouseButtonPress, at_pixel(canvas, 6, 6), Qt::LeftButton,
          Qt::AltModifier);
    CHECK(studio->primary() == second);
    // Tools by their shortcuts' actions.
    studio->set_tool(editor::StudioTool::Fill);
    studio->set_primary({255, 0, 0, 255});
    drag(canvas, {0, 7}, {0, 7});
    CHECK(model->sprite().pixel(0, 0, 3, 5) == sprite::Color{255, 0, 0, 255});
    studio->undo();
    CHECK(model->sprite().pixel(0, 0, 3, 5) == sprite::Color{});

    // Layers and frames show in the timeline: layers down (the top first), frames across.
    std::string error;
    REQUIRE(model->add_layer(error));
    REQUIRE(model->add_frame(false, error));
    studio->refresh();
    QTableWidget* timeline = studio->timeline();
    CHECK(timeline->rowCount() == 2);
    CHECK(timeline->columnCount() == 2);
    CHECK(studio->layers()->topLevelItemCount() == 2);
    CHECK(studio->layers()->topLevelItem(0)->text(2) == QStringLiteral("Layer 2"));
    // Choosing a cell chooses its layer and frame.
    timeline->setCurrentCell(1, 0);
    CHECK(model->layer() == 0);
    CHECK(model->frame() == 0);
    // Hiding a layer by its tick box.
    studio->layers()->topLevelItem(1)->setCheckState(0, Qt::Unchecked);
    QApplication::processEvents();
    CHECK_FALSE(model->sprite().layers[0].visible);
    CHECK(model->undo_text() == "Hide Layer 1");

    REQUIRE(studio->save());
    CHECK_FALSE(model->modified());
    const std::optional<sprite::Sprite> loaded =
        sprite::load(p.path / "assets" / "art" / "slime.dsprite", error);
    REQUIRE(loaded.has_value());
    const sprite::Sprite saved = loaded.value_or(sprite::Sprite{});
    CHECK(saved.layers.size() == 2);
    CHECK(saved.frames.size() == 2);
    CHECK(saved.pixel(0, 0, 1, 1) == first);
    // Opening it again brings the same window forward.
    CHECK(p.window.open_sprite(QStringLiteral("art/slime.dsprite")) == studio);
}

TEST_CASE("A picture of the scene opens in Sprite Studio, and the scene shows it once saved") {
    Project p;
    const QImage before = p.window.scene_view()->image();
    // The hero's picture, cut into the frames its Sprite uses.
    editor::SpriteStudio* studio = p.window.open_sprite(QStringLiteral("art/hero.png"));
    REQUIRE(studio != nullptr);
    sprite::Editor* model = studio->editor();
    CHECK(studio->file().filename() == "hero.dsprite");
    CHECK(model->sprite().width == 16);
    CHECK(model->sprite().frames.size() > 1);
    // Painted over in red, every frame, and saved: the scene draws the new pixels.
    sprite::Brush brush;
    brush.fill_all = true;
    for (std::size_t frame = 0; frame < model->sprite().frames.size(); ++frame) {
        model->set_frame(frame);
        std::string error;
        REQUIRE(model->begin(sprite::Tool::FilledRectangle, {255, 0, 0, 255}, brush, 0, 0, error));
        model->drag(model->sprite().width - 1, model->sprite().height - 1);
        REQUIRE(model->end());
    }
    REQUIRE(studio->save());
    QApplication::processEvents();
    CHECK(fs::exists(p.path / "assets" / "art" / "hero.dsprite"));
    const QImage after = p.window.scene_view()->image();
    REQUIRE(after.size() == before.size());
    CHECK(after != before);
    // Files that are neither pictures nor sprites are refused.
    CHECK(p.window.open_sprite(QStringLiteral("sfx/chime.wav")) == nullptr);
}

TEST_CASE("File > New sound effect makes a sound of a kind and saves it into the project") {
    Project p;
    editor::SfxGenerator* generator = p.window.show_sfx_generator();
    REQUIRE(generator != nullptr);
    const editor::sfx::Params first = generator->params();
    generator->make(editor::sfx::Preset::Explosion);
    CHECK(generator->params().wave == editor::sfx::Wave::Noise);
    CHECK(generator->name_field()->text() == QStringLiteral("explosion"));
    generator->mutate();
    CHECK_FALSE(generator->params().frequency == first.frequency);
    std::string error;
    REQUIRE(generator->save(QStringLiteral("boom"), error));
    CHECK(fs::exists(p.path / "assets" / "sfx" / "boom.wav"));
    CHECK(p.window.document()->assets().find("sfx/boom.wav") != nullptr);
    CHECK_FALSE(generator->save(QStringLiteral("boom"), error));
    CHECK(error == "There is a sound called boom already.");
    CHECK_FALSE(generator->save(QStringLiteral("../x"), error));
    generator->close();
}

TEST_CASE("The style guide: a palette from a picture, shown, and new sprites start with it") {
    Project p;
    editor::ProjectPanel* panel = p.window.project_panel();
    REQUIRE(panel->palette_from_picture(QStringLiteral("art/elder.png")));
    QApplication::processEvents();
    const std::string palette = p.window.document()->settings().palette;
    const std::vector<sprite::Color> colours = sprite::parse_palette(palette);
    CHECK(colours.size() >= 3);
    CHECK(panel->findChild<QLabel*>(QStringLiteral("palette_swatches")) != nullptr);
    CHECK_FALSE(panel->palette_from_picture(QStringLiteral("maps/road.dscene")));
    // One undo puts it back.
    p.window.undo();
    CHECK(p.window.document()->settings().palette.empty());
    panel->palette_from_sprite_studio();
    CHECK(sprite::parse_palette(p.window.document()->settings().palette) ==
          sprite::default_palette());

    // A sprite made now has the project's palette, not Sprite Studio's own.
    p.window.undo();
    REQUIRE(panel->palette_from_picture(QStringLiteral("art/elder.png")));
    REQUIRE(p.window.new_sprite(QStringLiteral("villager"), 8, 8) != nullptr);
    std::string error;
    const std::optional<sprite::Sprite> made =
        sprite::load(p.path / "assets" / "art" / "villager.dsprite", error);
    REQUIRE(made.has_value());
    if (!made) {
        return;
    }
    CHECK(made->palette == colours);

    // The font: chosen from the project's pictures, or the built-in one.
    auto* font = panel->findChild<QComboBox*>(QStringLiteral("setting:font"));
    REQUIRE(font != nullptr);
    CHECK(font->itemText(0) == QStringLiteral("(built-in)"));
    const int elder = font->findData(QStringLiteral("art/elder.png"));
    REQUIRE(elder > 0);
    font->setCurrentIndex(elder);
    Q_EMIT font->activated(elder);
    CHECK(p.window.document()->settings().font == "art/elder.png");
    // The boxes' frame, the same way.
    auto* frame = panel->findChild<QComboBox*>(QStringLiteral("setting:box_frame"));
    REQUIRE(frame != nullptr);
    const int tiles = frame->findData(QStringLiteral("art/tiles.png"));
    REQUIRE(tiles > 0);
    Q_EMIT frame->activated(tiles);
    CHECK(p.window.document()->settings().box_frame == "art/tiles.png");
}
