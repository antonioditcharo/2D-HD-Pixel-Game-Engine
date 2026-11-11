#include "inspector.h"

#include "engine/foundation/reflect.h"

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
#include <string>
#include <variant>

namespace editor {

namespace reflect = engine::reflect;

namespace {

QString text_of(const std::string& s) {
    return QString::fromStdString(s);
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

} // namespace

Inspector::Inspector(QWidget* parent) : QWidget(parent), layout_(new QVBoxLayout(this)) {
    layout_->setContentsMargins(4, 4, 4, 4);
}

void Inspector::show_entity(Document* document, engine::Guid entity) {
    document_ = document;
    entity_ = entity;
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
        if (document_->set_property(entity_, component, property, value, error)) {
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
        if (property.id == "image" || property.id == "tileset") {
            // A picture: choose one of the project's images by its path.
            auto* choose = new QComboBox(content_);
            choose->addItem(tr("(none)"), QString());
            for (const auto& [path, guid] : images(*document_)) {
                choose->addItem(text_of(path), text_of(guid));
            }
            const int index = choose->findData(text_of(value));
            if (index >= 0) {
                choose->setCurrentIndex(index);
            } else if (!value.empty()) {
                choose->addItem(tr("(missing picture)"), text_of(value));
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
