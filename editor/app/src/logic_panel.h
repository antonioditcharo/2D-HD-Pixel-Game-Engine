#pragma once

// The Logic view: every logic file of the project as plain language, edited in place.
// Selecting a step shows the values written into it (the text said, the seconds waited)
// as fields that change it as you type. Add puts in a step (after the selected one, or
// first under a selected Rule, Sequence or Brain state), a condition, a Rule on any event,
// a Sequence, a Brain, a state, a "Go to", or a new logic file; Delete removes any of
// them; Up and Down move steps.
// Below, the compiler says whether the file still runs. Every edit is a step of the
// Document's undo.
//
// See docs/dev/editor.md.

#include <QWidget>

#include "editor/core/document.h"

class QLabel;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QFormLayout;

namespace editor {

class LogicPanel : public QWidget {
    Q_OBJECT

public:
    explicit LogicPanel(QWidget* parent = nullptr);

    /// Shows `document`'s logic (null shows nothing).
    void set_document(Document* document);
    /// Rebuilds the lines from the document (after undo, for example), keeping the
    /// selected block selected where it still exists.
    void rebuild();
    /// Selects the line showing `block` of the logic file at `path`.
    void select(std::string_view path, engine::logic::BlockId block);

    [[nodiscard]] QTreeWidget* tree() const noexcept { return tree_; }

Q_SIGNALS:
    /// The logic was changed here.
    void edited();
    /// An edit was refused, with why.
    void refused(const QString& message);

private:
    /// The logic file and block of the selected line; an invalid block for none.
    [[nodiscard]] std::pair<std::string, engine::logic::BlockId> current() const;
    void show_fields();
    void refresh_texts();
    void update_buttons();
    void fill_add_menu();
    /// After adding `block` (invalid, with `error`, if refused): shows and selects it.
    void added(const std::string& path, engine::logic::BlockId block, const std::string& error);
    void add_step(const LogicAction& action);
    void remove_step();
    void move_step(int by);

    Document* document_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QWidget* fields_ = nullptr;
    QFormLayout* form_ = nullptr;
    QToolButton* add_ = nullptr;
    QPushButton* delete_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QLabel* problems_ = nullptr;
};

} // namespace editor
