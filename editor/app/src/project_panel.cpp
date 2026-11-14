#include "project_panel.h"

#include "engine/assets/assets.h"
#include "engine/game/ui_theme.h"
#include "engine/scene2d/scene2d.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "editor/core/looks.h"
#include "editor/core/sprite.h"

namespace editor {

namespace game = engine::game;
namespace scene2d = engine::scene2d;
using Settings = game::ProjectSettings;

namespace {

QString text_of(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

} // namespace

ProjectPanel::ProjectPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    content_ = new QWidget(this);
    layout->addWidget(content_);
    layout->addStretch(1);
}

void ProjectPanel::set_document(Document* document) {
    document_ = document;
    refresh();
}

bool ProjectPanel::palette_from_picture(const QString& asset) {
    if (document_ == nullptr) {
        return false;
    }
    const engine::assets::AssetRecord* record = document_->assets().find(asset.toStdString());
    const std::optional<std::vector<std::uint8_t>> bytes =
        record != nullptr ? document_->assets().read(record->guid) : std::nullopt;
    std::string error;
    const std::optional<engine::assets::Image> image =
        bytes ? engine::assets::decode_png(*bytes, error) : std::nullopt;
    if (!image) {
        Q_EMIT refused(tr("%1 is not a picture that can be read.").arg(asset));
        return false;
    }
    std::vector<sprite::Color> colours;
    for (const auto& rgb : scene2d::image_colors(*image, 64)) {
        colours.push_back(sprite::Color{rgb[0], rgb[1], rgb[2], 255});
    }
    const std::string text = sprite::palette_text(colours);
    apply([&text](Settings& s) { s.palette = text; });
    document_->end_merge();
    QTimer::singleShot(0, this, [this] { refresh(); });
    return true;
}

void ProjectPanel::palette_from_sprite_studio() {
    if (document_ == nullptr) {
        return;
    }
    const std::string text = sprite::palette_text(sprite::default_palette());
    apply([&text](Settings& s) { s.palette = text; });
    document_->end_merge();
    QTimer::singleShot(0, this, [this] { refresh(); });
}

void ProjectPanel::refresh() {
    build();
}

template <typename Change>
void ProjectPanel::apply(const Change& change) {
    if (document_ == nullptr || filling_) {
        return;
    }
    Settings settings = document_->settings();
    change(settings);
    std::string error;
    if (!document_->set_settings(settings, error)) {
        Q_EMIT refused(text_of(error));
        return;
    }
    Q_EMIT edited();
}

bool ProjectPanel::eventFilter(QObject* watched, QEvent* event) {
    // Hovering a preset shows it; leaving shows the project's own again.
    if (const QVariant preset = watched->property("preset"); preset.isValid()) {
        if (event->type() == QEvent::Enter) {
            const auto i = static_cast<std::size_t>(preset.toInt());
            if (i < look_presets().size()) {
                Q_EMIT previewing(look_presets()[i].look);
            }
        } else if (event->type() == QEvent::Leave) {
            Q_EMIT previewing(std::nullopt);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectPanel::build() {
    // Old fields go now, so lookups by name find only the new ones. (A copy of the list:
    // reparenting a child changes children() as it is walked.)
    const QObjectList old = content_->children();
    for (QObject* child : old) {
        if (auto* widget = qobject_cast<QWidget*>(child)) {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
    }
    delete content_->layout();
    auto* groups = new QVBoxLayout(content_);
    groups->setContentsMargins(4, 4, 4, 4);
    if (document_ == nullptr) {
        return;
    }
    filling_ = true;
    const Settings& now = document_->settings();
    const auto end = [this] {
        if (document_ != nullptr) {
            document_->end_merge();
        }
    };
    const auto group = [&](const QString& title) {
        auto* box = new QGroupBox(title, content_);
        auto* form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        groups->addWidget(box);
        return form;
    };
    // A number: a spin box with the setting's range, writing it as it changes.
    const auto number = [&](QFormLayout* form, const QString& label, const char* id, double low,
                            double high, double step, int decimals,
                            const std::function<double(const Settings&)>& get,
                            const std::function<void(Settings&, double)>& set) {
        auto* spin = new QDoubleSpinBox(form->parentWidget());
        spin->setObjectName(QStringLiteral("setting:") + QString::fromLatin1(id));
        spin->setRange(low, high);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setValue(get(now));
        spin->setMinimumWidth(72);
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, set](double v) { apply([&set, v](Settings& s) { set(s, v); }); });
        connect(spin, &QDoubleSpinBox::editingFinished, this, end);
        form->addRow(label, spin);
    };
    const auto tick = [&](QFormLayout* form, const QString& label, const char* id,
                          const std::function<bool(const Settings&)>& get,
                          const std::function<void(Settings&, bool)>& set) {
        auto* box = new QCheckBox(form->parentWidget());
        box->setObjectName(QStringLiteral("setting:") + QString::fromLatin1(id));
        box->setChecked(get(now));
        connect(box, &QCheckBox::toggled, this, [this, set, end](bool on) {
            apply([&set, on](Settings& s) { set(s, on); });
            end();
        });
        form->addRow(label, box);
    };
    const auto choice = [&](QFormLayout* form, const QString& label, const char* id,
                            const QStringList& names, int current,
                            const std::function<void(Settings&, int)>& set) {
        auto* combo = new QComboBox(form->parentWidget());
        combo->setObjectName(QStringLiteral("setting:") + QString::fromLatin1(id));
        combo->addItems(names);
        combo->setCurrentIndex(current);
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, set, end](int i) {
            apply([&set, i](Settings& s) { set(s, i); });
            end();
        });
        form->addRow(label, combo);
    };

    QFormLayout* game_form = group(tr("Game"));
    auto* name = new QLineEdit(text_of(now.name), content_);
    name->setObjectName(QStringLiteral("setting:name"));
    connect(name, &QLineEdit::textEdited, this, [this](const QString& text) {
        const QByteArray bytes = text.toUtf8();
        apply([&bytes](Settings& s) {
            s.name.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        });
    });
    connect(name, &QLineEdit::editingFinished, this, end);
    game_form->addRow(tr("Name"), name);
    number(
        game_form, tr("Gravity across"), "gravity_x", -1000, 1000, 1, 1,
        [](const Settings& s) { return s.gravity_x; },
        [](Settings& s, double v) { s.gravity_x = v; });
    number(
        game_form, tr("Gravity down"), "gravity_y", -1000, 1000, 1, 1,
        [](const Settings& s) { return s.gravity_y; },
        [](Settings& s, double v) { s.gravity_y = v; });
    number(
        game_form, tr("Scene fade (seconds)"), "scene_fade", 0, 3, 0.1, 2,
        [](const Settings& s) { return s.scene_fade; },
        [](Settings& s, double v) { s.scene_fade = v; });

    QFormLayout* picture = group(tr("Picture"));
    choice(picture, tr("Style"), "render_style", {tr("Classic 2D"), tr("Diorama (3D)")},
           now.style == game::RenderStyle::Diorama ? 1 : 0, [](Settings& s, int i) {
               s.style = i == 1 ? game::RenderStyle::Diorama : game::RenderStyle::Classic2D;
           });
    number(
        picture, tr("Width"), "width", 16, 7680, 16, 0,
        [](const Settings& s) { return static_cast<double>(s.width); },
        [](Settings& s, double v) { s.width = static_cast<std::uint32_t>(v); });
    number(
        picture, tr("Height"), "height", 16, 4320, 9, 0,
        [](const Settings& s) { return static_cast<double>(s.height); },
        [](Settings& s, double v) { s.height = static_cast<std::uint32_t>(v); });
    number(
        picture, tr("Pixels per tile"), "ppu", 8, 64, 1, 0,
        [](const Settings& s) { return static_cast<double>(s.ppu); },
        [](Settings& s, double v) { s.ppu = static_cast<std::uint32_t>(v); });
    const auto channel = [&](const QString& label, const char* id,
                             std::uint8_t engine::render::Color::*part) {
        number(
            picture, label, id, 0, 255, 1, 0,
            [part](const Settings& s) { return static_cast<double>(s.background.*part); },
            [part](Settings& s, double v) { s.background.*part = static_cast<std::uint8_t>(v); });
    };
    channel(tr("Background red"), "background_r", &engine::render::Color::r);
    channel(tr("Background green"), "background_g", &engine::render::Color::g);
    channel(tr("Background blue"), "background_b", &engine::render::Color::b);

    QFormLayout* words = group(tr("Languages"));
    const auto text_field = [&](const QString& label, const char* key, const QString& tip,
                                const std::string& value, std::string Settings::*field) {
        auto* line = new QLineEdit(text_of(value), content_);
        line->setObjectName(QStringLiteral("setting:%1").arg(QLatin1String(key)));
        line->setToolTip(tip);
        connect(line, &QLineEdit::textEdited, this, [this, field](const QString& text) {
            const QByteArray bytes = text.trimmed().toUtf8();
            apply([&bytes, field](Settings& s) {
                (s.*field).assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
            });
        });
        connect(line, &QLineEdit::editingFinished, this, end);
        words->addRow(label, line);
    };
    text_field(tr("Shown in"), "language",
               tr("A column of data/translations (fr), pseudo to check what is not translated, "
                  "or empty for the words as written"),
               now.language, &Settings::language);
    text_field(tr("Translate into"), "languages",
               tr("Language codes, separated by commas (fr, de): Game > Collect texts to "
                  "translate makes a column for each"),
               now.languages, &Settings::languages);

    // The style guide: the colours the game's art keeps to.
    QFormLayout* style = group(tr("Style guide"));
    auto* palette_line = new QLineEdit(text_of(now.palette), content_);
    palette_line->setObjectName(QStringLiteral("setting:palette"));
    palette_line->setPlaceholderText(tr("(any colours)"));
    palette_line->setToolTip(tr("The colours the art keeps to, as #rrggbb: new sprites start "
                                "with them, and Check the project notes pictures that use others"));
    connect(palette_line, &QLineEdit::textEdited, this, [this](const QString& text) {
        const QByteArray bytes = text.trimmed().toUtf8();
        apply([&bytes](Settings& s) {
            s.palette.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        });
    });
    connect(palette_line, &QLineEdit::editingFinished, this, [this, end] {
        end();
        QTimer::singleShot(0, this, [this] { refresh(); }); // the swatches again
    });
    style->addRow(tr("Palette"), palette_line);
    // The palette as swatches, sixteen to a row.
    const std::vector<sprite::Color> colours = sprite::parse_palette(now.palette);
    if (!colours.empty()) {
        constexpr int cell = 12;
        constexpr int across = 16;
        const int rows = (static_cast<int>(colours.size()) + across - 1) / across;
        QPixmap strip(across * cell, rows * cell);
        strip.fill(Qt::transparent);
        QPainter painter(&strip);
        for (std::size_t i = 0; i < colours.size(); ++i) {
            const int at = static_cast<int>(i);
            painter.fillRect((at % across) * cell, (at / across) * cell, cell - 1, cell - 1,
                             QColor(colours[i].r, colours[i].g, colours[i].b));
        }
        painter.end();
        auto* swatches = new QLabel(content_);
        swatches->setObjectName(QStringLiteral("palette_swatches"));
        swatches->setPixmap(strip);
        swatches->setToolTip(tr("%n colour(s)", nullptr, static_cast<int>(colours.size())));
        style->addRow(QString(), swatches);
    }
    auto* palette_buttons = new QWidget(content_);
    auto* palette_row = new QHBoxLayout(palette_buttons);
    palette_row->setContentsMargins(0, 0, 0, 0);
    auto* from_picture = new QPushButton(tr("From a picture…"), palette_buttons);
    from_picture->setToolTip(tr("The colours a picture of the project uses"));
    auto* from_studio = new QPushButton(tr("Sprite Studio's"), palette_buttons);
    from_studio->setToolTip(tr("Sprite Studio's own 32 colours, in ramps"));
    palette_row->addWidget(from_picture);
    palette_row->addWidget(from_studio);
    palette_row->addStretch();
    style->addRow(QString(), palette_buttons);
    // The font: one of the project's pictures, a strip of letters, or the built-in one.
    auto* font = new QComboBox(content_);
    font->setObjectName(QStringLiteral("setting:font"));
    font->setToolTip(tr("A picture of letters side by side, each separated by a column of the "
                        "top-left pixel's colour; or the built-in font"));
    font->addItem(tr("(built-in)"), QString());
    {
        QStringList pictures;
        for (const engine::assets::AssetRecord& record : document_->assets().assets()) {
            if (record.importer == "image") {
                pictures << text_of(record.path);
            }
        }
        pictures.sort();
        for (const QString& strip : pictures) {
            font->addItem(strip, strip);
        }
        const int chosen = font->findData(text_of(now.font));
        if (chosen >= 0) {
            font->setCurrentIndex(chosen);
        } else if (!now.font.empty()) {
            font->addItem(tr("(missing) %1").arg(text_of(now.font)), text_of(now.font));
            font->setCurrentIndex(font->count() - 1);
        }
    }
    connect(font, qOverload<int>(&QComboBox::activated), this, [this, font, end](int index) {
        const QByteArray bytes = font->itemData(index).toString().toUtf8();
        apply([&bytes](Settings& s) {
            s.font.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        });
        end();
    });
    style->addRow(tr("Font"), font);
    auto* letters = new QLineEdit(text_of(now.font_characters), content_);
    letters->setObjectName(QStringLiteral("setting:font_characters"));
    letters->setPlaceholderText(tr("(the space to ~, in ASCII order)"));
    letters->setToolTip(tr("The font picture's letters, in the order drawn"));
    connect(letters, &QLineEdit::textEdited, this, [this](const QString& text) {
        const QByteArray bytes = text.toUtf8();
        apply([&bytes](Settings& s) {
            s.font_characters.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        });
    });
    connect(letters, &QLineEdit::editingFinished, this, end);
    style->addRow(tr("Its letters"), letters);
    connect(from_studio, &QPushButton::clicked, this, [this] { palette_from_sprite_studio(); });
    connect(from_picture, &QPushButton::clicked, this, [this] {
        if (document_ == nullptr) {
            return;
        }
        QStringList pictures;
        for (const engine::assets::AssetRecord& record : document_->assets().assets()) {
            if (record.importer == "image") {
                pictures << text_of(record.path);
            }
        }
        pictures.sort();
        bool chosen = false;
        const QString source =
            QInputDialog::getItem(this, tr("Palette from a picture"), tr("Take the colours of:"),
                                  pictures, 0, false, &chosen);
        if (chosen) {
            palette_from_picture(source);
        }
    });

    QFormLayout* menus = group(tr("Menus and messages"));
    number(
        menus, tr("Music while a message shows (0 to 1)"), "music_while_talking", 0, 1, 0.05, 2,
        [](const Settings& s) { return s.music_while_talking; },
        [](Settings& s, double v) { s.music_while_talking = v; });
    number(
        menus, tr("Text speed (letters a second, 0: at once)"), "text_speed", 0, 1000, 5, 0,
        [](const Settings& s) { return s.text_speed; },
        [](Settings& s, double v) { s.text_speed = v; });
    tick(
        menus, tr("Title screen (New game, Continue, Quit)"), "title_screen",
        [](const Settings& s) { return s.title_screen; },
        [](Settings& s, bool on) { s.title_screen = on; });
    tick(
        menus, tr("Pause menu on the Menu key"), "pause_menu",
        [](const Settings& s) { return s.pause_menu; },
        [](Settings& s, bool on) { s.pause_menu = on; });
    tick(
        menus, tr("Minimap (top right)"), "minimap", [](const Settings& s) { return s.minimap; },
        [](Settings& s, bool on) { s.minimap = on; });
    // Their colours: a ready-made look, or each colour on its own.
    auto* ui_presets = new QComboBox(content_);
    ui_presets->setObjectName(QStringLiteral("setting:ui_preset"));
    ui_presets->setToolTip(tr("A ready-made look for every box: messages, menus, shops, battles"));
    ui_presets->addItem(tr("(your own colours)"));
    for (const engine::game::UiThemePreset& preset : engine::game::ui_theme_presets()) {
        ui_presets->addItem(QString::fromUtf8(preset.name));
        if (preset.theme == now.ui) {
            ui_presets->setCurrentIndex(ui_presets->count() - 1);
        }
    }
    connect(ui_presets, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (index <= 0) {
            return;
        }
        apply([index](Settings& s) {
            s.ui = engine::game::ui_theme_presets().at(static_cast<std::size_t>(index - 1)).theme;
        });
        if (document_ != nullptr) {
            document_->end_merge();
        }
        QTimer::singleShot(0, this, [this] { refresh(); });
    });
    menus->addRow(tr("Look"), ui_presets);
    auto* swatches = new QWidget(content_);
    auto* row = new QHBoxLayout(swatches);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    for (const auto& [label, field] :
         {std::pair{tr("Box"), &engine::game::UiTheme::box},
          std::pair{tr("Border"), &engine::game::UiTheme::border},
          std::pair{tr("Words"), &engine::game::UiTheme::text},
          std::pair{tr("Hints"), &engine::game::UiTheme::dim},
          std::pair{tr("Chosen"), &engine::game::UiTheme::highlight},
          std::pair{tr("Name plate"), &engine::game::UiTheme::plate}}) {
        const engine::render::Color c = now.ui.*field;
        auto* swatch = new QPushButton(swatches);
        swatch->setObjectName(QStringLiteral("setting:ui_") +
                              label.toLower().remove(QLatin1Char(' ')));
        swatch->setToolTip(tr("%1 colour: click to choose").arg(label));
        swatch->setFixedSize(22, 22);
        swatch->setStyleSheet(QStringLiteral("background: rgb(%1, %2, %3); border: 1px solid gray;")
                                  .arg(c.r)
                                  .arg(c.g)
                                  .arg(c.b));
        connect(swatch, &QPushButton::clicked, this, [this, field, c, label] {
            const QColor picked =
                QColorDialog::getColor(QColor(c.r, c.g, c.b, c.a), this, tr("%1 colour").arg(label),
                                       QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) {
                return;
            }
            apply([field, picked](Settings& s) {
                s.ui.*field = engine::render::Color{static_cast<std::uint8_t>(picked.red()),
                                                    static_cast<std::uint8_t>(picked.green()),
                                                    static_cast<std::uint8_t>(picked.blue()),
                                                    static_cast<std::uint8_t>(picked.alpha())};
            });
            if (document_ != nullptr) {
                document_->end_merge();
            }
            QTimer::singleShot(0, this, [this] { refresh(); });
        });
        row->addWidget(swatch);
    }
    row->addStretch();
    menus->addRow(tr("Colours"), swatches);
    // A picture every box is drawn with instead of the colours: a frame stretched by its
    // 9-slice edge.
    auto* frame = new QComboBox(content_);
    frame->setObjectName(QStringLiteral("setting:box_frame"));
    frame->setToolTip(tr("A picture every box is drawn with (messages, menus, shops, "
                         "battles), its edges kept crisp as it stretches"));
    frame->addItem(tr("(the colours)"), QString());
    {
        QStringList pictures;
        for (const engine::assets::AssetRecord& record : document_->assets().assets()) {
            if (record.importer == "image") {
                pictures << text_of(record.path);
            }
        }
        pictures.sort();
        for (const QString& picture_path : pictures) {
            frame->addItem(picture_path, picture_path);
        }
        const int chosen = frame->findData(text_of(now.box_frame));
        if (chosen >= 0) {
            frame->setCurrentIndex(chosen);
        } else if (!now.box_frame.empty()) {
            frame->addItem(tr("(missing) %1").arg(text_of(now.box_frame)), text_of(now.box_frame));
            frame->setCurrentIndex(frame->count() - 1);
        }
    }
    connect(frame, qOverload<int>(&QComboBox::activated), this, [this, frame, end](int index) {
        const QByteArray bytes = frame->itemData(index).toString().toUtf8();
        apply([&bytes](Settings& s) {
            s.box_frame.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        });
        end();
    });
    menus->addRow(tr("Box frame"), frame);
    number(
        menus, tr("Frame edge (pixels)"), "box_frame_edge", 0, 64, 1, 0,
        [](const Settings& s) { return s.box_frame_edge; },
        [](Settings& s, double v) { s.box_frame_edge = v; });

    QFormLayout* day = group(tr("Day and night"));
    number(
        day, tr("Day length (minutes, 0: no clock)"), "day_minutes", 0, 1440, 1, 0,
        [](const Settings& s) { return s.day_minutes; },
        [](Settings& s, double v) { s.day_minutes = v; });
    number(
        day, tr("Starting hour"), "start_hour", 0, 24, 0.5, 1,
        [](const Settings& s) { return s.start_hour; },
        [](Settings& s, double v) { s.start_hour = v; });

    QFormLayout* following = group(tr("Camera following"));
    number(
        following, tr("Smoothing (s)"), "camera_smoothing", 0, 2, 0.05, 2,
        [](const Settings& s) { return s.camera_smoothing; },
        [](Settings& s, double v) { s.camera_smoothing = v; });
    number(
        following, tr("Dead zone (tiles)"), "camera_dead_zone", 0, 16, 0.25, 2,
        [](const Settings& s) { return s.camera_dead_zone; },
        [](Settings& s, double v) { s.camera_dead_zone = v; });
    number(
        following, tr("Look ahead (tiles)"), "camera_look_ahead", 0, 8, 0.25, 2,
        [](const Settings& s) { return s.camera_look_ahead; },
        [](Settings& s, double v) { s.camera_look_ahead = v; });

    QFormLayout* camera = group(tr("Camera (Diorama)"));
    number(
        camera, tr("Looking down (°)"), "camera_pitch", 5, 85, 1, 1,
        [](const Settings& s) { return s.camera_pitch; },
        [](Settings& s, double v) { s.camera_pitch = v; });
    number(
        camera, tr("Distance (tiles)"), "camera_distance", 1, 200, 0.5, 1,
        [](const Settings& s) { return s.camera_distance; },
        [](Settings& s, double v) { s.camera_distance = v; });
    number(
        camera, tr("Field of view (°)"), "camera_fov", 5, 90, 1, 1,
        [](const Settings& s) { return s.camera_fov; },
        [](Settings& s, double v) { s.camera_fov = v; });
    tick(
        camera, tr("Stay inside the map"), "camera_clamp",
        [](const Settings& s) { return s.camera_clamp; },
        [](Settings& s, bool on) { s.camera_clamp = on; });

    QFormLayout* light = group(tr("Light and shadows (Diorama)"));
    number(
        light, tr("Ambient light"), "ambient_light", 0, 2, 0.05, 2,
        [](const Settings& s) { return s.ambient_light; },
        [](Settings& s, double v) { s.ambient_light = v; });
    number(
        light, tr("Sunlight"), "sun_light", 0, 2, 0.05, 2,
        [](const Settings& s) { return s.sun_light; },
        [](Settings& s, double v) { s.sun_light = v; });
    number(
        light, tr("Sun direction (°)"), "sun_angle", 0, 360, 5, 1,
        [](const Settings& s) { return s.sun_angle; },
        [](Settings& s, double v) { s.sun_angle = v; });
    number(
        light, tr("Sun height (°)"), "sun_height", 5, 90, 5, 1,
        [](const Settings& s) { return s.sun_height; },
        [](Settings& s, double v) { s.sun_height = v; });
    number(
        light, tr("Sprite shadows"), "sprite_shadow", 0, 1, 0.05, 2,
        [](const Settings& s) { return s.sprite_shadow; },
        [](Settings& s, double v) { s.sprite_shadow = v; });
    choice(light, tr("Sprite shadow shape"), "shadow_shape",
           {tr("Cast by the sun"), tr("Round, under the feet")},
           now.shadow_shape == scene2d::ShadowShape::Round ? 1 : 0, [](Settings& s, int i) {
               s.shadow_shape = i == 1 ? scene2d::ShadowShape::Round : scene2d::ShadowShape::Cast;
           });
    number(
        light, tr("Block shadows"), "block_shadow", 0, 1, 0.05, 2,
        [](const Settings& s) { return s.block_shadow; },
        [](Settings& s, double v) { s.block_shadow = v; });

    QFormLayout* look = group(tr("Look"));
    // Presets first, as a gallery: hovering one shows it in the scene, clicking takes it.
    auto* gallery = new QWidget(look->parentWidget());
    auto* tiles = new QGridLayout(gallery);
    tiles->setContentsMargins(0, 0, 0, 0);
    tiles->setSpacing(2);
    const std::span<const LookPreset> presets = look_presets();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const LookPreset& preset = presets[i];
        auto* button = new QPushButton(
            QString::fromUtf8(preset.name.data(), static_cast<qsizetype>(preset.name.size())),
            gallery);
        button->setObjectName(QStringLiteral("look_preset:%1").arg(i));
        button->setToolTip(QString::fromUtf8(preset.description.data(),
                                             static_cast<qsizetype>(preset.description.size())));
        button->setProperty("preset", static_cast<int>(i));
        button->installEventFilter(this);
        connect(button, &QPushButton::clicked, this, [this, i] {
            apply([i](Settings& s) { s.look = look_presets()[i].look; });
            if (document_ != nullptr) {
                document_->end_merge();
            }
            Q_EMIT previewing(std::nullopt);
            QTimer::singleShot(0, this, [this] { refresh(); });
        });
        tiles->addWidget(button, static_cast<int>(i / 3), static_cast<int>(i % 3));
    }
    look->addRow(gallery);
    tick(
        look, tr("Tilt-shift (Diorama)"), "tilt_shift",
        [](const Settings& s) { return s.look.tilt_shift; },
        [](Settings& s, bool on) { s.look.tilt_shift = on; });
    struct LookField {
        const char* label;
        const char* id;
        float engine::render::Look::*member;
        double high;
    };
    for (const LookField& field : {
             LookField{"Focus centre", "focus_center", &engine::render::Look::focus_center, 1},
             LookField{"Focus band", "focus_height", &engine::render::Look::focus_height, 1},
             LookField{"Blur (pixels)", "blur", &engine::render::Look::blur, 12},
             LookField{"Blur ramp", "blur_ramp", &engine::render::Look::blur_ramp, 1},
             LookField{"Bloom", "bloom", &engine::render::Look::bloom, 2},
             LookField{"Bloom threshold", "bloom_threshold", &engine::render::Look::bloom_threshold,
                       1},
             LookField{"Bloom radius (pixels)", "bloom_radius", &engine::render::Look::bloom_radius,
                       16},
             LookField{"Exposure", "exposure", &engine::render::Look::exposure, 4},
             LookField{"Contrast", "contrast", &engine::render::Look::contrast, 2},
             LookField{"Saturation", "saturation", &engine::render::Look::saturation, 2},
             LookField{"Vignette", "vignette", &engine::render::Look::vignette, 1},
         }) {
        const auto member = field.member;
        number(
            look, tr(field.label), field.id, 0, field.high, field.high > 4 ? 0.5 : 0.05, 2,
            [member](const Settings& s) { return static_cast<double>(s.look.*member); },
            [member](Settings& s, double v) { s.look.*member = static_cast<float>(v); });
    }
    filling_ = false;
}

} // namespace editor
