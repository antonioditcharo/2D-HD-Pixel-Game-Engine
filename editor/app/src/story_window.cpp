#include "story_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <array>
#include <cmath>
#include <utility>

namespace editor {

namespace fs = std::filesystem;
namespace game = engine::game;

namespace {

constexpr double node_width = 200.0;
constexpr double node_height = 76.0;
/// Where a node's id is kept on its graph item.
constexpr int id_key = 0;

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

QString excerpt(const std::string& text, int most) {
    const QString s = text_of(text).simplified();
    return s.size() > most ? s.left(most - 1) + QStringLiteral("…") : s;
}

/// An arrow from one box's right side to another's left side, curving between them; one
/// leading back to the left goes round underneath both boxes.
QPainterPath arrow(QPointF from, QPointF to) {
    QPainterPath path(from);
    if (to.x() >= from.x() + 20.0) {
        const double bend = std::max(40.0, std::abs(to.x() - from.x()) / 2.0);
        path.cubicTo(from + QPointF(bend, 0), to - QPointF(bend, 0), to);
    } else {
        const double low = std::max(from.y(), to.y()) + (node_height / 2.0) + 30.0;
        path.cubicTo(from + QPointF(50, 0), QPointF(from.x() + 50, low), QPointF(from.x(), low));
        path.lineTo(to.x(), low);
        path.cubicTo(QPointF(to.x() - 50, low), to - QPointF(50, 0), to);
    }
    path.moveTo(to);
    path.lineTo(to + QPointF(-9, -5));
    path.moveTo(to);
    path.lineTo(to + QPointF(-9, 5));
    return path;
}

/// `text` cut to fit `width` pixels in `font`, ending in "…" when it is cut.
QString fitted(const QString& text, const QFont& font, double width) {
    return QFontMetrics(font).elidedText(text, Qt::ElideRight, static_cast<int>(width));
}

const std::array<QString, 4>& condition_names() {
    static const std::array<QString, 4> names{
        QObject::tr("the player has the item"), QObject::tr("the flag is set"),
        QObject::tr("the quest is under way"), QObject::tr("the quest is complete")};
    return names;
}

const std::array<QString, 6>& effect_names() {
    static const std::array<QString, 6> names{
        QObject::tr("Give the item"),   QObject::tr("Take the item"),
        QObject::tr("Set the flag"),    QObject::tr("Clear the flag"),
        QObject::tr("Start the quest"), QObject::tr("Complete the quest")};
    return names;
}

} // namespace

StoryWindow::StoryWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("story_window"));
    resize(1200, 760);
    scene_ = new QGraphicsScene(this);
    view_ = new QGraphicsView(scene_, this);
    view_->setObjectName(QStringLiteral("story_graph"));
    view_->setAccessibleName(tr("Conversation graph"));
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setDragMode(QGraphicsView::RubberBandDrag);
    view_->viewport()->installEventFilter(this);
    setCentralWidget(view_);
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (refreshing_) {
            return;
        }
        const QList<QGraphicsItem*> chosen = scene_->selectedItems();
        if (chosen.size() == 1) {
            select(utf8(chosen.front()->data(id_key).toString()));
        }
    });

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("story_nodes"));
    list_->setAccessibleName(tr("Nodes"));
    connect(list_, &QListWidget::currentTextChanged, this, [this](const QString& text) {
        if (!refreshing_ && !text.isEmpty()) {
            select(utf8(text.section(QLatin1Char(' '), 0, 0)));
        }
    });
    auto* nodes = new QDockWidget(tr("Nodes"), this);
    nodes->setObjectName(QStringLiteral("story_nodes_dock"));
    nodes->setWidget(list_);
    addDockWidget(Qt::LeftDockWidgetArea, nodes);

    inspector_scroll_ = new QScrollArea(this);
    inspector_scroll_->setWidgetResizable(true);
    auto* inspector = new QDockWidget(tr("Node"), this);
    inspector->setObjectName(QStringLiteral("story_inspector_dock"));
    inspector->setWidget(inspector_scroll_);
    addDockWidget(Qt::RightDockWidgetArea, inspector);
    resizeDocks({inspector}, {380}, Qt::Horizontal);
    build_menus();
}

StoryWindow::~StoryWindow() = default;

void StoryWindow::build_menus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Save"), this, [this] { save(); })->setShortcut(QKeySequence::Save);
    file->addAction(tr("&Close"), this, [this] { close(); })->setShortcut(QKeySequence::Close);
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    undo_ = edit->addAction(tr("&Undo"), this, [this] { undo(); });
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = edit->addAction(tr("&Redo"), this, [this] { redo(); });
    redo_->setShortcut(QKeySequence::Redo);
    QMenu* node = menuBar()->addMenu(tr("&Node"));
    QAction* fresh = node->addAction(tr("&New node after this one"), this, [this] { new_node(); });
    fresh->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    fresh->setToolTip(tr("A new line below the chosen one, which leads to it"));
    QAction* gone = node->addAction(tr("&Delete node"), this, [this] { delete_node(); });
    gone->setShortcut(QKeySequence::Delete);
    QAction* start = node->addAction(tr("Make it the &start"), this, [this] {
        std::string error;
        if (editor_ != nullptr && editor_->set_start(selected_, error)) {
            refresh();
        } else if (!error.empty()) {
            report(text_of(error));
        }
    });
    QAction* checking = node->addAction(tr("&Check the conversation"), this, [this] { check(); });
    checking->setShortcut(QKeySequence(QStringLiteral("F7")));
    QToolBar* tools = addToolBar(tr("Story"));
    tools->setObjectName(QStringLiteral("story_tools"));
    for (QAction* action : {fresh, gone, start, checking, undo_, redo_}) {
        tools->addAction(action);
    }
}

// ----- Opening and saving -----

void StoryWindow::edit(game::Conversation conversation, const fs::path& file) {
    editor_ = std::make_unique<StoryEditor>(std::move(conversation));
    file_ = file;
    selected_ = editor_->conversation().start;
    refresh();
}

bool StoryWindow::open(const fs::path& file, std::string& error) {
    std::optional<game::Conversation> loaded = StoryEditor::load(file, error);
    if (!loaded) {
        return false;
    }
    edit(std::move(*loaded), file);
    return true;
}

bool StoryWindow::save() {
    if (editor_ == nullptr) {
        return false;
    }
    std::string error;
    if (!StoryEditor::save(editor_->conversation(), file_, error)) {
        report(tr("The conversation could not be saved: %1").arg(text_of(error)));
        return false;
    }
    editor_->mark_saved();
    refresh_title();
    const std::vector<std::string> problems = game::check_conversation(editor_->conversation());
    statusBar()->showMessage(
        problems.empty() ? tr("Saved")
                         : tr("Saved, with something to fix: %1").arg(text_of(problems.front())),
        6000);
    Q_EMIT saved(file_);
    return true;
}

void StoryWindow::undo() {
    if (editor_ != nullptr && editor_->undo()) {
        refresh();
    }
}

void StoryWindow::redo() {
    if (editor_ != nullptr && editor_->redo()) {
        refresh();
    }
}

std::string StoryWindow::new_node() {
    if (editor_ == nullptr) {
        return {};
    }
    const game::TalkNode* from = editor_->node(selected_);
    const double x = from != nullptr ? from->x + node_width + 60.0 : 0.0;
    const double y = from != nullptr ? from->y : 0.0;
    // Said by whoever said the line before, as conversations go back and forth.
    const std::string id =
        editor_->add_node(x, y, from != nullptr ? from->speaker : std::string(), selected_);
    selected_ = id;
    refresh();
    return id;
}

bool StoryWindow::delete_node() {
    std::string error;
    if (editor_ == nullptr || !editor_->remove_node(selected_, error)) {
        if (!error.empty()) {
            report(text_of(error));
        }
        return false;
    }
    selected_ = editor_->conversation().start;
    refresh();
    return true;
}

std::vector<std::string> StoryWindow::check() {
    if (editor_ == nullptr) {
        return {};
    }
    std::vector<std::string> problems = game::check_conversation(editor_->conversation());
    statusBar()->showMessage(problems.empty() ? tr("Nothing wrong: every way leads somewhere.")
                                              : text_of(problems.front()),
                             8000);
    return problems;
}

void StoryWindow::report(const QString& message) {
    statusBar()->showMessage(message, 8000);
    if (!quiet_ && !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        QMessageBox::warning(this, tr("Story"), message);
    }
}

void StoryWindow::select(const std::string& id) {
    if (editor_ == nullptr || editor_->node(id) == nullptr) {
        return;
    }
    selected_ = id;
    refreshing_ = true;
    for (QGraphicsItem* item : scene_->items()) {
        if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) {
            item->setSelected(utf8(item->data(id_key).toString()) == id);
        }
    }
    for (int i = 0; i < list_->count(); ++i) {
        if (utf8(list_->item(i)->text().section(QLatin1Char(' '), 0, 0)) == id) {
            list_->setCurrentRow(i);
        }
    }
    refreshing_ = false;
    refresh_inspector();
}

// ----- Showing -----

void StoryWindow::refresh() {
    if (editor_ == nullptr) {
        return;
    }
    if (editor_->node(selected_) == nullptr) {
        selected_ = editor_->conversation().start;
    }
    refresh_graph();
    refresh_list();
    refresh_inspector();
    refresh_title();
}

void StoryWindow::refresh_title() {
    setWindowTitle(
        tr("%1%2 — Story")
            .arg(path_text(file_.filename()))
            .arg(editor_ != nullptr && editor_->modified() ? QStringLiteral(" *") : QString()));
    undo_->setEnabled(editor_ != nullptr && editor_->can_undo());
    redo_->setEnabled(editor_ != nullptr && editor_->can_redo());
    undo_->setText(editor_ != nullptr && editor_->can_undo()
                       ? tr("&Undo %1").arg(text_of(editor_->undo_text()))
                       : tr("&Undo"));
    redo_->setText(editor_ != nullptr && editor_->can_redo()
                       ? tr("&Redo %1").arg(text_of(editor_->redo_text()))
                       : tr("&Redo"));
}

void StoryWindow::refresh_list() {
    refreshing_ = true;
    list_->clear();
    for (const game::TalkNode& node : editor_->conversation().nodes) {
        list_->addItem(
            text_of(node.id) + QStringLiteral("  ") +
            (node.speaker.empty() ? QString() : text_of(node.speaker) + QStringLiteral(": ")) +
            excerpt(node.text, 40));
        if (node.id == selected_) {
            list_->setCurrentRow(list_->count() - 1);
        }
    }
    refreshing_ = false;
}

void StoryWindow::refresh_graph() {
    refreshing_ = true;
    scene_->clear();
    const game::Conversation& c = editor_->conversation();
    const QFont small = QApplication::font();
    // The arrows first, so the boxes sit over their ends.
    for (const game::TalkNode& node : c.nodes) {
        const QPointF out(node.x + node_width, node.y + (node_height / 2.0));
        const auto into = [](const game::TalkNode& target) {
            return QPointF(target.x, target.y + (node_height / 2.0));
        };
        if (node.choices.empty()) {
            if (const game::TalkNode* next = c.find(node.next)) {
                scene_->addPath(arrow(out, into(*next)), QPen(QColor(120, 120, 130), 2));
            }
        }
        for (std::size_t i = 0; i < node.choices.size(); ++i) {
            const game::TalkChoice& choice = node.choices[i];
            const QPointF from = out + QPointF(0, (static_cast<double>(i) * 12.0) - 12.0);
            const game::TalkNode* next = c.find(choice.next);
            const QPointF to = next != nullptr ? into(*next) : from + QPointF(80, 0);
            scene_->addPath(arrow(from, to), QPen(QColor(70, 120, 210), 2,
                                                  next != nullptr ? Qt::SolidLine : Qt::DashLine));
            auto* label = scene_->addSimpleText(excerpt(choice.text, 24), small);
            label->setBrush(QColor(50, 90, 180));
            label->setPos((from + to) / 2.0 + QPointF(-30, -16));
        }
    }
    for (const game::TalkNode& node : c.nodes) {
        const bool start = node.id == c.start;
        auto* box =
            scene_->addRect(0, 0, node_width, node_height,
                            QPen(start ? QColor(40, 160, 70) : QColor(90, 90, 100), start ? 3 : 1),
                            QColor(250, 248, 240));
        box->setPos(node.x, node.y);
        box->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        box->setData(id_key, text_of(node.id));
        box->setToolTip(text_of(node.text));
        QFont bold = small;
        bold.setBold(true);
        auto* title = new QGraphicsSimpleTextItem(
            fitted(text_of(node.id) + (start ? tr("  (start)") : QString()), bold,
                   node_width - 80.0),
            box);
        title->setFont(bold);
        title->setPos(6, 4);
        auto* who = new QGraphicsSimpleTextItem(
            fitted(node.speaker.empty() ? tr("(no speaker)") : text_of(node.speaker), small,
                   node_width - 12.0),
            box);
        who->setBrush(QColor(150, 90, 20));
        who->setPos(6, 22);
        auto* words = new QGraphicsSimpleTextItem(
            fitted(text_of(node.text).simplified(), small, node_width - 12.0), box);
        words->setPos(6, 42);
        if (!node.conditions.empty() || !node.effects.empty()) {
            auto* marks =
                new QGraphicsSimpleTextItem((node.conditions.empty() ? QString() : tr("if… ")) +
                                                (node.effects.empty() ? QString() : tr("does…")),
                                            box);
            marks->setBrush(QColor(110, 60, 160));
            marks->setPos(node_width - 70, 4);
        }
        box->setSelected(node.id == selected_);
    }
    refreshing_ = false;
}

bool StoryWindow::eventFilter(QObject* watched, QEvent* event) {
    // A drag of boxes ended: their new places are one step of undo.
    if (watched == view_->viewport() && event->type() == QEvent::MouseButtonRelease &&
        editor_ != nullptr) {
        bool moved = false;
        for (QGraphicsItem* item : scene_->items()) {
            if (!item->flags().testFlag(QGraphicsItem::ItemIsMovable)) {
                continue;
            }
            const std::string id = utf8(item->data(id_key).toString());
            moved =
                editor_->move_node(id, std::round(item->pos().x()), std::round(item->pos().y())) ||
                moved;
        }
        editor_->end_merge();
        if (moved) {
            QTimer::singleShot(0, this, [this] {
                refresh_graph();
                refresh_title();
            });
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

QComboBox* StoryWindow::asset_box(QWidget* parent, const std::vector<std::string>& extensions,
                                  const std::string& now, const QString& none) const {
    auto* box = new QComboBox(parent);
    box->setEditable(true);
    box->addItem(QString());
    box->lineEdit()->setPlaceholderText(none);
    // The project's assets folder: the one the conversation is in, or above it.
    std::filesystem::path root = file_.parent_path();
    while (!root.empty() && root.filename() != "assets" && root != root.parent_path()) {
        root = root.parent_path();
    }
    std::error_code ec;
    if (root.filename() == "assets" && std::filesystem::is_directory(root, ec)) {
        QStringList found;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            std::string extension = it->path().extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (!extension.empty() &&
                std::ranges::find(extensions, extension.substr(1)) != extensions.end()) {
                const std::u8string relative =
                    it->path().lexically_relative(root).generic_u8string();
                found << QString::fromUtf8(reinterpret_cast<const char*>(relative.data()), // NOLINT
                                           static_cast<qsizetype>(relative.size()));
            }
        }
        found.sort();
        box->addItems(found);
    }
    box->setCurrentText(text_of(now));
    return box;
}

void StoryWindow::change_speaker(const std::string& name,
                                 const std::function<void(game::Speaker&)>& change) {
    if (refreshing_ || editor_ == nullptr) {
        return;
    }
    const game::Speaker* now = editor_->conversation().speaker(name);
    if (now == nullptr) {
        return;
    }
    game::Speaker next = *now;
    change(next);
    if (editor_->set_speaker(name, next)) {
        refresh_title();
    }
}

void StoryWindow::change_node(const std::function<void(game::TalkNode&)>& change, bool structure) {
    if (refreshing_ || editor_ == nullptr) {
        return;
    }
    const game::TalkNode* now = editor_->node(selected_);
    if (now == nullptr) {
        return;
    }
    game::TalkNode next = *now;
    change(next);
    std::string error;
    if (editor_->set_node(selected_, next, error)) {
        refresh_graph();
        refresh_list();
        refresh_title();
        if (structure) {
            // Not now: this may run from one of the inspector's own widgets.
            QTimer::singleShot(0, this, [this] { refresh_inspector(); });
        }
    } else if (!error.empty()) {
        report(text_of(error));
    }
}

void StoryWindow::refresh_inspector() {
    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("story_inspector"));
    auto* form = new QFormLayout(panel);
    inspector_ = panel;
    const game::TalkNode* node = editor_ != nullptr ? editor_->node(selected_) : nullptr;
    if (node == nullptr) {
        form->addRow(new QLabel(tr("Choose a node in the graph."), panel));
        inspector_scroll_->setWidget(panel);
        return;
    }
    const game::Conversation& c = editor_->conversation();
    // The ids a "next" can name, for lists.
    QStringList targets{tr("(the conversation ends)")};
    for (const game::TalkNode& n : c.nodes) {
        targets << text_of(n.id);
    }
    const auto target_box = [&](const std::string& now) {
        auto* box = new QComboBox(panel);
        box->addItems(targets);
        box->setCurrentIndex(
            now.empty() ? 0 : std::max(0, static_cast<int>(targets.indexOf(text_of(now)))));
        return box;
    };
    const auto chosen_target = [](const QComboBox* box) {
        return box->currentIndex() <= 0 ? std::string() : utf8(box->currentText());
    };

    auto* name = new QLineEdit(text_of(node->id), panel);
    name->setObjectName(QStringLiteral("node_name"));
    name->setToolTip(tr("What the node is called; logic can start a conversation here"));
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
        std::string error;
        const std::string wanted = utf8(name->text().trimmed());
        if (editor_->rename_node(selected_, wanted, error)) {
            selected_ = wanted;
            QTimer::singleShot(0, this, [this] { refresh(); });
        } else if (!error.empty()) {
            report(text_of(error));
            name->setText(text_of(selected_));
        }
    });
    form->addRow(tr("Name"), name);

    auto* speaker = new QComboBox(panel);
    speaker->setObjectName(QStringLiteral("node_speaker"));
    speaker->setEditable(true);
    speaker->addItem(QString());
    for (const game::Speaker& s : c.speakers) {
        speaker->addItem(text_of(s.name));
    }
    speaker->setCurrentText(text_of(node->speaker));
    speaker->setToolTip(tr("Who says it: their name shows above the message. Type a new name "
                           "to add a speaker"));
    connect(speaker, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        change_node([&](game::TalkNode& n) { n.speaker = utf8(text.trimmed()); });
    });
    // Once the speaker is settled, their look shows below (rebuilt only if it changed, so
    // a click elsewhere keeps its focus).
    const std::string shown_look =
        c.speaker(node->speaker) != nullptr ? node->speaker : std::string();
    const auto settle = [this, speaker, shown_look] {
        if (editor_->add_speaker(utf8(speaker->currentText().trimmed()))) {
            refresh_title();
        }
        editor_->end_merge();
        const game::TalkNode* now = editor_->node(selected_);
        const std::string wanted = now != nullptr && editor_->conversation().speaker(now->speaker)
                                       ? now->speaker
                                       : std::string();
        if (wanted != shown_look) {
            QTimer::singleShot(0, this, [this] { refresh_inspector(); });
        }
    };
    connect(speaker->lineEdit(), &QLineEdit::editingFinished, this, settle);
    connect(speaker, &QComboBox::activated, this, settle);
    form->addRow(tr("Speaker"), speaker);

    // How the speaker looks and sounds, in all their lines (of this conversation, and when
    // Rules make them speak).
    if (const game::Speaker* who = c.speaker(node->speaker)) {
        const QString who_name = text_of(who->name); // Qt captures stay cheap to copy
        form->addRow(
            new QLabel(tr("<b>%1</b> in every line").arg(who_name.toHtmlEscaped()), panel));
        auto* colour = new QPushButton(text_of(who->color), panel);
        colour->setObjectName(QStringLiteral("speaker_color"));
        colour->setToolTip(tr("The colour of their name on the plate above the message"));
        colour->setStyleSheet(QStringLiteral("QPushButton { color: %1; font-weight: bold; }")
                                  .arg(text_of(who->color)));
        connect(colour, &QPushButton::clicked, this, [this, colour, who_name] {
            const QColor picked =
                QColorDialog::getColor(QColor(colour->text()), this, tr("Name plate colour"));
            if (picked.isValid()) {
                const QString hex = picked.name(QColor::HexRgb);
                change_speaker(utf8(who_name), [&](game::Speaker& s) { s.color = utf8(hex); });
                colour->setText(hex);
                colour->setStyleSheet(
                    QStringLiteral("QPushButton { color: %1; font-weight: bold; }").arg(hex));
            }
        });
        form->addRow(tr("Name colour"), colour);
        auto* portrait = asset_box(panel, {"png"}, who->portrait, tr("(none)"));
        portrait->setObjectName(QStringLiteral("speaker_portrait"));
        portrait->setToolTip(tr("A picture in assets/ standing on the message box while they "
                                "speak"));
        connect(portrait, &QComboBox::currentTextChanged, this, [this, who_name](const QString& t) {
            change_speaker(utf8(who_name),
                           [&](game::Speaker& s) { s.portrait = utf8(t.trimmed()); });
        });
        form->addRow(tr("Portrait"), portrait);
        auto* voice = asset_box(panel, {"wav", "ogg", "flac", "mp3"}, who->voice, tr("(silent)"));
        voice->setObjectName(QStringLiteral("speaker_voice"));
        voice->setToolTip(tr("A short sound in assets/ blipped as their words are typed (needs a "
                             "text speed above 0 in the Project view)"));
        connect(voice, &QComboBox::currentTextChanged, this, [this, who_name](const QString& t) {
            change_speaker(utf8(who_name), [&](game::Speaker& s) { s.voice = utf8(t.trimmed()); });
        });
        form->addRow(tr("Voice"), voice);
        auto* pitch = new QDoubleSpinBox(panel);
        pitch->setObjectName(QStringLiteral("speaker_pitch"));
        pitch->setRange(0.25, 4.0);
        pitch->setSingleStep(0.05);
        pitch->setValue(who->pitch);
        pitch->setToolTip(tr("How high their voice is: 1 as recorded, 2 an octave up"));
        connect(pitch, &QDoubleSpinBox::valueChanged, this, [this, who_name](double v) {
            change_speaker(utf8(who_name), [&](game::Speaker& s) { s.pitch = v; });
        });
        form->addRow(tr("Voice pitch"), pitch);
    }

    auto* words = new QPlainTextEdit(text_of(node->text), panel);
    words->setObjectName(QStringLiteral("node_text"));
    words->setTabChangesFocus(true);
    words->setMinimumHeight(90);
    connect(words, &QPlainTextEdit::textChanged, this, [this, words] {
        change_node([&](game::TalkNode& n) { n.text = utf8(words->toPlainText()); });
    });
    form->addRow(tr("Says"), words);

    auto* mood = asset_box(panel, {"png"}, node->portrait, tr("(the speaker's own)"));
    mood->setObjectName(QStringLiteral("node_portrait"));
    mood->setToolTip(tr("Another picture of the speaker for this line only: a mood, such as "
                        "glad or angry"));
    connect(mood, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        change_node([&](game::TalkNode& n) { n.portrait = utf8(t.trimmed()); });
    });
    form->addRow(tr("Portrait here"), mood);

    auto* next = target_box(node->next);
    next->setObjectName(QStringLiteral("node_next"));
    next->setToolTip(tr("What comes after, when the node offers no choices"));
    connect(next, &QComboBox::currentIndexChanged, this, [this, next, chosen_target] {
        change_node([&](game::TalkNode& n) { n.next = chosen_target(next); });
    });
    form->addRow(tr("Then"), next);

    // Choices: words, where each leads, and an item it needs.
    form->addRow(new QLabel(tr("<b>Choices</b> (the player picks one)"), panel));
    for (std::size_t i = 0; i < node->choices.size(); ++i) {
        const game::TalkChoice& choice = node->choices[i];
        auto* row = new QWidget(panel);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        auto* text = new QLineEdit(text_of(choice.text), row);
        text->setObjectName(QStringLiteral("choice_text:%1").arg(i));
        auto* leads = target_box(choice.next);
        leads->setObjectName(QStringLiteral("choice_next:%1").arg(i));
        std::string needs;
        for (const game::TalkCondition& cond : choice.conditions) {
            if (cond.kind == game::TalkCondition::Kind::HasItem && !cond.negate) {
                needs = cond.name;
            }
        }
        auto* item = new QLineEdit(text_of(needs), row);
        item->setPlaceholderText(tr("needs item"));
        item->setToolTip(tr("Offered only if the player carries this item; empty for always"));
        item->setMaximumWidth(90);
        auto* remove = new QPushButton(tr("✕"), row);
        remove->setToolTip(tr("Remove this choice"));
        remove->setMaximumWidth(28);
        line->addWidget(text, 2);
        line->addWidget(leads, 2);
        line->addWidget(item, 1);
        line->addWidget(remove);
        connect(text, &QLineEdit::textEdited, this, [this, i](const QString& t) {
            change_node([&](game::TalkNode& n) { n.choices[i].text = utf8(t); });
        });
        connect(leads, &QComboBox::currentIndexChanged, this, [this, i, leads, chosen_target] {
            change_node([&](game::TalkNode& n) { n.choices[i].next = chosen_target(leads); });
        });
        connect(item, &QLineEdit::textEdited, this, [this, i](const QString& t) {
            change_node([&](game::TalkNode& n) {
                std::erase_if(n.choices[i].conditions, [](const game::TalkCondition& cond) {
                    return cond.kind == game::TalkCondition::Kind::HasItem && !cond.negate;
                });
                if (!t.trimmed().isEmpty()) {
                    n.choices[i].conditions.push_back(
                        {game::TalkCondition::Kind::HasItem, utf8(t.trimmed()), false});
                }
            });
        });
        connect(remove, &QPushButton::clicked, this, [this, i] {
            if (editor_->remove_choice(selected_, i)) {
                QTimer::singleShot(0, this, [this] { refresh(); });
            }
        });
        form->addRow(QString::number(i + 1), row);
    }
    auto* add_choice = new QPushButton(tr("Add a choice"), panel);
    add_choice->setObjectName(QStringLiteral("add_choice"));
    connect(add_choice, &QPushButton::clicked, this, [this] {
        std::string error;
        if (editor_->add_choice(selected_, error)) {
            QTimer::singleShot(0, this, [this] { refresh(); });
        } else {
            report(text_of(error));
        }
    });
    form->addRow(add_choice);

    // Only if: the node is said only when these hold; otherwise the conversation goes on.
    form->addRow(new QLabel(tr("<b>Only if</b> (else it goes on to Then)"), panel));
    for (std::size_t i = 0; i < node->conditions.size(); ++i) {
        const game::TalkCondition& cond = node->conditions[i];
        auto* row = new QWidget(panel);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        auto* negate = new QCheckBox(tr("not"), row);
        negate->setChecked(cond.negate);
        auto* kind = new QComboBox(row);
        for (const QString& n : condition_names()) {
            kind->addItem(n);
        }
        kind->setCurrentIndex(static_cast<int>(cond.kind));
        auto* what = new QLineEdit(text_of(cond.name), row);
        what->setPlaceholderText(tr("its name"));
        auto* remove = new QPushButton(tr("✕"), row);
        remove->setMaximumWidth(28);
        line->addWidget(negate);
        line->addWidget(kind, 2);
        line->addWidget(what, 2);
        line->addWidget(remove);
        connect(negate, &QCheckBox::toggled, this, [this, i](bool on) {
            change_node([&](game::TalkNode& n) { n.conditions[i].negate = on; });
        });
        connect(kind, &QComboBox::currentIndexChanged, this, [this, i](int k) {
            change_node([&](game::TalkNode& n) {
                n.conditions[i].kind = static_cast<game::TalkCondition::Kind>(std::max(k, 0));
            });
        });
        connect(what, &QLineEdit::textEdited, this, [this, i](const QString& t) {
            change_node([&](game::TalkNode& n) { n.conditions[i].name = utf8(t.trimmed()); });
        });
        connect(remove, &QPushButton::clicked, this, [this, i] {
            change_node(
                [&](game::TalkNode& n) {
                    n.conditions.erase(n.conditions.begin() + static_cast<std::ptrdiff_t>(i));
                },
                true);
        });
        form->addRow(row);
    }
    auto* add_condition = new QPushButton(tr("Add a condition"), panel);
    add_condition->setObjectName(QStringLiteral("add_condition"));
    connect(add_condition, &QPushButton::clicked, this,
            [this] { change_node([](game::TalkNode& n) { n.conditions.push_back({}); }, true); });
    form->addRow(add_condition);

    // Does: what saying it changes.
    form->addRow(new QLabel(tr("<b>Does</b> (when it is said)"), panel));
    for (std::size_t i = 0; i < node->effects.size(); ++i) {
        const game::TalkEffect& effect = node->effects[i];
        auto* row = new QWidget(panel);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        auto* kind = new QComboBox(row);
        for (const QString& n : effect_names()) {
            kind->addItem(n);
        }
        kind->setCurrentIndex(static_cast<int>(effect.kind));
        auto* what = new QLineEdit(text_of(effect.name), row);
        what->setPlaceholderText(tr("its name"));
        auto* value = new QLineEdit(text_of(effect.value), row);
        value->setPlaceholderText(tr("how many / title"));
        value->setToolTip(tr("How many items, or a quest's title"));
        auto* remove = new QPushButton(tr("✕"), row);
        remove->setMaximumWidth(28);
        line->addWidget(kind, 2);
        line->addWidget(what, 2);
        line->addWidget(value, 1);
        line->addWidget(remove);
        connect(kind, &QComboBox::currentIndexChanged, this, [this, i](int k) {
            change_node([&](game::TalkNode& n) {
                n.effects[i].kind = static_cast<game::TalkEffect::Kind>(std::max(k, 0));
            });
        });
        connect(what, &QLineEdit::textEdited, this, [this, i](const QString& t) {
            change_node([&](game::TalkNode& n) { n.effects[i].name = utf8(t.trimmed()); });
        });
        connect(value, &QLineEdit::textEdited, this, [this, i](const QString& t) {
            change_node([&](game::TalkNode& n) { n.effects[i].value = utf8(t.trimmed()); });
        });
        connect(remove, &QPushButton::clicked, this, [this, i] {
            change_node(
                [&](game::TalkNode& n) {
                    n.effects.erase(n.effects.begin() + static_cast<std::ptrdiff_t>(i));
                },
                true);
        });
        form->addRow(row);
    }
    auto* add_effect = new QPushButton(tr("Add an effect"), panel);
    add_effect->setObjectName(QStringLiteral("add_effect"));
    connect(add_effect, &QPushButton::clicked, this,
            [this] { change_node([](game::TalkNode& n) { n.effects.push_back({}); }, true); });
    form->addRow(add_effect);
    inspector_scroll_->setWidget(panel); // the old panel is deleted
}

void StoryWindow::closeEvent(QCloseEvent* event) {
    if (editor_ != nullptr && editor_->modified() &&
        !QApplication::platformName().startsWith(QStringLiteral("offscreen"))) {
        const auto answer = QMessageBox::question(
            this, tr("Story"), tr("Save the changes to %1 first?").arg(path_text(file_.filename())),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

} // namespace editor
