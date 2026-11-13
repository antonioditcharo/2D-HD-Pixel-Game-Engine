#include "search.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace editor {

namespace {

/// Where an item is in `items_`, kept in its list row.
constexpr int index_role = Qt::UserRole;
/// The most rows shown, so a large project stays quick.
constexpr int most_shown = 60;

bool word_start(const QString& text, qsizetype at) {
    if (at == 0) {
        return true;
    }
    const QChar before = text.at(at - 1);
    return !before.isLetterOrNumber() ||
           (before.isLower() && text.at(at).isUpper()); // camelCase, too
}

} // namespace

int search_score(const QString& query, const QString& text) {
    const QString q = query.simplified().remove(QLatin1Char(' ')).toCaseFolded();
    if (q.isEmpty()) {
        return 0;
    }
    const QString folded = text.toCaseFolded();
    int score = 0;
    qsizetype at = 0;
    qsizetype previous = -2;
    for (const QChar c : q) {
        const qsizetype found = folded.indexOf(c, at);
        if (found < 0) {
            return -1;
        }
        score += 1;
        if (found == previous + 1) {
            score += 5; // side by side
        }
        if (word_start(text, found)) {
            score += found == 0 ? 10 : 6;
        }
        previous = found;
        at = found + 1;
    }
    // Shorter texts, and a whole-text start, first.
    score -= static_cast<int>(std::min<qsizetype>(folded.size(), 60) / 6);
    if (folded.startsWith(q)) {
        score += 15;
    }
    return score;
}

SearchBox::SearchBox(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("search_box"));
    setWindowTitle(tr("Search everything"));
    resize(560, 420);
    auto* column = new QVBoxLayout(this);
    field_ = new QLineEdit(this);
    field_->setObjectName(QStringLiteral("search_field"));
    field_->setPlaceholderText(tr("Type to find commands, scenes, pictures, logic and entities"));
    field_->setClearButtonEnabled(true);
    field_->installEventFilter(this);
    column->addWidget(field_);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("search_results"));
    list_->setAccessibleName(tr("Search results"));
    column->addWidget(list_);
    connect(field_, &QLineEdit::textChanged, this, &SearchBox::filter);
    connect(field_, &QLineEdit::returnPressed, this, [this] { run_current(); });
    connect(list_, &QListWidget::itemActivated, this, [this] { run_current(); });
}

void SearchBox::set_items(std::vector<SearchItem> items) {
    items_ = std::move(items);
    filter(field_->text());
}

void SearchBox::filter(const QString& query) {
    std::vector<std::pair<int, std::size_t>> ranked;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const int score = search_score(query, items_[i].text);
        if (score >= 0) {
            ranked.emplace_back(score, i);
        }
    }
    // Best first; ties keep the order given (commands, then assets, then entities).
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    list_->clear();
    for (std::size_t r = 0; r < ranked.size() && r < static_cast<std::size_t>(most_shown); ++r) {
        const SearchItem& item = items_[ranked[r].second];
        auto* row = new QListWidgetItem(item.text + QStringLiteral("    ") + item.kind, list_);
        row->setData(index_role, static_cast<qulonglong>(ranked[r].second));
        row->setToolTip(item.kind);
    }
    list_->setCurrentRow(list_->count() > 0 ? 0 : -1);
}

bool SearchBox::run_current() {
    const QListWidgetItem* row = list_->currentItem();
    if (row == nullptr) {
        return false;
    }
    const auto index = static_cast<std::size_t>(row->data(index_role).toULongLong());
    if (index >= items_.size()) {
        return false;
    }
    const std::function<void()> run = items_[index].run;
    accept();
    if (run) {
        run();
    }
    return true;
}

bool SearchBox::eventFilter(QObject* watched, QEvent* event) {
    // Up and Down move through the results while typing.
    if (watched == field_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const int row = list_->currentRow();
        if (key->key() == Qt::Key_Down && row + 1 < list_->count()) {
            list_->setCurrentRow(row + 1);
            return true;
        }
        if (key->key() == Qt::Key_Up && row > 0) {
            list_->setCurrentRow(row - 1);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace editor
