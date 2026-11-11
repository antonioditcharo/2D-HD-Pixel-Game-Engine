#include "logic_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <map>
#include <string>
#include <variant>

#include "editor/core/logic_text.h"

namespace editor {

namespace reflect = engine::reflect;
using engine::logic::BlockId;

namespace {

constexpr int block_role = Qt::UserRole;
constexpr int path_role = Qt::UserRole + 1;

QString text_of(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

std::string utf8(const QString& s) {
    const QByteArray bytes = s.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

BlockId block_of(const QTreeWidgetItem* item) {
    return BlockId{item->data(0, block_role).toULongLong()};
}

} // namespace

LogicPanel::LogicPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("logic"));
    tree_->setHeaderHidden(true);
    tree_->setAccessibleName(tr("Logic"));
    tree_->setWordWrap(true);
    layout->addWidget(tree_, 1);

    // The selected step's values.
    fields_ = new QWidget(this);
    fields_->setObjectName(QStringLiteral("logic_fields"));
    form_ = new QFormLayout(fields_);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(fields_);

    auto* buttons = new QHBoxLayout();
    add_ = new QToolButton(this);
    add_->setObjectName(QStringLiteral("logic_add"));
    add_->setText(tr("Add step"));
    add_->setToolTip(tr("Add a step after the selected one, or first under the selected "
                        "Rule, Sequence or Brain state."));
    add_->setPopupMode(QToolButton::InstantPopup);
    add_->setMenu(new QMenu(add_));
    delete_ = new QPushButton(tr("Delete"), this);
    delete_->setObjectName(QStringLiteral("logic_delete"));
    up_ = new QPushButton(tr("Up"), this);
    up_->setObjectName(QStringLiteral("logic_up"));
    down_ = new QPushButton(tr("Down"), this);
    down_->setObjectName(QStringLiteral("logic_down"));
    for (QWidget* button : {static_cast<QWidget*>(add_), static_cast<QWidget*>(delete_),
                            static_cast<QWidget*>(up_), static_cast<QWidget*>(down_)}) {
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);

    problems_ = new QLabel(this);
    problems_->setObjectName(QStringLiteral("logic_problems"));
    problems_->setWordWrap(true);
    layout->addWidget(problems_);

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        show_fields();
        update_buttons();
    });
    connect(add_->menu(), &QMenu::aboutToShow, this, [this] {
        // The actions of the game's objects and of the scene's entities, by object.
        QMenu* menu = add_->menu();
        menu->clear();
        if (document_ == nullptr) {
            return;
        }
        std::map<std::string, QMenu*> by_object;
        for (const LogicAction& action : document_->logic_actions()) {
            QMenu*& sub = by_object[action.object];
            if (sub == nullptr) {
                sub = menu->addMenu(text_of(action.object));
            }
            QAction* item = sub->addAction(text_of(action.text));
            item->setObjectName(text_of("action:" + action.object + "/" + action.member));
            connect(item, &QAction::triggered, this, [this, action] { add_step(action); });
        }
    });
    connect(delete_, &QPushButton::clicked, this, [this] { remove_step(); });
    connect(up_, &QPushButton::clicked, this, [this] { move_step(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move_step(1); });
    update_buttons();
}

void LogicPanel::set_document(Document* document) {
    document_ = document;
    rebuild();
}

std::pair<std::string, BlockId> LogicPanel::current() const {
    const QList<QTreeWidgetItem*> chosen = tree_->selectedItems();
    if (chosen.isEmpty()) {
        return {};
    }
    return {utf8(chosen.front()->data(0, path_role).toString()), block_of(chosen.front())};
}

void LogicPanel::rebuild() {
    const auto [path, block] = current();
    {
        const QSignalBlocker quiet(tree_);
        tree_->clear();
        if (document_ != nullptr) {
            std::vector<engine::serialize::Issue> issues;
            for (const LogicFile& file : logic_files(*document_, issues)) {
                auto* top = new QTreeWidgetItem(QStringList{text_of(file.path)});
                top->setData(0, path_role, text_of(file.path));
                tree_->addTopLevelItem(top);
                // Each line nests under the last line one level up.
                std::vector<QTreeWidgetItem*> parents{top};
                for (const LogicLine& line : describe(file.asset, *document_)) {
                    const auto depth = static_cast<std::size_t>(line.depth);
                    parents.resize(std::min(parents.size(), depth + 1));
                    auto* item = new QTreeWidgetItem(QStringList{text_of(line.text)});
                    item->setData(0, block_role,
                                  QVariant::fromValue(static_cast<qulonglong>(line.block.value)));
                    item->setData(0, path_role, text_of(file.path));
                    parents.back()->addChild(item);
                    parents.push_back(item);
                }
            }
            for (const engine::serialize::Issue& issue : issues) {
                tree_->addTopLevelItem(new QTreeWidgetItem(QStringList{
                    text_of(issue.path) + QStringLiteral(": ") + text_of(issue.message)}));
            }
        }
        tree_->expandAll();
    }
    if (block.is_valid()) {
        select(path, block);
    }
    show_fields();
    update_buttons();
}

void LogicPanel::select(std::string_view path, BlockId block) {
    const QString wanted_path = text_of(std::string(path));
    for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
        if ((*it)->data(0, path_role).toString() == wanted_path && block_of(*it) == block) {
            tree_->setCurrentItem(*it);
            tree_->scrollToItem(*it);
            return;
        }
    }
}

void LogicPanel::refresh_texts() {
    // A value changed: the lines are the same blocks in the same order, so only their
    // texts change, and the field being typed in stays where it is.
    if (document_ == nullptr) {
        return;
    }
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree_->topLevelItem(i);
        const std::string path = utf8(top->data(0, path_role).toString());
        const auto file = std::find_if(document_->logic().begin(), document_->logic().end(),
                                       [&path](const LogicFile& f) { return f.path == path; });
        if (file == document_->logic().end()) {
            continue;
        }
        const std::vector<LogicLine> lines = describe(file->asset, *document_);
        std::size_t next = 0;
        for (QTreeWidgetItemIterator it(top); *it != nullptr && next <= lines.size(); ++it) {
            if (*it == top) {
                continue;
            }
            if ((*it)->parent() == nullptr) {
                break; // the next file
            }
            if (next < lines.size()) {
                (*it)->setText(0, text_of(lines[next].text));
            }
            ++next;
        }
    }
}

void LogicPanel::show_fields() {
    // Old fields go now, so lookups by name find only the new ones.
    while (form_->rowCount() > 0) {
        form_->removeRow(0);
    }
    const auto [path, block] = current();
    if (document_ == nullptr || !block.is_valid()) {
        return;
    }
    const std::string file = path;
    const auto write = [this, file, block](std::size_t argument, const reflect::Value& value) {
        std::string error;
        if (!document_->set_logic_slot(file, block, argument, value, error)) {
            Q_EMIT refused(text_of(error));
            return;
        }
        refresh_texts();
        update_buttons();
        Q_EMIT edited();
    };
    const auto end = [this] {
        if (document_ != nullptr) {
            document_->end_merge();
        }
    };
    for (const LogicSlot& slot : document_->logic_slots(file, block)) {
        const std::size_t argument = slot.argument;
        const QString name = QStringLiteral("logic_slot:%1").arg(argument);
        QWidget* field = nullptr;
        switch (slot.kind) {
        case reflect::ValueKind::Text: {
            const auto* value = std::get_if<std::string>(&slot.value);
            auto* edit = new QLineEdit(value != nullptr ? text_of(*value) : QString(), fields_);
            connect(edit, &QLineEdit::textEdited, this, [write, argument](const QString& text) {
                write(argument, reflect::Value{utf8(text)});
            });
            connect(edit, &QLineEdit::editingFinished, this, end);
            field = edit;
            break;
        }
        case reflect::ValueKind::Float: {
            auto* spin = new QDoubleSpinBox(fields_);
            spin->setDecimals(3);
            spin->setRange(slot.limits.min.value_or(-1.0e6), slot.limits.max.value_or(1.0e6));
            spin->setSingleStep(slot.limits.step > 0.0 ? slot.limits.step : 0.25);
            if (const auto* d = std::get_if<double>(&slot.value)) {
                spin->setValue(*d);
            } else if (const auto* i = std::get_if<std::int64_t>(&slot.value)) {
                spin->setValue(static_cast<double>(*i));
            }
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [write, argument](double v) { write(argument, reflect::Value{v}); });
            connect(spin, &QDoubleSpinBox::editingFinished, this, end);
            field = spin;
            break;
        }
        case reflect::ValueKind::Int: {
            auto* spin = new QSpinBox(fields_);
            spin->setRange(
                static_cast<int>(std::max(slot.limits.min.value_or(-1.0e6), double{INT_MIN})),
                static_cast<int>(std::min(slot.limits.max.value_or(1.0e6), double{INT_MAX})));
            if (const auto* i = std::get_if<std::int64_t>(&slot.value)) {
                spin->setValue(static_cast<int>(*i));
            }
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                    [write, argument](int v) { write(argument, reflect::Value{std::int64_t{v}}); });
            connect(spin, &QSpinBox::editingFinished, this, end);
            field = spin;
            break;
        }
        case reflect::ValueKind::Bool: {
            auto* tick = new QCheckBox(fields_);
            const auto* yes = std::get_if<bool>(&slot.value);
            tick->setChecked(yes != nullptr && *yes);
            connect(tick, &QCheckBox::toggled, this, [write, end, argument](bool on) {
                write(argument, reflect::Value{on});
                end();
            });
            field = tick;
            break;
        }
        case reflect::ValueKind::Choice: {
            auto* choose = new QComboBox(fields_);
            if (slot.choice != nullptr) {
                for (const reflect::ChoiceEntry& entry : slot.choice->entries) {
                    choose->addItem(text_of(entry.meta.display_name), text_of(entry.id));
                }
                const auto* id = std::get_if<std::string>(&slot.value);
                const auto* number = std::get_if<std::int64_t>(&slot.value);
                const reflect::ChoiceEntry* entry = id != nullptr ? slot.choice->find_entry(*id)
                                                    : number != nullptr
                                                        ? slot.choice->find_entry(*number)
                                                        : nullptr;
                if (entry != nullptr) {
                    choose->setCurrentIndex(choose->findData(text_of(entry->id)));
                }
            }
            connect(choose, qOverload<int>(&QComboBox::activated), this,
                    [write, end, choose, argument](int i) {
                        write(argument, reflect::Value{utf8(choose->itemData(i).toString())});
                        end();
                    });
            field = choose;
            break;
        }
        }
        field->setObjectName(name);
        field->setMinimumWidth(72);
        form_->addRow(text_of(slot.label), field);
    }
}

void LogicPanel::update_buttons() {
    const auto [path, block] = current();
    const bool open = document_ != nullptr && !path.empty();
    const bool step = open && document_->is_logic_step(path, block);
    add_->setEnabled(open && block.is_valid());
    delete_->setEnabled(step);
    up_->setEnabled(step);
    down_->setEnabled(step);
    if (!open) {
        problems_->clear();
        return;
    }
    const std::vector<engine::logic::Diagnostic> found = document_->check_logic(path);
    if (found.empty()) {
        problems_->setText(tr("%1 runs: no problems.").arg(text_of(path)));
    } else {
        problems_->setText(tr("%1 does not run (%2 problem(s)). First: %3")
                               .arg(text_of(path))
                               .arg(found.size())
                               .arg(text_of(found.front().message)));
    }
}

void LogicPanel::add_step(const LogicAction& action) {
    const auto [path, block] = current();
    if (document_ == nullptr) {
        return;
    }
    std::string error;
    const BlockId added = document_->add_logic_step(path, block, action, error);
    if (!added.is_valid()) {
        Q_EMIT refused(text_of(error));
        return;
    }
    rebuild();
    select(path, added);
    Q_EMIT edited();
}

void LogicPanel::remove_step() {
    const auto [path, block] = current();
    if (document_ != nullptr && document_->remove_logic_step(path, block)) {
        rebuild();
        Q_EMIT edited();
    }
}

void LogicPanel::move_step(int by) {
    const auto [path, block] = current();
    if (document_ != nullptr && document_->move_logic_step(path, block, by)) {
        rebuild(); // keeps the moved step selected
        Q_EMIT edited();
    }
}

} // namespace editor
