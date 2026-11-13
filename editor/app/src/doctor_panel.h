#pragma once

// The Project Doctor panel (plan/02 A10, plan/06 §2): "Check the project" lists what would
// go wrong, problems first, each in plain language; double-clicking one goes where it can
// be fixed.
//
// See docs/dev/editor.md.

#include <QWidget>
#include <vector>

#include "editor/core/doctor.h"

class QLabel;
class QTreeWidget;

namespace editor {

class DoctorPanel : public QWidget {
    Q_OBJECT

public:
    explicit DoctorPanel(QWidget* parent = nullptr);

    /// Checks `document` (nothing with none) and lists what it found.
    void check(const Document* document);
    [[nodiscard]] const std::vector<Finding>& findings() const noexcept { return findings_; }
    [[nodiscard]] QTreeWidget* list() const noexcept { return list_; }
    /// A short summary, such as "2 problems, 1 warning", for the status bar.
    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    /// The Check button was pressed.
    void check_requested();
    /// A finding was chosen, to go to.
    void chosen(const editor::Finding& finding);

private:
    QTreeWidget* list_ = nullptr;
    QLabel* result_ = nullptr;
    std::vector<Finding> findings_;
};

} // namespace editor
