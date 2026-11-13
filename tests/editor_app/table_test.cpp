// The table window, driven as a person would on Qt's offscreen platform.

#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <QApplication>
#include <QLineEdit>
#include <QTableWidget>
#include <filesystem>
#include <fstream>
#include <string>

#include "main_window.h"
#include "table_window.h"

namespace fs = std::filesystem;
namespace game = engine::game;
using engine::Guid;

TEST_CASE("File > New table opens a grid: typed into, worked out, saved, and to and from CSV") {
    const fs::path path =
        fs::temp_directory_path() / ("sparxel-table-app-" + Guid::generate().to_string());
    fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", path, fs::copy_options::recursive);
    {
        editor::MainWindow window(SPARXEL_PLAYER);
        window.show();
        REQUIRE(window.open_project(path));
        editor::TableWindow* table = window.new_table(QStringLiteral("enemies"));
        REQUIRE(table != nullptr);
        QApplication::processEvents();
        CHECK(fs::exists(path / "assets" / "data" / "enemies.dtable"));
        QTableWidget* grid = table->grid();
        REQUIRE(grid->rowCount() == 1);
        CHECK(grid->columnCount() == 2); // the row's name, and "name"
        // Typing into the grid: a cell, then the row's name.
        grid->item(0, 1)->setText(QStringLiteral("Slime"));
        QApplication::processEvents();
        CHECK(table->editor()->table().text("first", "name") ==
              std::optional<std::string>("Slime"));
        grid->item(0, 0)->setText(QStringLiteral("slime"));
        QApplication::processEvents();
        CHECK(table->editor()->table().row("slime") != nullptr);

        // A column of whole numbers and a formula; a cell that does not fit is marked.
        std::string error;
        REQUIRE(table->editor()->add_column({"attack", game::ColumnKind::Whole}, error));
        REQUIRE(
            table->editor()->add_column({"xp", game::ColumnKind::Formula, "attack * 3"}, error));
        REQUIRE(table->editor()->set_cell("slime", "attack", "5", error));
        table->refresh();
        CHECK(grid->item(0, 3)->text() == QStringLiteral("15"));
        CHECK_FALSE(grid->item(0, 3)->flags().testFlag(Qt::ItemIsEditable));
        grid->item(0, 2)->setText(QStringLiteral("5.5"));
        QApplication::processEvents();
        CHECK(grid->item(0, 2)->toolTip() == QStringLiteral("\"5.5\" is not a whole number."));
        // The row's form shows the same cells.
        grid->setCurrentCell(0, 1);
        auto* name = table->form()->findChild<QLineEdit*>(QStringLiteral("form:name"));
        REQUIRE(name != nullptr);
        CHECK(name->text() == QStringLiteral("Slime"));

        const std::string added = table->add_row();
        CHECK(grid->rowCount() == 2);
        CHECK(table->editor()->table().rows.back().id == added);
        // CSV out and in.
        REQUIRE(table->export_csv(path / "enemies.csv", error));
        std::ofstream(path / "more.csv") << "id,attack\nbat,7\n";
        REQUIRE(table->import_csv(path / "more.csv", error));
        CHECK(table->editor()->table().number("bat", "xp") == 21.0);
        REQUIRE(table->save());
        CHECK(window.document()->assets().find("data/enemies.dtable") != nullptr);
        CHECK(window.open_table(QStringLiteral("data/enemies.dtable")) == table);
        table->close();
    }
    std::error_code ignored;
    fs::remove_all(path, ignored);
}
