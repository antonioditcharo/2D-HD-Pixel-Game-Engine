#include "tutorials.h"

#include "engine/game/behaviours.h"
#include "engine/game/conversation.h"

#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <string>
#include <string_view>

#include "main_window.h"

namespace editor {

namespace {

namespace game = engine::game;
namespace logic = engine::logic;

bool undo_starts(const MainWindow& w, std::string_view start) {
    return w.document() != nullptr && w.document()->undo_label().starts_with(start);
}

/// Whether any step of the project's logic does `object`'s action `member`.
bool logic_does(const MainWindow& w, std::string_view object, std::string_view member) {
    if (w.document() == nullptr) {
        return false;
    }
    const std::function<bool(const std::vector<logic::Stmt>&)> any =
        [&](const std::vector<logic::Stmt>& steps) {
            return std::any_of(steps.begin(), steps.end(), [&](const logic::Stmt& s) {
                return (s.kind == logic::StmtKind::Do && s.object == object &&
                        s.member == member) ||
                       any(s.body) || any(s.otherwise);
            });
        };
    for (const LogicFile& file : w.document()->logic()) {
        for (const logic::Rule& rule : file.asset.rules) {
            if (any(rule.actions) || any(rule.otherwise)) {
                return true;
            }
        }
    }
    return false;
}

/// Whether the project has a conversation whose first line has words.
bool conversation_written(const MainWindow& w) {
    if (w.document() == nullptr) {
        return false;
    }
    for (const engine::assets::AssetRecord& record : w.document()->assets().assets()) {
        if (!record.path.ends_with(".dtalk")) {
            continue;
        }
        const std::optional<std::vector<std::uint8_t>> bytes =
            w.document()->assets().read(record.guid);
        std::string error;
        const std::optional<game::Conversation> talk =
            bytes ? game::parse_conversation(
                        std::string_view(reinterpret_cast<const char*>(bytes->data()), // NOLINT
                                         bytes->size()),
                        error)
                  : std::nullopt;
        if (talk && std::any_of(talk->nodes.begin(), talk->nodes.end(),
                                [](const game::TalkNode& n) { return !n.text.empty(); })) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<Tutorial> tutorials() {
    std::vector<Tutorial> out;
    out.push_back(Tutorial{
        TutorialPanel::tr("Your first game"),
        TutorialPanel::tr("Move a hero around a map you paint, then play it."),
        {
            {TutorialPanel::tr("Open or make a project"),
             TutorialPanel::tr("File ▸ New project… makes a small game to start from: choose "
                               "Classic 2D or Diorama."),
             {},
             [](const MainWindow& w) { return w.document() != nullptr; }},
            {TutorialPanel::tr("Select the hero"),
             TutorialPanel::tr("Click the hero in the scene, or in the Entities list on the "
                               "left. Its parts show in the Inspector."),
             QStringLiteral("entities_dock"),
             [](const MainWindow& w) {
                 const Document* d = w.document();
                 const engine::world::Entity e =
                     d != nullptr ? d->world().find(w.selection()) : engine::world::Entity{};
                 return e.is_valid() && d->world().get<game::TopDownMover>(e) != nullptr;
             }},
            {TutorialPanel::tr("Make the hero faster"),
             TutorialPanel::tr("In the Inspector, under Top-down Mover, raise Speed."),
             QStringLiteral("inspector_dock"),
             [](const MainWindow& w) { return undo_starts(w, "Change Speed"); }},
            {TutorialPanel::tr("Paint the ground"),
             TutorialPanel::tr("Select the ground, pick a tile in the Tiles panel and paint in "
                               "the scene with the left button."),
             QStringLiteral("tiles_dock"),
             [](const MainWindow& w) { return undo_starts(w, "Paint"); }},
            {TutorialPanel::tr("Save"),
             TutorialPanel::tr("File ▸ Save, or Ctrl+S."),
             {},
             [](const MainWindow& w) {
                 return w.document() != nullptr && !w.document()->dirty() &&
                        w.document()->can_undo();
             }},
            {TutorialPanel::tr("Play your game"),
             TutorialPanel::tr("Game ▸ Play, or F5. Walk with the arrow keys; F1 opens the "
                               "debug menu while testing."),
             QStringLiteral("commands"), [](const MainWindow& w) { return w.plays() > 0; }},
        }});
    out.push_back(Tutorial{
        TutorialPanel::tr("Make someone talk"),
        TutorialPanel::tr("Write a conversation and start it when the player talks to someone."),
        {
            {TutorialPanel::tr("Open a project"),
             TutorialPanel::tr("Open or make one first."),
             {},
             [](const MainWindow& w) { return w.document() != nullptr; }},
            {TutorialPanel::tr("Write a conversation"),
             TutorialPanel::tr("File ▸ New conversation…, then type what is said in Says, and "
                               "save it (Ctrl+S in its window)."),
             QStringLiteral("assets_dock"),
             [](const MainWindow& w) { return conversation_written(w); }},
            {TutorialPanel::tr("Start it from a Rule"),
             TutorialPanel::tr("In the Logic panel, add a Rule: When the player talks to someone "
                               "(Interact), do Dialogue ▸ Start the conversation."),
             QStringLiteral("logic_dock"),
             [](const MainWindow& w) { return logic_does(w, "dialogue", "start_conversation"); }},
            {TutorialPanel::tr("Play and talk"),
             TutorialPanel::tr("Game ▸ Play (F5), walk up to them and press Z."),
             QStringLiteral("commands"), [](const MainWindow& w) { return w.plays() > 0; }},
        }});
    return out;
}

TutorialPanel::TutorialPanel(MainWindow* window, QWidget* parent)
    : QWidget(parent), window_(window), all_(tutorials()) {
    setObjectName(QStringLiteral("tutorials"));
    auto* column = new QVBoxLayout(this);
    choice_ = new QComboBox(this);
    choice_->setObjectName(QStringLiteral("tutorial_choice"));
    for (const Tutorial& t : all_) {
        choice_->addItem(t.name);
    }
    blurb_ = new QLabel(this);
    blurb_->setWordWrap(true);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("tutorial_steps"));
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("tutorial_text"));
    text_->setWordWrap(true);
    text_->setMinimumHeight(48);
    auto* buttons = new QHBoxLayout();
    show_ = new QPushButton(tr("Show me"), this);
    show_->setToolTip(tr("Points at the part of the editor this step needs"));
    skip_ = new QPushButton(tr("Skip"), this);
    skip_->setToolTip(tr("Go on to the next step without doing this one"));
    buttons->addWidget(show_);
    buttons->addWidget(skip_);
    column->addWidget(choice_);
    column->addWidget(blurb_);
    column->addWidget(list_, 1);
    column->addWidget(text_);
    column->addLayout(buttons);
    connect(choice_, &QComboBox::currentIndexChanged, this, [this](int index) { start(index); });
    connect(show_, &QPushButton::clicked, this, &TutorialPanel::show_me);
    connect(skip_, &QPushButton::clicked, this, &TutorialPanel::skip);
    // Watching what is done: twice a second, cheaply.
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &TutorialPanel::check);
    timer_->start(500);
    start(0);
}

bool TutorialPanel::finished() const noexcept {
    return tutorial_ < 0 || static_cast<std::size_t>(tutorial_) >= all_.size() ||
           step_ >= all_[static_cast<std::size_t>(tutorial_)].steps.size();
}

void TutorialPanel::start(int index) {
    tutorial_ = std::clamp(index, 0, static_cast<int>(all_.size()) - 1);
    step_ = 0;
    if (choice_->currentIndex() != tutorial_) {
        const QSignalBlocker quiet(choice_);
        choice_->setCurrentIndex(tutorial_);
    }
    show_step();
    check();
}

void TutorialPanel::check() {
    if (finished() || window_ == nullptr) {
        return;
    }
    const Tutorial& t = all_[static_cast<std::size_t>(tutorial_)];
    const std::size_t before = step_;
    while (step_ < t.steps.size() && t.steps[step_].done && t.steps[step_].done(*window_)) {
        ++step_;
    }
    if (step_ != before) {
        show_step();
    }
}

void TutorialPanel::skip() {
    if (!finished()) {
        ++step_;
        show_step();
    }
}

void TutorialPanel::show_step() {
    const Tutorial& t = all_[static_cast<std::size_t>(tutorial_)];
    blurb_->setText(t.blurb);
    list_->clear();
    for (std::size_t i = 0; i < t.steps.size(); ++i) {
        const QString mark = i < step_
                                 ? QStringLiteral("✓ ")
                                 : (i == step_ ? QStringLiteral("▶ ") : QStringLiteral("   "));
        list_->addItem(mark + t.steps[i].title);
        QListWidgetItem* item = list_->item(list_->count() - 1);
        if (i == step_) {
            QFont bold = item->font();
            bold.setBold(true);
            item->setFont(bold);
        } else if (i < step_) {
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        }
    }
    if (finished()) {
        text_->setText(tr("Done! Choose another tutorial above, or read the guide (F1)."));
        show_->setEnabled(false);
        skip_->setEnabled(false);
        return;
    }
    const TutorialStep& s = t.steps[step_];
    text_->setText(s.text);
    show_->setEnabled(!s.show_me.isEmpty());
    skip_->setEnabled(true);
}

void TutorialPanel::show_me() {
    if (finished() || window_ == nullptr) {
        return;
    }
    const QString name = all_[static_cast<std::size_t>(tutorial_)].steps[step_].show_me;
    QWidget* target = window_->findChild<QWidget*>(name);
    if (target == nullptr) {
        return;
    }
    if (auto* dock = qobject_cast<QDockWidget*>(target)) {
        dock->show();
        dock->raise();
    }
    target->show();
    // A bright frame for a moment, then as it was.
    const QString before = target->styleSheet();
    target->setStyleSheet(before + QStringLiteral(" #%1 { border: 3px solid #ff40ff; }").arg(name));
    QTimer::singleShot(1500, target, [target, before] { target->setStyleSheet(before); });
}

} // namespace editor
