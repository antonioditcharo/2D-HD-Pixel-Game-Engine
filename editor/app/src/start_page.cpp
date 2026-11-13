#include "start_page.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace editor {

namespace {

/// Where a recent project's folder is kept in its list item.
constexpr int folder_role = Qt::UserRole;

} // namespace

StartPage::StartPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("start_page"));
    auto* outer = new QHBoxLayout(this);
    outer->addStretch(1);
    auto* column = new QVBoxLayout;
    outer->addLayout(column, 3);
    outer->addStretch(1);

    column->addStretch(1);
    auto* title = new QLabel(tr("Sparxel"), this);
    QFont big = title->font();
    big.setPointSize(big.pointSize() * 3);
    big.setBold(true);
    title->setFont(big);
    column->addWidget(title);
    auto* line = new QLabel(tr("Make a pixel game, from the first sketch to a game that plays on "
                               "its own, without writing code."),
                            this);
    line->setWordWrap(true);
    column->addWidget(line);

    auto* buttons = new QHBoxLayout;
    auto* fresh = new QPushButton(tr("New project…"), this);
    fresh->setObjectName(QStringLiteral("start_new"));
    fresh->setToolTip(tr("Start a game from a template: Classic 2D, Diorama, a platformer or a "
                         "visual novel (Ctrl+N)"));
    connect(fresh, &QPushButton::clicked, this, [this] { Q_EMIT new_project(); });
    auto* open = new QPushButton(tr("Open project…"), this);
    open->setObjectName(QStringLiteral("start_open"));
    open->setToolTip(tr("Open a project folder (Ctrl+O)"));
    connect(open, &QPushButton::clicked, this, [this] { Q_EMIT open_project(); });
    sample_ = new QPushButton(tr("Try the sample village"), this);
    sample_->setObjectName(QStringLiteral("start_sample"));
    sample_->setToolTip(tr("A small village with a talking elder, to look around and change"));
    sample_->hide();
    connect(sample_, &QPushButton::clicked, this, [this] { Q_EMIT open_folder(sample_folder_); });
    for (QPushButton* button : {fresh, open, sample_}) {
        button->setMinimumHeight(40);
        buttons->addWidget(button);
    }
    column->addLayout(buttons);

    column->addWidget(new QLabel(tr("Recent projects"), this));
    recent_ = new QListWidget(this);
    recent_->setObjectName(QStringLiteral("recent_projects"));
    recent_->setAccessibleName(tr("Recent projects"));
    recent_->setToolTip(tr("Double-click (or Enter) to open; right-click to leave one off"));
    recent_->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* remove = new QAction(tr("Leave off the list"), recent_);
    remove->setObjectName(QStringLiteral("forget_recent"));
    recent_->addAction(remove);
    connect(remove, &QAction::triggered, this, [this] {
        if (QListWidgetItem* item = recent_->currentItem()) {
            Q_EMIT forget(item->data(folder_role).toString());
        }
    });
    connect(recent_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        Q_EMIT open_folder(item->data(folder_role).toString());
    });
    column->addWidget(recent_, 2);
    column->addStretch(1);
}

QString StartPage::project_name(const QString& folder) {
    QFile file(QDir(folder).filePath(QStringLiteral("project.sparxel.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    return json.object().value(QStringLiteral("name")).toString();
}

void StartPage::set_recent(const QStringList& folders) {
    recent_->clear();
    for (const QString& folder : folders) {
        const QString name = project_name(folder);
        auto* item = new QListWidgetItem(recent_);
        item->setData(folder_role, folder);
        item->setToolTip(QDir::toNativeSeparators(folder));
        if (name.isEmpty()) {
            item->setText(tr("%1 (missing)\n%2")
                              .arg(QDir(folder).dirName(), QDir::toNativeSeparators(folder)));
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        } else {
            item->setText(name + QLatin1Char('\n') + QDir::toNativeSeparators(folder));
        }
    }
    if (folders.isEmpty()) {
        auto* empty = new QListWidgetItem(
            tr("Projects you open show here. Start with a new project, or try the sample."),
            recent_);
        empty->setFlags(Qt::NoItemFlags);
    }
}

void StartPage::set_sample(const QString& folder) {
    sample_folder_ = folder;
    sample_->setVisible(!folder.isEmpty());
}

} // namespace editor
