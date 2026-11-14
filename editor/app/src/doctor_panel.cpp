#include "doctor_panel.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace editor {

DoctorPanel::DoctorPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("doctor_panel"));
    auto* column = new QVBoxLayout(this);
    auto* button = new QPushButton(tr("Check the project"), this);
    button->setObjectName(QStringLiteral("doctor_check"));
    button->setToolTip(tr("Find what would go wrong in the game: missing files, logic that does "
                          "not run, scenes nothing leads to, files nothing uses"));
    connect(button, &QPushButton::clicked, this, [this] { Q_EMIT check_requested(); });
    column->addWidget(button);
    result_ = new QLabel(tr("Not checked yet."), this);
    result_->setWordWrap(true);
    column->addWidget(result_);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("doctor_findings"));
    list_->setAccessibleName(tr("Project Doctor's findings"));
    list_->setHeaderLabels({tr("Kind"), tr("What")});
    list_->setRootIsDecorated(false);
    list_->setWordWrap(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list_->setToolTip(tr("Double-click to go where it can be fixed"));
    column->addWidget(list_);
    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const int index = list_->indexOfTopLevelItem(item);
        if (index >= 0 && static_cast<std::size_t>(index) < findings_.size()) {
            Q_EMIT chosen(findings_[static_cast<std::size_t>(index)]);
        }
    });
}

void DoctorPanel::check(const Document* document) {
    findings_ = document == nullptr ? std::vector<Finding>{} : examine(*document);
    list_->clear();
    for (const Finding& finding : findings_) {
        // Words as well as colour, so the kind reads without seeing colours.
        QString kind = tr("Problem");
        QColor colour(200, 40, 40);
        if (finding.severity == Finding::Severity::Warning) {
            kind = tr("Warning");
            colour = QColor(190, 120, 0);
        } else if (finding.severity == Finding::Severity::Note) {
            kind = tr("Note");
            colour = palette().color(QPalette::Text);
        }
        auto* item = new QTreeWidgetItem(list_, {kind, QString::fromStdString(finding.message)});
        item->setForeground(0, colour);
        item->setToolTip(1, QString::fromStdString(finding.message));
    }
    result_->setText(document == nullptr ? tr("Open a project to check it.") : summary());
}

QString DoctorPanel::summary() const {
    int problems = 0;
    int warnings = 0;
    int notes = 0;
    for (const Finding& finding : findings_) {
        problems += finding.severity == Finding::Severity::Problem ? 1 : 0;
        warnings += finding.severity == Finding::Severity::Warning ? 1 : 0;
        notes += finding.severity == Finding::Severity::Note ? 1 : 0;
    }
    if (problems + warnings + notes == 0) {
        return tr("All good: nothing found.");
    }
    return tr("%n problem(s)", nullptr, problems) + QStringLiteral(", ") +
           tr("%n warning(s)", nullptr, warnings) + QStringLiteral(", ") +
           tr("%n note(s)", nullptr, notes);
}

} // namespace editor
