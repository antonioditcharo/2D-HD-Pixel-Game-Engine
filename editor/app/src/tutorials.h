#pragma once

// Interactive tutorials (plan/02 Q1): "quests" inside the editor. Each tutorial is a list of
// steps in plain words; the panel ticks each off by watching what is really done in the
// editor (a step is never ticked by pressing a button), and Show me points at the part of
// the editor the step needs.

#include <QString>
#include <QWidget>
#include <functional>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;

namespace editor {

class MainWindow;

struct TutorialStep {
    QString title;   ///< "Select the hero"
    QString text;    ///< How, in a sentence or two.
    QString show_me; ///< The object name of the panel to point at; empty for none.
    std::function<bool(const MainWindow&)> done;
};

struct Tutorial {
    QString name;
    QString blurb;
    std::vector<TutorialStep> steps;
};

/// The tutorials the panel offers.
[[nodiscard]] std::vector<Tutorial> tutorials();

class TutorialPanel : public QWidget {
    Q_OBJECT

public:
    explicit TutorialPanel(MainWindow* window, QWidget* parent = nullptr);

    /// Starts tutorial `index` from its first step.
    void start(int index);
    /// Ticks off the steps done, in order, up to the first not done yet.
    void check();
    /// Passes over the current step.
    void skip();
    /// Points at the panel the current step needs, for a moment.
    void show_me();
    [[nodiscard]] int tutorial() const noexcept { return tutorial_; }
    [[nodiscard]] std::size_t step() const noexcept { return step_; }
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] QListWidget* step_list() const noexcept { return list_; }

private:
    void show_step();

    MainWindow* window_;
    std::vector<Tutorial> all_;
    int tutorial_ = 0;
    std::size_t step_ = 0;
    QComboBox* choice_ = nullptr;
    QLabel* blurb_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* text_ = nullptr;
    QPushButton* show_ = nullptr;
    QPushButton* skip_ = nullptr;
    QTimer* timer_ = nullptr;
};

} // namespace editor
