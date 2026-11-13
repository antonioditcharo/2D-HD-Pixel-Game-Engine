#pragma once

// The table editor (plan/02 T1-T5, plan/06 §3.7): one data table as a grid, as spreadsheets
// and RPG databases show one: a row per thing (the row's name first), a column per value,
// formula columns worked out and greyed, cells that do not fit their column marked red with
// the reason. The chosen row also shows as a form. Columns are added and changed with their
// kind or formula; tables go to and from CSV.
//
// See docs/dev/tables.md.

#include <QMainWindow>
#include <filesystem>
#include <memory>
#include <string>

#include "editor/core/table_edit.h"

class QAction;
class QFormLayout;
class QScrollArea;
class QTableWidget;
class QWidget;

namespace editor {

class TableWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit TableWindow(QWidget* parent = nullptr);

    void edit(engine::game::Table table, const std::filesystem::path& file);
    bool open(const std::filesystem::path& file, std::string& error);
    [[nodiscard]] TableEditor* editor() const noexcept { return editor_.get(); }
    [[nodiscard]] QTableWidget* grid() const noexcept { return grid_; }
    [[nodiscard]] QWidget* form() const noexcept { return form_; }
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }

    bool save();
    void undo();
    void redo();
    /// Adds a row after the chosen one.
    std::string add_row();
    /// Asks for a new column (name, kind, formula) and adds it.
    void add_column_dialog();
    /// Asks how to change a column (also by double-clicking its heading).
    void edit_column_dialog(int column);
    bool export_csv(const std::filesystem::path& to, std::string& error);
    bool import_csv(const std::filesystem::path& from, std::string& error);
    void set_quiet(bool quiet) noexcept { quiet_ = quiet; }
    void refresh();

Q_SIGNALS:
    void saved(const std::filesystem::path& file);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refresh_form();
    void refresh_title();
    void report(const QString& message);
    /// The row chosen in the grid; empty for none.
    [[nodiscard]] std::string current_row() const;
    bool column_dialog(engine::game::TableColumn& column, const QString& title);

    std::unique_ptr<TableEditor> editor_;
    std::filesystem::path file_;
    bool quiet_ = false;
    bool refreshing_ = false;
    QTableWidget* grid_ = nullptr;
    QWidget* form_ = nullptr;
    QScrollArea* form_scroll_ = nullptr;
    QAction* undo_ = nullptr;
    QAction* redo_ = nullptr;
};

} // namespace editor
