#include "logic_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <climits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>

#include "editor/core/logic_text.h"

namespace editor {

namespace reflect = engine::reflect;
using engine::logic::BlockId;

namespace {

constexpr int block_role = Qt::UserRole;
constexpr int path_role = Qt::UserRole + 1;
constexpr int otherwise_role = Qt::UserRole + 2; ///< An "Otherwise:" line.

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

    // The selected file's variables.
    variables_ = new QGroupBox(tr("Variables"), this);
    variables_->setObjectName(QStringLiteral("logic_variables"));
    variables_form_ = new QFormLayout(variables_);
    variables_form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(variables_);

    auto* buttons = new QHBoxLayout();
    add_ = new QToolButton(this);
    add_->setObjectName(QStringLiteral("logic_add"));
    add_->setText(tr("Add"));
    add_->setToolTip(tr("Add a step (after the selected one, or first under the selected "
                        "Rule, Sequence or state), a condition to the selected Rule or "
                        "\u201cGo to\u201d, a Rule, a Sequence, a Brain, a state, a "
                        "\u201cGo to\u201d, or a new logic file."));
    add_->setPopupMode(QToolButton::InstantPopup);
    add_->setMenu(new QMenu(add_));
    delete_ = new QPushButton(tr("Delete"), this);
    delete_->setObjectName(QStringLiteral("logic_delete"));
    up_ = new QPushButton(tr("Up"), this);
    up_->setObjectName(QStringLiteral("logic_up"));
    down_ = new QPushButton(tr("Down"), this);
    down_->setObjectName(QStringLiteral("logic_down"));
    not_ = new QPushButton(tr("Not"), this);
    not_->setObjectName(QStringLiteral("logic_not"));
    not_->setToolTip(tr("Turn the selected condition into its opposite, or back."));
    for (QWidget* button :
         {static_cast<QWidget*>(add_), static_cast<QWidget*>(delete_), static_cast<QWidget*>(up_),
          static_cast<QWidget*>(down_), static_cast<QWidget*>(not_)}) {
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);
    any_ = new QCheckBox(tr("Run when any of its conditions holds (not all)"), this);
    any_->setObjectName(QStringLiteral("logic_any"));
    layout->addWidget(any_);

    problems_ = new QLabel(this);
    problems_->setObjectName(QStringLiteral("logic_problems"));
    problems_->setWordWrap(true);
    layout->addWidget(problems_);

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        show_fields();
        update_buttons();
    });
    connect(add_->menu(), &QMenu::aboutToShow, this, [this] { fill_add_menu(); });
    connect(delete_, &QPushButton::clicked, this, [this] { remove_step(); });
    connect(up_, &QPushButton::clicked, this, [this] { move_step(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move_step(1); });
    connect(not_, &QPushButton::clicked, this, [this] {
        const auto [path, block] = current();
        if (document_ != nullptr && document_->toggle_logic_not(path, block)) {
            rebuild();
            Q_EMIT edited();
        }
    });
    connect(any_, &QCheckBox::clicked, this, [this](bool on) {
        const auto [path, block] = current();
        std::string error;
        if (document_ == nullptr || !document_->set_logic_any(path, block, on, error)) {
            Q_EMIT refused(text_of(error));
            update_buttons(); // back as it was
            return;
        }
        rebuild();
        Q_EMIT edited();
    });
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
                    item->setData(0, otherwise_role, line.otherwise);
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
    if (!path.empty()) {
        select(path, block); // a file's own line has no block
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

void LogicPanel::show_variables() {
    while (variables_form_->rowCount() > 0) {
        variables_form_->removeRow(0);
    }
    if (document_ == nullptr) {
        variables_->hide();
        return;
    }
    const std::string path = current().first;
    const auto file = std::find_if(document_->logic().begin(), document_->logic().end(),
                                   [&path](const LogicFile& f) { return f.path == path; });
    variables_->setVisible(file != document_->logic().end());
    if (file == document_->logic().end()) {
        return;
    }
    const std::string at = path;
    // Rebuilt later, not from inside the signal of a field that goes with it.
    const auto later = [this, at] {
        QTimer::singleShot(0, this, [this, at] {
            rebuild();
            Q_EMIT edited();
        });
    };
    for (const engine::logic::Variable& v : file->asset.variables) {
        const std::string name = v.id;
        auto* row = new QWidget(variables_);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        auto* rename = new QLineEdit(text_of(name), row);
        rename->setObjectName(text_of("variable_name:" + name));
        connect(rename, &QLineEdit::editingFinished, this, [this, at, name, rename, later] {
            const std::string wanted = utf8(rename->text());
            if (wanted == name) {
                return;
            }
            std::string error;
            if (!document_->rename_logic_variable(at, name, wanted, error)) {
                Q_EMIT refused(text_of(error));
                rename->setText(text_of(name));
                return;
            }
            later();
        });
        line->addWidget(rename, 1);
        const auto start = [this, at, name](const reflect::Value& value) {
            std::string error;
            if (!document_->set_logic_variable_start(at, name, value, error)) {
                Q_EMIT refused(text_of(error));
                return;
            }
            Q_EMIT edited();
        };
        QWidget* field = nullptr;
        const reflect::Value initial = v.initial.value_or(reflect::Value{false});
        switch (v.kind) {
        case reflect::ValueKind::Bool: {
            auto* tick = new QCheckBox(row);
            const auto* on = std::get_if<bool>(&initial);
            tick->setChecked(on != nullptr && *on);
            connect(tick, &QCheckBox::toggled, this,
                    [start](bool yes) { start(reflect::Value{yes}); });
            field = tick;
            break;
        }
        case reflect::ValueKind::Int: {
            auto* spin = new QSpinBox(row);
            spin->setRange(-1000000, 1000000);
            const auto* number = std::get_if<std::int64_t>(&initial);
            spin->setValue(number != nullptr ? static_cast<int>(*number) : 0);
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                    [start](int n) { start(reflect::Value{std::int64_t{n}}); });
            field = spin;
            break;
        }
        case reflect::ValueKind::Float: {
            auto* spin = new QDoubleSpinBox(row);
            spin->setRange(-1.0e6, 1.0e6);
            spin->setDecimals(3);
            const auto* number = std::get_if<double>(&initial);
            spin->setValue(number != nullptr ? *number : 0.0);
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [start](double n) { start(reflect::Value{n}); });
            field = spin;
            break;
        }
        case reflect::ValueKind::Text:
        case reflect::ValueKind::Choice: {
            const auto* words = std::get_if<std::string>(&initial);
            auto* edit = new QLineEdit(words != nullptr ? text_of(*words) : QString(), row);
            connect(edit, &QLineEdit::textEdited, this,
                    [start](const QString& text) { start(reflect::Value{utf8(text)}); });
            field = edit;
            break;
        }
        }
        field->setObjectName(text_of("variable_start:" + name));
        field->setToolTip(tr("What it holds when the game starts."));
        line->addWidget(field, 1);
        auto* remove = new QPushButton(tr("Delete"), row);
        remove->setObjectName(text_of("variable_delete:" + name));
        connect(remove, &QPushButton::clicked, this, [this, at, name, later] {
            std::string error;
            if (!document_->remove_logic_variable(at, name, error)) {
                Q_EMIT refused(text_of(error));
                return;
            }
            later();
        });
        line->addWidget(remove);
        variables_form_->addRow(row);
    }
    if (file->asset.variables.empty()) {
        variables_form_->addRow(new QLabel(tr("None yet: use Add, then Variable."), variables_));
    }
}

void LogicPanel::show_fields() {
    show_variables();
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
    add_->setEnabled(document_ != nullptr);
    delete_->setEnabled(open && document_->is_logic_block(path, block));
    up_->setEnabled(step);
    down_->setEnabled(step);
    not_->setEnabled(open && document_->is_logic_test(path, block));
    const std::optional<bool> any = open ? document_->logic_any(path, block) : std::nullopt;
    any_->setVisible(any.has_value());
    {
        const QSignalBlocker quiet(any_);
        any_->setChecked(any.value_or(false));
    }
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

void LogicPanel::fill_add_menu() {
    QMenu* menu = add_->menu();
    menu->clear();
    if (document_ == nullptr) {
        return;
    }
    // Lists of things by object: a submenu each, the game's own first.
    const auto grouped = [](QMenu* into, const std::string& object) {
        return object.empty() ? into : into->addMenu(text_of(object));
    };
    QMenu* steps = menu->addMenu(tr("Step"));
    std::map<std::string, QMenu*> by_object;
    for (const LogicAction& action : document_->logic_actions()) {
        QMenu*& sub = by_object[action.object];
        if (sub == nullptr) {
            sub = grouped(steps, action.object);
        }
        QAction* item = sub->addAction(text_of(action.text));
        item->setObjectName(text_of("action:" + action.object + "/" + action.member));
        connect(item, &QAction::triggered, this, [this, action] { add_step(action); });
    }
    // The same steps, into the Otherwise of the selected If or Rule.
    QMenu* otherwise = menu->addMenu(tr("Otherwise"));
    by_object.clear();
    for (const LogicAction& action : document_->logic_actions()) {
        QMenu*& sub = by_object[action.object];
        if (sub == nullptr) {
            sub = grouped(otherwise, action.object);
        }
        QAction* item = sub->addAction(text_of(action.text));
        item->setObjectName(text_of("otherwise:" + action.object + "/" + action.member));
        connect(item, &QAction::triggered, this, [this, action] { add_step(action, true); });
    }
    // If and Wait until, each with the condition it tests.
    for (const bool wait_until : {false, true}) {
        QMenu* branch = steps->addMenu(wait_until ? tr("Wait until") : tr("If"));
        std::map<std::string, QMenu*> objects;
        for (const LogicAction& condition : document_->logic_conditions()) {
            QMenu*& sub = objects[condition.object];
            if (sub == nullptr) {
                sub = grouped(branch, condition.object);
            }
            QAction* item = sub->addAction(text_of(condition.text));
            item->setObjectName(text_of((wait_until ? "wait_until:" : "if:") + condition.object +
                                        "/" + condition.member));
            connect(item, &QAction::triggered, this, [this, condition, wait_until] {
                const auto [path, block] = current();
                std::string error;
                added(path, document_->add_logic_branch(path, block, condition, wait_until, error),
                      error);
            });
        }
    }
    // Variables: new ones by kind, and comparisons of the file's own.
    QMenu* variable = menu->addMenu(tr("Variable"));
    for (const auto& [kind, label, id] :
         {std::tuple{reflect::ValueKind::Int, tr("Whole number"), "int"},
          std::tuple{reflect::ValueKind::Float, tr("Number"), "float"},
          std::tuple{reflect::ValueKind::Bool, tr("Yes or no"), "bool"},
          std::tuple{reflect::ValueKind::Text, tr("Text"), "text"}}) {
        QAction* item = variable->addAction(label);
        item->setObjectName(QStringLiteral("variable:") + QString::fromLatin1(id));
        const reflect::ValueKind made = kind;
        connect(item, &QAction::triggered, this, [this, made] {
            const auto [path, block] = current();
            std::string error;
            if (!document_->add_logic_variable(path, made, error)) {
                Q_EMIT refused(
                    text_of(error.empty() ? std::string("Select a logic file first.") : error));
                return;
            }
            rebuild();
            Q_EMIT edited();
        });
    }
    QMenu* compare = menu->addMenu(tr("Compare a variable"));
    {
        const std::string path = current().first;
        const auto file = std::find_if(document_->logic().begin(), document_->logic().end(),
                                       [&path](const LogicFile& f) { return f.path == path; });
        if (file != document_->logic().end()) {
            for (const engine::logic::Variable& v : file->asset.variables) {
                QAction* item = compare->addAction(text_of(v.id));
                item->setObjectName(text_of("compare:" + v.id));
                const std::string name = v.id;
                connect(item, &QAction::triggered, this, [this, name] {
                    const auto [at, selected] = current();
                    std::string error;
                    added(at, document_->add_logic_comparison(at, selected, name, error), error);
                });
            }
        }
        compare->setEnabled(!compare->isEmpty());
    }
    QMenu* conditions = menu->addMenu(tr("Condition"));
    by_object.clear();
    for (const LogicAction& condition : document_->logic_conditions()) {
        QMenu*& sub = by_object[condition.object];
        if (sub == nullptr) {
            sub = grouped(conditions, condition.object);
        }
        QAction* item = sub->addAction(text_of(condition.text));
        item->setObjectName(text_of("condition:" + condition.object + "/" + condition.member));
        connect(item, &QAction::triggered, this, [this, condition] {
            const auto [path, block] = current();
            std::string error;
            added(path, document_->add_logic_condition(path, block, condition, error), error);
        });
    }
    QMenu* rules = menu->addMenu(tr("Rule"));
    by_object.clear();
    for (const LogicEvent& event : document_->logic_events()) {
        QMenu*& sub = by_object[event.object];
        if (sub == nullptr) {
            sub = grouped(rules, event.object);
        }
        QAction* item = sub->addAction(text_of(event.text));
        item->setObjectName(text_of("rule:" + event.object + "/" + event.event));
        connect(item, &QAction::triggered, this, [this, event] {
            const auto [path, block] = current();
            std::string error;
            added(path, document_->add_logic_rule(path, event, error), error);
        });
    }
    menu->addSeparator();
    // Blocks that need only the file, or the Brain or state selected.
    using Adder = engine::logic::BlockId (Document::*)(std::string_view, engine::logic::BlockId,
                                                       std::string&);
    const auto in_file =
        [this, menu](const QString& text, const char* name,
                     engine::logic::BlockId (Document::*add)(std::string_view, std::string&)) {
            QAction* item = menu->addAction(text);
            item->setObjectName(QString::fromLatin1(name));
            connect(item, &QAction::triggered, this, [this, add] {
                const auto [path, block] = current();
                std::string error;
                added(path, (document_->*add)(path, error), error);
            });
        };
    const auto under = [this, menu](const QString& text, const char* name, Adder add) {
        QAction* item = menu->addAction(text);
        item->setObjectName(QString::fromLatin1(name));
        connect(item, &QAction::triggered, this, [this, add] {
            const auto [path, block] = current();
            std::string error;
            added(path, (document_->*add)(path, block, error), error);
        });
    };
    in_file(tr("Sequence"), "add_sequence", &Document::add_logic_sequence);
    in_file(tr("Brain"), "add_brain", &Document::add_logic_brain);
    under(tr("State (in the selected Brain)"), "add_state", &Document::add_logic_state);
    under(tr("\u201cGo to\u201d (out of the selected state)"), "add_transition",
          &Document::add_logic_transition);
    menu->addSeparator();
    QAction* file = menu->addAction(tr("New logic file"));
    file->setObjectName(QStringLiteral("add_file"));
    connect(file, &QAction::triggered, this, [this] {
        // logic/logic_1.drules, or the next number free.
        for (int n = 1;; ++n) {
            const std::string path = "logic/logic_" + std::to_string(n) + ".drules";
            std::string error;
            if (document_->create_logic_file(path, error)) {
                rebuild();
                select(path, engine::logic::BlockId{});
                Q_EMIT edited();
                return;
            }
            if (!error.starts_with("There is already")) {
                Q_EMIT refused(text_of(error));
                return;
            }
        }
    });
}

void LogicPanel::added(const std::string& path, engine::logic::BlockId block,
                       const std::string& error) {
    if (!block.is_valid()) {
        Q_EMIT refused(text_of(error.empty() ? std::string("Select a logic file first.") : error));
        return;
    }
    rebuild();
    select(path, block);
    Q_EMIT edited();
}

void LogicPanel::add_step(const LogicAction& action, bool otherwise) {
    const auto [path, block] = current();
    if (document_ == nullptr) {
        return;
    }
    // On an "Otherwise:" line, steps go into that Otherwise.
    const QList<QTreeWidgetItem*> chosen = tree_->selectedItems();
    const bool on_otherwise = !chosen.isEmpty() && chosen.front()->data(0, otherwise_role).toBool();
    std::string error;
    const BlockId added =
        document_->add_logic_step(path, block, action, error, otherwise || on_otherwise);
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
    if (document_ != nullptr && document_->remove_logic_block(path, block)) {
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
