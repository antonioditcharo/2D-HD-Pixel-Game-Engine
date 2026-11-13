// The Sparxel guide inside the editor: every page built in, searched, shown, and linked.

#include <doctest/doctest.h>

#include <QListWidget>
#include <QTextBrowser>
#include <QUrl>

#include "help_window.h"

TEST_CASE("The guide: its pages are built in, searched by title first, and linked") {
    const std::vector<editor::HelpPage>& pages = editor::help_pages();
    CHECK(pages.size() >= 20); // every page of docs/dev
    const std::vector<const editor::HelpPage*> found = editor::search_help(QStringLiteral("shop"));
    REQUIRE_FALSE(found.empty());
    CHECK(found.front()->title == QStringLiteral("Shops"));
    CHECK(editor::search_help(QStringLiteral("shop gold")).size() <= found.size());
    CHECK(editor::search_help(QStringLiteral("zzzqqq")).empty());
    CHECK(editor::search_help({}).size() == pages.size());

    editor::HelpWindow window;
    CHECK(window.current() == QStringLiteral("editor.md"));
    window.search(QStringLiteral("auto-tile"));
    REQUIRE(window.page_list()->count() > 0);
    window.page_list()->setCurrentRow(0);
    CHECK_FALSE(window.current() == QStringLiteral("editor.md"));
    REQUIRE(window.show_page(QStringLiteral("shop.md")));
    CHECK(window.view()->toPlainText().contains(QStringLiteral("Shops")));
    CHECK_FALSE(window.show_page(QStringLiteral("nothing.md")));
    // A link to another page opens it here.
    Q_EMIT window.view()->anchorClicked(QUrl(QStringLiteral("battle.md")));
    CHECK(window.current() == QStringLiteral("battle.md"));
}
