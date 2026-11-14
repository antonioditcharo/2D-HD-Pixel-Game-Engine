#pragma once

// The Sparxel guide (plan/02 Q4): the documentation, built into the editor so it works
// offline, with a search over every page. Pages are Markdown, shown with their pictures;
// links between pages open them here.

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <vector>

class QLineEdit;
class QListWidget;
class QTextBrowser;

namespace editor {

/// One page of the guide.
struct HelpPage {
    QString file;  ///< "shop.md"
    QString title; ///< Its first heading: "Shops".
    QString text;  ///< Its Markdown.
};

/// Every page built into the editor, in order of title.
[[nodiscard]] const std::vector<HelpPage>& help_pages();
/// The pages that have every word of `query` (ignoring case), the best first: words in the
/// title count most, then how often they appear. Every page for an empty query.
[[nodiscard]] std::vector<const HelpPage*> search_help(const QString& query);

class HelpWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit HelpWindow(QWidget* parent = nullptr);

    /// Shows a page by its file ("shop.md"). False if there is none.
    bool show_page(const QString& file);
    /// Lists the pages matching `query` (search_help()).
    void search(const QString& query);
    [[nodiscard]] QListWidget* page_list() const noexcept { return list_; }
    [[nodiscard]] QTextBrowser* view() const noexcept { return view_; }
    [[nodiscard]] const QString& current() const noexcept { return current_; }

private:
    QLineEdit* field_ = nullptr;
    QListWidget* list_ = nullptr;
    QTextBrowser* view_ = nullptr;
    QString current_;
};

} // namespace editor
