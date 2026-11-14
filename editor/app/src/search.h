#pragma once

// Search everything (plan/06 §1, principle 7): one box, Ctrl+K, that finds the editor's
// commands, the project's assets and the scene's entities as you type, as the command
// palettes of code editors and engines do. Letters match in order, not only side by side
// ("vlg" finds "village"), best matches first; Enter runs the chosen one.
//
// See docs/dev/editor.md.

#include <QDialog>
#include <QString>
#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;

namespace editor {

/// One thing search can find, and what choosing it does.
struct SearchItem {
    QString text; ///< What it is called, as searched and shown.
    QString kind; ///< "Command", "Scene", "Picture", "Entity"...: shown beside it.
    std::function<void()> run;
};

/// How well `query` matches `text`: below 0 if it does not (its letters, ignoring case, are
/// not all in `text` in order). Higher is better: matches at the start of the text and of
/// words, letters side by side, and shorter texts score more.
[[nodiscard]] int search_score(const QString& query, const QString& text);

class SearchBox : public QDialog {
    Q_OBJECT

public:
    explicit SearchBox(QWidget* parent = nullptr);

    /// What can be found, and shows them all (empty query).
    void set_items(std::vector<SearchItem> items);
    [[nodiscard]] QLineEdit* field() const noexcept { return field_; }
    [[nodiscard]] QListWidget* list() const noexcept { return list_; }
    /// Runs the chosen item (closing the box first). False if none is chosen.
    bool run_current();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void filter(const QString& query);

    QLineEdit* field_ = nullptr;
    QListWidget* list_ = nullptr;
    std::vector<SearchItem> items_;
};

} // namespace editor
