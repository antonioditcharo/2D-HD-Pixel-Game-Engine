#include "help_window.h"

#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

// The guide's pages are a Qt resource of this static library: registered by hand, from the
// global namespace, before the first use.
static void register_guide() {
    static const bool done = [] {
        Q_INIT_RESOURCE(guide);
        return true;
    }();
    static_cast<void>(done);
}

namespace editor {

const std::vector<HelpPage>& help_pages() {
    static const std::vector<HelpPage> pages = [] {
        register_guide();
        std::vector<HelpPage> out;
        const QDir dir(QStringLiteral(":/guide"));
        for (const QString& file : dir.entryList({QStringLiteral("*.md")}, QDir::Files)) {
            QFile in(dir.filePath(file));
            if (!in.open(QIODevice::ReadOnly)) {
                continue;
            }
            HelpPage page{file, file, QString::fromUtf8(in.readAll())};
            for (const QString& line : page.text.split(QLatin1Char('\n'))) {
                if (line.startsWith(QStringLiteral("# "))) {
                    page.title = line.mid(2).trimmed();
                    break;
                }
            }
            out.push_back(std::move(page));
        }
        std::sort(out.begin(), out.end(), [](const HelpPage& a, const HelpPage& b) {
            return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
        });
        return out;
    }();
    return pages;
}

std::vector<const HelpPage*> search_help(const QString& query) {
    const QStringList words = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    std::vector<std::pair<int, const HelpPage*>> found;
    for (const HelpPage& page : help_pages()) {
        int score = 0;
        bool all = true;
        for (const QString& word : words) {
            const int in_title = static_cast<int>(page.title.count(word, Qt::CaseInsensitive));
            const int in_text = static_cast<int>(page.text.count(word, Qt::CaseInsensitive));
            if (in_title == 0 && in_text == 0) {
                all = false;
                break;
            }
            score += (in_title * 100) + std::min(in_text, 50);
        }
        if (all) {
            found.emplace_back(score, &page);
        }
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const HelpPage*> out;
    out.reserve(found.size());
    for (const auto& [score, page] : found) {
        out.push_back(page);
    }
    return out;
}

HelpWindow::HelpWindow(QWidget* parent) : QMainWindow(parent) {
    register_guide();
    setWindowTitle(tr("Sparxel guide"));
    setObjectName(QStringLiteral("help_window"));
    resize(1000, 700);
    auto* split = new QSplitter(this);
    auto* side = new QWidget(split);
    auto* column = new QVBoxLayout(side);
    column->setContentsMargins(4, 4, 4, 4);
    field_ = new QLineEdit(side);
    field_->setObjectName(QStringLiteral("help_search"));
    field_->setPlaceholderText(tr("Search the guide"));
    field_->setClearButtonEnabled(true);
    list_ = new QListWidget(side);
    list_->setObjectName(QStringLiteral("help_pages"));
    column->addWidget(field_);
    column->addWidget(list_);
    view_ = new QTextBrowser(split);
    view_->setObjectName(QStringLiteral("help_view"));
    view_->setOpenLinks(false);
    view_->setSearchPaths({QStringLiteral(":/guide")});
    split->addWidget(side);
    split->addWidget(view_);
    split->setStretchFactor(1, 3);
    setCentralWidget(split);

    connect(field_, &QLineEdit::textChanged, this, &HelpWindow::search);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (const QListWidgetItem* item = list_->item(row)) {
            show_page(item->data(Qt::UserRole).toString());
        }
    });
    // Links to other pages open them here; anchors within a page scroll; the rest (code)
    // is not part of the guide.
    connect(view_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        const QString target = url.toString();
        const QString file = target.section(QLatin1Char('#'), 0, 0).section(QLatin1Char('/'), -1);
        if (target.startsWith(QLatin1Char('#'))) {
            view_->scrollToAnchor(target.mid(1));
        } else if (file.endsWith(QStringLiteral(".md"))) {
            show_page(file);
        }
    });
    search({});
    show_page(QStringLiteral("editor.md"));
}

void HelpWindow::search(const QString& query) {
    const QSignalBlocker quiet(list_);
    list_->clear();
    for (const HelpPage* page : search_help(query)) {
        auto* item = new QListWidgetItem(page->title, list_);
        item->setData(Qt::UserRole, page->file);
        item->setToolTip(page->file);
    }
}

bool HelpWindow::show_page(const QString& file) {
    const auto& pages = help_pages();
    const auto page = std::find_if(pages.begin(), pages.end(),
                                   [&file](const HelpPage& p) { return p.file == file; });
    if (page == pages.end()) {
        return false;
    }
    current_ = file;
    view_->setMarkdown(page->text);
    setWindowTitle(tr("%1 - Sparxel guide").arg(page->title));
    return true;
}

} // namespace editor
