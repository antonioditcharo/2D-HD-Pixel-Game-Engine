#include "inspector.h"

#include "engine/assets/assets.h"
#include "engine/foundation/reflect.h"
#include "engine/scene2d/scene2d.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <cmath>
#include <optional>
#include <string>
#include <variant>

#include "recolor_dialog.h"

namespace editor {

namespace reflect = engine::reflect;

namespace {

/// The tags of the sheet of `entity`'s Sprite picture; empty if it has none.
std::vector<std::string> sheet_tags(const Document& document, engine::world::Entity entity) {
    const auto* sprite = document.world().get<engine::scene2d::Sprite>(entity);
    const std::optional<engine::Guid> guid =
        sprite != nullptr ? engine::Guid::parse(sprite->image) : std::nullopt;
    const engine::assets::AssetRecord* picture = guid ? document.assets().find(*guid) : nullptr;
    const engine::assets::AssetRecord* file =
        picture != nullptr ? document.assets().find(engine::scene2d::sheet_path(picture->path))
                           : nullptr;
    const std::optional<std::vector<std::uint8_t>> bytes =
        file != nullptr ? document.assets().read(file->guid) : std::nullopt;
    std::vector<std::string> out;
    if (!bytes) {
        return out;
    }
    std::string error;
    const std::optional<engine::scene2d::SheetInfo> sheet = engine::scene2d::parse_sheet(
        std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), // NOLINT
        error);
    if (sheet) {
        for (const engine::scene2d::SheetTag& tag : sheet->tags) {
            out.push_back(tag.name);
        }
    }
    return out;
}

QString text_of(const std::string& s) {
    return QString::fromStdString(s);
}

/// The pixels of the entity's Sprite picture, if it has one that can be read.
std::optional<engine::assets::Image> sprite_picture(const Document& document,
                                                    engine::world::Entity entity) {
    const auto* sprite = document.world().get<engine::scene2d::Sprite>(entity);
    const std::optional<engine::Guid> guid =
        sprite != nullptr ? engine::Guid::parse(sprite->image) : std::nullopt;
    const std::optional<std::vector<std::uint8_t>> bytes =
        guid ? document.assets().read(*guid) : std::nullopt;
    if (!bytes) {
        return std::nullopt;
    }
    std::string error;
    return engine::assets::decode_png(*bytes, error);
}

/// The suffix a unit shows after a number, with a space.
QString suffix(reflect::Unit unit) {
    const std::string_view symbol = reflect::unit_symbol(unit);
    return symbol.empty() ? QString() : QStringLiteral(" ") + text_of(std::string(symbol));
}

/// The project's image assets, for picture fields: (path, GUID).
std::vector<std::pair<std::string, std::string>> images(Document& document) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const engine::assets::AssetRecord& record : document.assets().assets()) {
        if (record.importer == "image") {
            out.emplace_back(record.path, record.guid.to_string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// The project's sound assets, for sound fields: (path, GUID).
std::vector<std::pair<std::string, std::string>> sounds(Document& document) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const engine::assets::AssetRecord& record : document.assets().assets()) {
        const std::string_view path = record.path;
        if (path.ends_with(".wav") || path.ends_with(".ogg") || path.ends_with(".flac") ||
            path.ends_with(".mp3")) {
            out.emplace_back(record.path, record.guid.to_string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

Inspector::Inspector(QWidget* parent) : QWidget(parent), layout_(new QVBoxLayout(this)) {
    layout_->setContentsMargins(4, 4, 4, 4);
}

void Inspector::show_entity(Document* document, engine::Guid entity) {
    show_entities(document, entity.is_valid() ? std::vector<engine::Guid>{entity}
                                              : std::vector<engine::Guid>{});
}

void Inspector::show_entities(Document* document, std::vector<engine::Guid> entities) {
    document_ = document;
    entities_ = std::move(entities);
    entity_ = entities_.empty() ? engine::Guid{} : entities_.front();
    rebuild();
}

void Inspector::set_show_advanced(bool show) {
    show_advanced_ = show;
    rebuild();
}

void Inspector::rebuild() {
    // The old fields go later, not now: this may run from one of their own signals.
    if (content_ != nullptr) {
        layout_->removeWidget(content_);
        content_->hide();
        content_->setParent(nullptr); // no longer found among the inspector's fields
        content_->deleteLater();
    }
    content_ = new QWidget(this);
    layout_->addWidget(content_);
    auto* column = new QVBoxLayout(content_);
    column->setContentsMargins(0, 0, 0, 0);
    const engine::world::Entity entity =
        document_ != nullptr ? document_->world().find(entity_) : engine::world::Entity{};
    if (!entity.is_valid()) {
        auto* hint =
            new QLabel(tr("Select something in the scene or the list to see it here."), content_);
        hint->setWordWrap(true);
        column->addWidget(hint);
        column->addStretch();
        return;
    }
    const engine::world::World& world = document_->world();
    if (entities_.size() > 1) {
        rebuild_many(*column);
        return;
    }

    // The name.
    auto* name = new QLineEdit(text_of(std::string(world.name(entity))), content_);
    name->setObjectName(QStringLiteral("name"));
    name->setAccessibleName(tr("Name"));
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
        const std::string text = name->text().toStdString();
        const engine::world::Entity e = document_->world().find(entity_);
        if (e.is_valid() && document_->world().name(e) != text &&
            document_->rename(entity_, text)) {
            Q_EMIT edited(true);
        }
    });
    auto* name_row = new QFormLayout();
    name_row->addRow(tr("Name"), name);
    column->addLayout(name_row);

    // A group for each component, in the order they were added.
    for (const reflect::TypeInfo* component : world.components(entity)) {
        auto* group = new QGroupBox(text_of(component->meta.display_name), content_);
        group->setObjectName(text_of("component:" + component->name));
        group->setToolTip(text_of(component->meta.tooltip));
        auto* form = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const reflect::PropertyInfo& property : component->properties) {
            if (reflect::has_flag(property.flags, reflect::property_flag::hidden) ||
                (!show_advanced_ &&
                 reflect::has_flag(property.flags, reflect::property_flag::advanced))) {
                continue;
            }
            QWidget* editor = field(*component, property);
            editor->setToolTip(text_of(property.meta.tooltip));
            editor->setAccessibleName(text_of(property.meta.display_name));
            form->addRow(text_of(property.meta.display_name), editor);
        }
        auto* remove =
            new QPushButton(tr("Remove %1").arg(text_of(component->meta.display_name)), group);
        remove->setObjectName(text_of("remove:" + component->name));
        connect(remove, &QPushButton::clicked, this, [this, component] {
            if (document_->remove_component(entity_, *component)) {
                Q_EMIT edited(true);
                rebuild();
            }
        });
        form->addRow(remove);
        column->addWidget(group);
    }

    // Adding a component the entity does not have.
    auto* add = new QComboBox(content_);
    add->setObjectName(QStringLiteral("add_component"));
    add->setAccessibleName(tr("Add a component"));
    add->addItem(tr("Add a component…"));
    for (const reflect::TypeInfo* type : document_->component_types()) {
        if (world.get(entity, *type).is_null()) {
            add->addItem(text_of(type->meta.display_name), text_of(type->name));
        }
    }
    connect(add, qOverload<int>(&QComboBox::activated), this, [this, add](int index) {
        if (index <= 0) {
            return;
        }
        const std::string type_name = add->itemData(index).toString().toStdString();
        if (const reflect::TypeInfo* type = document_->registry().find_type(type_name);
            type != nullptr && document_->add_component(entity_, *type)) {
            Q_EMIT edited(true);
            rebuild();
        }
    });
    column->addWidget(add);
    column->addStretch();
}

void Inspector::rebuild_many(QVBoxLayout& column) {
    const engine::world::World& world = document_->world();
    auto* heading = new QLabel(
        tr("%1 selected. A change here changes them all.").arg(entities_.size()), content_);
    heading->setObjectName(QStringLiteral("many"));
    heading->setWordWrap(true);
    column.addWidget(heading);
    // One name for them all, numbered: "tree" names them tree_1, tree_2, ...
    auto* name = new QLineEdit(content_);
    name->setObjectName(QStringLiteral("name_all"));
    name->setPlaceholderText(tr("Name them all (numbered)"));
    name->setAccessibleName(tr("Name them all"));
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
        const std::string base = name->text().trimmed().toStdString();
        if (!base.empty() && document_->rename_many(entities_, base)) {
            Q_EMIT edited(true);
        }
    });
    auto* name_row = new QFormLayout();
    name_row->addRow(tr("Name"), name);
    column.addLayout(name_row);
    // The components all of them have, in the first one's order.
    const engine::world::Entity first = world.find(entity_);
    for (const reflect::TypeInfo* component : world.components(first)) {
        const bool shared = std::all_of(entities_.begin(), entities_.end(), [&](engine::Guid g) {
            const engine::world::Entity e = world.find(g);
            return e.is_valid() && !world.get(e, *component).is_null();
        });
        if (!shared) {
            continue;
        }
        auto* group = new QGroupBox(text_of(component->meta.display_name), content_);
        group->setObjectName(text_of("component:" + component->name));
        auto* form = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const reflect::PropertyInfo& property : component->properties) {
            if (reflect::has_flag(property.flags, reflect::property_flag::hidden) ||
                (!show_advanced_ &&
                 reflect::has_flag(property.flags, reflect::property_flag::advanced))) {
                continue;
            }
            const std::optional<reflect::Value> mine =
                reflect::get_property(world.get(first, *component), property);
            const bool differs =
                std::any_of(entities_.begin(), entities_.end(), [&](engine::Guid g) {
                    return reflect::get_property(world.get(world.find(g), *component), property) !=
                           mine;
                });
            QWidget* editor = field(*component, property);
            editor->setToolTip(text_of(property.meta.tooltip));
            editor->setAccessibleName(text_of(property.meta.display_name));
            form->addRow(differs ? tr("%1 (differs)").arg(text_of(property.meta.display_name))
                                 : text_of(property.meta.display_name),
                         editor);
        }
        auto* remove = new QPushButton(
            tr("Remove %1 from all").arg(text_of(component->meta.display_name)), group);
        remove->setObjectName(text_of("remove:" + component->name));
        connect(remove, &QPushButton::clicked, this, [this, component] {
            if (document_->remove_component_many(entities_, *component)) {
                Q_EMIT edited(true);
                rebuild();
            }
        });
        form->addRow(remove);
        column.addWidget(group);
    }
    // Adding a component to all of them: those not every one has yet.
    auto* add = new QComboBox(content_);
    add->setObjectName(QStringLiteral("add_component"));
    add->setAccessibleName(tr("Add a component to all"));
    add->addItem(tr("Add a component to all…"));
    for (const reflect::TypeInfo* type : document_->component_types()) {
        const bool everyone = std::all_of(entities_.begin(), entities_.end(), [&](engine::Guid g) {
            const engine::world::Entity e = world.find(g);
            return e.is_valid() && !world.get(e, *type).is_null();
        });
        if (!everyone) {
            add->addItem(text_of(type->meta.display_name), text_of(type->name));
        }
    }
    connect(add, qOverload<int>(&QComboBox::activated), this, [this, add](int index) {
        if (index <= 0) {
            return;
        }
        const std::string type_name = add->itemData(index).toString().toStdString();
        if (const reflect::TypeInfo* type = document_->registry().find_type(type_name);
            type != nullptr && document_->add_component_many(entities_, *type)) {
            Q_EMIT edited(true);
            rebuild();
        }
    });
    column.addWidget(add);
    column.addStretch();
}

QWidget* Inspector::field(const reflect::TypeInfo& component,
                          const reflect::PropertyInfo& property) {
    const engine::world::Entity entity = document_->world().find(entity_);
    const std::optional<reflect::Value> current =
        reflect::get_property(document_->world().get(entity, component), property);
    const QString object_name = text_of("property:" + component.name + "/" + property.id);
    const bool read_only = reflect::has_flag(property.flags, reflect::property_flag::read_only);
    // Writes a value; reports a refusal.
    const auto write = [this, &component, &property](const reflect::Value& value) {
        std::string error;
        if (document_->set_property_many(entities_, component, property, value, error)) {
            Q_EMIT edited(false);
        } else {
            Q_EMIT refused(text_of(error));
        }
    };
    const auto end = [this] { document_->end_merge(); };

    QWidget* widget = nullptr;
    switch (property.kind) {
    case reflect::ValueKind::Bool: {
        auto* box = new QCheckBox(content_);
        box->setChecked(current && std::holds_alternative<bool>(*current) &&
                        std::get<bool>(*current));
        connect(box, &QCheckBox::toggled, this, [write, end](bool on) {
            write(reflect::Value{on});
            end();
        });
        widget = box;
        break;
    }
    case reflect::ValueKind::Int: {
        auto* spin = new QSpinBox(content_);
        const double low = property.limits.min.value_or(static_cast<double>(INT_MIN));
        const double high = property.limits.max.value_or(static_cast<double>(INT_MAX));
        spin->setRange(static_cast<int>(std::max(low, static_cast<double>(INT_MIN))),
                       static_cast<int>(std::min(high, static_cast<double>(INT_MAX))));
        if (property.limits.step >= 1.0) {
            spin->setSingleStep(static_cast<int>(property.limits.step));
        }
        spin->setSuffix(suffix(property.unit));
        if (current && std::holds_alternative<std::int64_t>(*current)) {
            spin->setValue(static_cast<int>(std::get<std::int64_t>(*current)));
        }
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                [write](int value) { write(reflect::Value{std::int64_t{value}}); });
        connect(spin, &QSpinBox::editingFinished, this, end);
        widget = spin;
        break;
    }
    case reflect::ValueKind::Float: {
        auto* spin = new QDoubleSpinBox(content_);
        spin->setDecimals(3);
        spin->setRange(property.limits.min.value_or(-1.0e6), property.limits.max.value_or(1.0e6));
        spin->setSingleStep(property.limits.step > 0.0 ? property.limits.step : 0.25);
        spin->setSuffix(suffix(property.unit));
        if (current && std::holds_alternative<double>(*current)) {
            spin->setValue(std::get<double>(*current));
        }
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [write](double value) { write(reflect::Value{value}); });
        connect(spin, &QDoubleSpinBox::editingFinished, this, end);
        widget = spin;
        break;
    }
    case reflect::ValueKind::Text: {
        const std::string value = current && std::holds_alternative<std::string>(*current)
                                      ? std::get<std::string>(*current)
                                      : std::string();
        if (property.id == "image" || property.id == "tileset" || property.id == "sound") {
            // A picture or a sound: choose one of the project's by its path.
            const bool sound = property.id == "sound";
            auto* choose = new QComboBox(content_);
            choose->addItem(tr("(none)"), QString());
            for (const auto& [path, guid] : sound ? sounds(*document_) : images(*document_)) {
                choose->addItem(text_of(path), text_of(guid));
            }
            const int index = choose->findData(text_of(value));
            if (index >= 0) {
                choose->setCurrentIndex(index);
            } else if (!value.empty()) {
                choose->addItem(sound ? tr("(missing sound)") : tr("(missing picture)"),
                                text_of(value));
                choose->setCurrentIndex(choose->count() - 1);
            }
            connect(choose, qOverload<int>(&QComboBox::activated), this,
                    [write, end, choose](int i) {
                        write(reflect::Value{choose->itemData(i).toString().toStdString()});
                        end();
                    });
            widget = choose;
            break;
        }
        if (component.name == "sprite_animation" && property.id == "tag") {
            // The tags of the sprite's sheet, as drawn in Sprite Studio or Aseprite; another
            // name may still be typed (a tag not drawn yet).
            auto* choose = new QComboBox(content_);
            choose->setEditable(true);
            choose->addItem(QString());
            for (const std::string& tag : sheet_tags(*document_, entity)) {
                choose->addItem(text_of(tag));
            }
            choose->setCurrentText(text_of(value));
            choose->lineEdit()->setPlaceholderText(tr("(frames by number)"));
            const QString was = text_of(value);
            const auto choose_tag = [write, end, choose, was] {
                const QString now = choose->currentText().trimmed();
                if (now != was) {
                    write(reflect::Value{now.toStdString()});
                    end();
                }
            };
            connect(choose, qOverload<int>(&QComboBox::activated), this, choose_tag);
            connect(choose->lineEdit(), &QLineEdit::editingFinished, this, choose_tag);
            widget = choose;
            break;
        }
        if (component.name == "sprite" && property.id == "recolor") {
            // The swaps as text, and a button to pick them by eye.
            auto* row = new QWidget(content_);
            auto* across = new QHBoxLayout(row);
            across->setContentsMargins(0, 0, 0, 0);
            auto* line = new QLineEdit(text_of(value), row);
            line->setPlaceholderText(tr("(as drawn)"));
            auto* pick = new QPushButton(tr("Recolour…"), row);
            pick->setObjectName(QStringLiteral("recolor_button"));
            across->addWidget(line, 1);
            across->addWidget(pick);
            const QString was = text_of(value);
            connect(line, &QLineEdit::editingFinished, this, [write, end, line, was] {
                if (line->text() != was) {
                    write(reflect::Value{line->text().toStdString()});
                    end();
                }
            });
            connect(pick, &QPushButton::clicked, this, [this, write, end, entity, was] {
                const std::optional<engine::assets::Image> picture =
                    sprite_picture(*document_, entity);
                if (!picture) {
                    return;
                }
                RecolorDialog dialog(*picture, was.toStdString(), this);
                if (dialog.exec() == QDialog::Accepted &&
                    QString::fromStdString(dialog.text()) != was) {
                    write(reflect::Value{dialog.text()});
                    end();
                }
            });
            widget = row;
            break;
        }
        auto* line = new QLineEdit(text_of(value), content_);
        connect(line, &QLineEdit::editingFinished, this, [write, end, line, value] {
            if (line->text().toStdString() != value) {
                write(reflect::Value{line->text().toStdString()});
                end();
            }
        });
        widget = line;
        break;
    }
    case reflect::ValueKind::Choice: {
        auto* choose = new QComboBox(content_);
        if (property.choice != nullptr) {
            for (const reflect::ChoiceEntry& entry : property.choice->entries) {
                choose->addItem(text_of(entry.meta.display_name),
                                QVariant::fromValue(static_cast<qlonglong>(entry.value)));
            }
        }
        if (current && std::holds_alternative<std::int64_t>(*current)) {
            choose->setCurrentIndex(choose->findData(
                QVariant::fromValue(static_cast<qlonglong>(std::get<std::int64_t>(*current)))));
        }
        connect(choose, qOverload<int>(&QComboBox::activated), this, [write, end, choose](int i) {
            write(reflect::Value{static_cast<std::int64_t>(choose->itemData(i).toLongLong())});
            end();
        });
        widget = choose;
        break;
    }
    }
    widget->setObjectName(object_name);
    widget->setEnabled(!read_only);
    // Fields may shrink below their wish (a spin box with a wide range asks for room for
    // its longest number), so labels always keep theirs.
    widget->setMinimumWidth(72);
    return widget;
}

} // namespace editor
