#include "table_window.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <array>
#include <fstream>
#include <iterator>
#include <map>

namespace editor {

namespace fs = std::filesystem;
namespace game = engine::game;

namespace {

QString text_of(const std::string& s) {
    return QString::fromStdString(s);
}

std::string utf8(const QString& s) {
    const QByteArray bytes = s.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QString path_text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), // NOLINT
                             static_cast<qsizetype>(text.size()));
}

fs::path path_of(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return fs::path(std::u8string(bytes.begin(), bytes.end()));
}

const std::array<QString, 5>& kind_names() {
    static const std::array<QString, 5> names{QObject::tr("text"), QObject::tr("whole number"),
                                              QObject::tr("number"), QObject::tr("yes/no"),
                                              QObject::tr("formula")};
    return names;
}

} // namespace

TableWindow::TableWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("table_window"));
    resize(1100, 640);
    grid_ = new QTableWidget(this);
    grid_->setObjectName(QStringLiteral("table_grid"));
    grid_->setAccessibleName(tr("Table: a row per thing, a column per value"));
    grid_->horizontalHeader()->setSectionsClickable(true);
    setCentralWidget(grid_);
    connect(grid_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (refreshing_ || editor_ == nullptr) {
            return;
        }
        const int row = item->row();
        const int column = item->column();
        if (row < 0 || static_cast<std::size_t>(row) >= editor_->table().rows.size()) {
            return;
        }
        const std::string id = editor_->table().rows[static_cast<std::size_t>(row)].id;
        std::string error;
        bool changed = false;
        if (column == 0) {
            changed = editor_->rename_row(id, utf8(item->text().trimmed()), error);
        } else {
            const std::string col =
                editor_->table().columns[static_cast<std::size_t>(column - 1)].id;
            changed = editor_->set_cell(id, col, utf8(item->text().trimmed()), error);
            editor_->history().end_merge();
        }
        if (!error.empty()) {
            report(text_of(error));
        }
        // Not now: this runs inside the grid's own signal.
        QTimer::singleShot(0, this, [this] { refresh(); });
        static_cast<void>(changed);
    });
    connect(grid_->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this,
            [this](int column) {
                if (column > 0) {
                    edit_column_dialog(column - 1);
                }
            });
    connect(grid_, &QTableWidget::currentCellChanged, this, [this] {
        if (!refreshing_) {
            refresh_form();
        }
    });

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    form_ = new QWidget;
    scroll->setWidget(form_);
    form_scroll_ = scroll;
    auto* dock = new QDockWidget(tr("Row"), this);
    dock->setObjectName(QStringLiteral("table_row_dock"));
    dock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Save"), this, [this] { save(); })->setShortcut(QKeySequence::Save);
    file->addAction(tr("&Import CSV…"), this, [this] {
        const QString from =
            QFileDialog::getOpenFileName(this, tr("Import a CSV file"), {}, tr("CSV (*.csv)"));
        std::string error;
        if (!from.isEmpty() && !import_csv(path_of(from), error)) {
            report(text_of(error));
        }
    });
    file->addAction(tr("&Export CSV…"), this, [this] {
        const QString to =
            QFileDialog::getSaveFileName(this, tr("Export as CSV"), {}, tr("CSV (*.csv)"));
        std::string error;
        if (!to.isEmpty() && !export_csv(path_of(to), error)) {
            report(text_of(error));
        }
    });
    file->addAction(tr("&Close"), this, [this] { close(); })->setShortcut(QKeySequence::Close);
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    undo_ = edit->addAction(tr("&Undo"), this, [this] { undo(); });
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = edit->addAction(tr("&Redo"), this, [this] { redo(); });
    redo_->setShortcut(QKeySequence::Redo);
    QMenu* table = menuBar()->addMenu(tr("&Table"));
    QAction* row = table->addAction(tr("Add a &row"), this, [this] { add_row(); });
    row->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    QAction* gone = table->addAction(tr("&Delete the row"), this, [this] {
        if (editor_ != nullptr && editor_->remove_row(current_row())) {
            refresh();
        }
    });
    QAction* up = table->addAction(tr("Move the row &up"), this, [this] {
        if (editor_ != nullptr && editor_->move_row(current_row(), true)) {
            const int r = grid_->currentRow();
            refresh();
            grid_->setCurrentCell(r - 1, 0);
        }
    });
    QAction* down = table->addAction(tr("Move the row d&own"), this, [this] {
        if (editor_ != nullptr && editor_->move_row(current_row(), false)) {
            const int r = grid_->currentRow();
            refresh();
            grid_->setCurrentCell(r + 1, 0);
        }
    });
    QAction* column = table->addAction(tr("Add a &column…"), this, [this] { add_column_dialog(); });
    QAction* change = table->addAction(tr("Change the column…"), this, [this] {
        if (grid_->currentColumn() > 0) {
            edit_column_dialog(grid_->currentColumn() - 1);
        }
    });
    QAction* drop = table->addAction(tr("Delete the column"), this, [this] {
        const int c = grid_->currentColumn();
        if (editor_ != nullptr && c > 0 &&
            editor_->remove_column(editor_->table().columns[static_cast<std::size_t>(c - 1)].id)) {
            refresh();
        }
    });
    QToolBar* tools = addToolBar(tr("Table"));
    tools->setObjectName(QStringLiteral("table_tools"));
    for (QAction* action : {row, gone, up, down, column, change, drop, undo_, redo_}) {
        tools->addAction(action);
    }
}

void TableWindow::edit(game::Table table, const fs::path& file) {
    editor_ = std::make_unique<TableEditor>(std::move(table));
    file_ = file;
    refresh();
}

bool TableWindow::open(const fs::path& file, std::string& error) {
    std::optional<game::Table> loaded = TableEditor::load(file, error);
    if (!loaded) {
        return false;
    }
    edit(std::move(*loaded), file);
    return true;
}

bool TableWindow::save() {
    std::string error;
    if (editor_ == nullptr || !TableEditor::save(editor_->table(), file_, error)) {
        report(tr("The table could not be saved: %1").arg(text_of(error)));
        return false;
    }
    editor_->history().mark_saved();
    refresh_title();
    statusBar()->showMessage(tr("Saved"), 3000);
    Q_EMIT saved(file_);
    return true;
}

void TableWindow::undo() {
    if (editor_ != nullptr && editor_->history().undo()) {
        refresh();
    }
}

void TableWindow::redo() {
    if (editor_ != nullptr && editor_->history().redo()) {
        refresh();
    }
}

std::string TableWindow::current_row() const {
    const int r = grid_->currentRow();
    if (editor_ == nullptr || r < 0 ||
        static_cast<std::size_t>(r) >= editor_->table().rows.size()) {
        return {};
    }
    return editor_->table().rows[static_cast<std::size_t>(r)].id;
}

std::string TableWindow::add_row() {
    if (editor_ == nullptr) {
        return {};
    }
    const std::string id = editor_->add_row(current_row());
    refresh();
    for (std::size_t r = 0; r < editor_->table().rows.size(); ++r) {
        if (editor_->table().rows[r].id == id) {
            grid_->setCurrentCell(static_cast<int>(r), 0);
        }
    }
    return id;
}

bool TableWindow::column_dialog(game::TableColumn& column, const QString& title) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* layout = new QFormLayout(&dialog);
    auto* name = new QLineEdit(text_of(column.id), &dialog);
    auto* kind = new QComboBox(&dialog);
    for (const QString& k : kind_names()) {
        kind->addItem(k);
    }
    kind->setCurrentIndex(static_cast<int>(column.kind));
    auto* formula = new QLineEdit(text_of(column.formula), &dialog);
    formula->setPlaceholderText(tr("level * level * 10"));
    formula->setToolTip(tr("Numbers, + - * / ^, brackets, min(a, b), max(a, b), floor(a), "
                           "round(a), the row's other columns by name, and row (its place)"));
    auto* low = new QDoubleSpinBox(&dialog);
    auto* high = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox* box : {low, high}) {
        box->setRange(-1e9, 1e9);
        box->setDecimals(2);
    }
    low->setValue(column.min);
    high->setValue(column.max);
    layout->addRow(tr("Name"), name);
    layout->addRow(tr("Holds"), kind);
    layout->addRow(tr("Formula"), formula);
    layout->addRow(tr("At least"), low);
    layout->addRow(tr("At most"), high);
    const auto enable = [kind, formula] { formula->setEnabled(kind->currentIndex() == 4); };
    enable();
    connect(kind, &QComboBox::currentIndexChanged, &dialog, enable);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    column.id = utf8(name->text().trimmed());
    column.kind = static_cast<game::ColumnKind>(kind->currentIndex());
    column.formula = utf8(formula->text().trimmed());
    column.min = low->value();
    column.max = high->value();
    return true;
}

void TableWindow::add_column_dialog() {
    if (editor_ == nullptr) {
        return;
    }
    game::TableColumn column{"value", game::ColumnKind::Number};
    if (!column_dialog(column, tr("Add a column"))) {
        return;
    }
    std::string error;
    if (editor_->add_column(column, error)) {
        refresh();
    } else {
        report(text_of(error));
    }
}

void TableWindow::edit_column_dialog(int index) {
    if (editor_ == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= editor_->table().columns.size()) {
        return;
    }
    const game::TableColumn now = editor_->table().columns[static_cast<std::size_t>(index)];
    game::TableColumn column = now;
    if (!column_dialog(column, tr("Change the column %1").arg(text_of(now.id)))) {
        return;
    }
    std::string error;
    if (editor_->set_column(now.id, column, error)) {
        editor_->history().end_merge();
        refresh();
    } else if (!error.empty()) {
        report(text_of(error));
    }
}

bool TableWindow::export_csv(const fs::path& to, std::string& error) {
    if (editor_ == nullptr) {
        return false;
    }
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    const std::string csv = game::table_to_csv(editor_->table());
    out.write(csv.data(), static_cast<std::streamsize>(csv.size()));
    if (!out) {
        error = "The CSV file could not be written.";
        return false;
    }
    statusBar()->showMessage(tr("Exported %1").arg(path_text(to.filename())), 4000);
    return true;
}

bool TableWindow::import_csv(const fs::path& from, std::string& error) {
    std::ifstream in(from, std::ios::binary);
    if (!in || editor_ == nullptr) {
        error = "The CSV file cannot be opened.";
        return false;
    }
    const std::string csv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!editor_->import_csv(csv, error)) {
        return error.empty(); // nothing new is not a failure
    }
    refresh();
    return true;
}

void TableWindow::report(const QString& message) {
    statusBar()->showMessage(message, 8000);
    if (!quiet_ && !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        QMessageBox::warning(this, tr("Table"), message);
    }
}

void TableWindow::refresh_title() {
    setWindowTitle(tr("%1%2 — Table")
                       .arg(path_text(file_.filename()))
                       .arg(editor_ != nullptr && editor_->history().modified()
                                ? QStringLiteral(" *")
                                : QString()));
    const History<game::Table>* h = editor_ != nullptr ? &editor_->history() : nullptr;
    undo_->setEnabled(h != nullptr && h->can_undo());
    redo_->setEnabled(h != nullptr && h->can_redo());
    undo_->setText(h != nullptr && h->can_undo() ? tr("&Undo %1").arg(text_of(h->undo_text()))
                                                 : tr("&Undo"));
    redo_->setText(h != nullptr && h->can_redo() ? tr("&Redo %1").arg(text_of(h->redo_text()))
                                                 : tr("&Redo"));
}

void TableWindow::refresh() {
    if (editor_ == nullptr) {
        return;
    }
    refreshing_ = true;
    const game::Table& t = editor_->table();
    const int keep_row = grid_->currentRow();
    const int keep_column = grid_->currentColumn();
    grid_->clear();
    grid_->setRowCount(static_cast<int>(t.rows.size()));
    grid_->setColumnCount(static_cast<int>(t.columns.size()) + 1);
    QStringList headings{tr("Name")};
    for (const game::TableColumn& c : t.columns) {
        headings << text_of(c.id) + QLatin1Char('\n') +
                        kind_names().at(static_cast<std::size_t>(c.kind));
    }
    grid_->setHorizontalHeaderLabels(headings);
    for (int c = 1; c < grid_->columnCount(); ++c) {
        const game::TableColumn& column = t.columns[static_cast<std::size_t>(c - 1)];
        grid_->horizontalHeaderItem(c)->setToolTip(
            column.kind == game::ColumnKind::Formula
                ? tr("= %1 (double-click to change)").arg(text_of(column.formula))
                : tr("Double-click to change the column"));
    }
    // Cells that do not fit, by "row, column".
    std::map<std::string, std::string> problems;
    for (const std::string& problem : game::check_table(t)) {
        const std::size_t colon = problem.find(": ");
        if (colon != std::string::npos) {
            problems[problem.substr(0, colon)] = problem.substr(colon + 2);
        }
    }
    for (std::size_t r = 0; r < t.rows.size(); ++r) {
        const game::TableRow& row = t.rows[r];
        grid_->setItem(static_cast<int>(r), 0, new QTableWidgetItem(text_of(row.id)));
        for (std::size_t c = 0; c < t.columns.size(); ++c) {
            const game::TableColumn& column = t.columns[c];
            auto* item = new QTableWidgetItem(text_of(t.text(row.id, column.id).value_or("")));
            if (column.kind == game::ColumnKind::Formula) {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setBackground(QColor(236, 236, 240));
                item->setToolTip(tr("Worked out: %1").arg(text_of(column.formula)));
            }
            if (const auto found = problems.find(row.id + ", " + column.id);
                found != problems.end()) {
                item->setBackground(QColor(255, 210, 210));
                item->setToolTip(text_of(found->second));
            }
            grid_->setItem(static_cast<int>(r), static_cast<int>(c) + 1, item);
        }
    }
    grid_->resizeColumnsToContents();
    if (keep_row >= 0 && keep_row < grid_->rowCount()) {
        grid_->setCurrentCell(keep_row,
                              std::max(0, std::min(keep_column, grid_->columnCount() - 1)));
    }
    refreshing_ = false;
    refresh_form();
    refresh_title();
    statusBar()->showMessage(problems.empty() ? QString()
                                              : tr("%n cell(s) do not fit their column: they "
                                                   "are red, with the reason as their tip.",
                                                   nullptr, static_cast<int>(problems.size())));
}

void TableWindow::refresh_form() {
    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("table_form"));
    auto* layout = new QFormLayout(panel);
    const std::string id = current_row();
    const game::TableRow* row = editor_ != nullptr ? editor_->table().row(id) : nullptr;
    if (row == nullptr) {
        layout->addRow(new QLabel(tr("Choose a row."), panel));
    } else {
        layout->addRow(
            new QLabel(QStringLiteral("<b>%1</b>").arg(text_of(id).toHtmlEscaped()), panel));
        for (const game::TableColumn& column : editor_->table().columns) {
            auto* field =
                new QLineEdit(text_of(editor_->table().text(id, column.id).value_or("")), panel);
            field->setObjectName(QStringLiteral("form:%1").arg(text_of(column.id)));
            field->setReadOnly(column.kind == game::ColumnKind::Formula);
            field->setToolTip(kind_names().at(static_cast<std::size_t>(column.kind)));
            field->setProperty("row", text_of(id));
            connect(field, &QLineEdit::editingFinished, this, [this, field] {
                std::string error;
                const std::string row_id = utf8(field->property("row").toString());
                const std::string column_id = utf8(field->objectName().mid(5)); // "form:"
                if (editor_->set_cell(row_id, column_id, utf8(field->text().trimmed()), error)) {
                    editor_->history().end_merge();
                    QTimer::singleShot(0, this, [this] { refresh(); });
                }
            });
            layout->addRow(text_of(column.id), field);
        }
    }
    form_ = panel;
    form_scroll_->setWidget(panel); // the old form is deleted
}

void TableWindow::closeEvent(QCloseEvent* event) {
    if (editor_ != nullptr && editor_->history().modified() &&
        !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const auto answer = QMessageBox::question(
            this, tr("Table"), tr("Save the changes to %1 first?").arg(path_text(file_.filename())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

} // namespace editor
