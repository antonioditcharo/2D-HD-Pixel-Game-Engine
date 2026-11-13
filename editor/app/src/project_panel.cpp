#include "project_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <functional>
#include <string>

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
