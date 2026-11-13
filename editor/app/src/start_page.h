#pragma once

// The start page (plan/02 A1, plan/06 §4): what the editor shows with no project open, as
// project managers in other engines do: making a new game, opening one, trying the sample,
// and the projects opened lately, the latest first.
//
// See docs/dev/editor.md.

#include <QStringList>
#include <QWidget>

class QListWidget;
class QPushButton;

namespace editor {

class StartPage : public QWidget {
    Q_OBJECT

public:
    explicit StartPage(QWidget* parent = nullptr);

    /// The projects opened lately, by folder, the latest first. One whose folder or project
    /// file is gone is listed as missing.
    void set_recent(const QStringList& folders);
    /// The sample project to try; empty hides the button.
    void set_sample(const QString& folder);
    [[nodiscard]] QListWidget* recent_list() const noexcept { return recent_; }
    [[nodiscard]] QPushButton* sample_button() const noexcept { return sample_; }

    /// The game's name in a project folder's project file; empty if it cannot be read.
    [[nodiscard]] static QString project_name(const QString& folder);

Q_SIGNALS:
    void new_project();
    void open_project();
    /// A project to open, by folder: a recent one, or the sample.
    void open_folder(const QString& folder);
    /// A recent project to leave off the list.
    void forget(const QString& folder);

private:
    QListWidget* recent_ = nullptr;
    QPushButton* sample_ = nullptr;
    QString sample_folder_;
};

} // namespace editor
