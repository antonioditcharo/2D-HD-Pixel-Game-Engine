// The Story editor's window, driven as a person would on Qt's offscreen platform.

#include "engine/foundation/guid.h"

#include <doctest/doctest.h>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <filesystem>
#include <fstream>
#include <string>

#include "main_window.h"
#include "story_window.h"

namespace fs = std::filesystem;
using engine::Guid;

TEST_CASE("File > New conversation opens the Story editor; nodes are made, edited and saved") {
    const fs::path path =
        fs::temp_directory_path() / ("sparxel-story-app-" + Guid::generate().to_string());
    fs::copy(fs::path(SPARXEL_SAMPLES_DIR) / "hello", path, fs::copy_options::recursive);
    fs::create_directories(path / "assets" / "faces");
    fs::create_directories(path / "assets" / "sfx");
    std::ofstream(path / "assets" / "faces" / "elder.png", std::ios::binary) << "png";
    std::ofstream(path / "assets" / "sfx" / "blip.wav", std::ios::binary) << "wav";
    {
        editor::MainWindow window(SPARXEL_PLAYER);
        window.show();
        REQUIRE(window.open_project(path));
        editor::StoryWindow* story = window.new_story(QStringLiteral("elder"));
        REQUIRE(story != nullptr);
        QApplication::processEvents();
        CHECK(fs::exists(path / "assets" / "talk" / "elder.dtalk"));
        CHECK(story->windowTitle().startsWith(QStringLiteral("elder.dtalk")));
        CHECK(story->selected() == "start");
        CHECK(story->node_list()->count() == 1);

        // Typing the words and the speaker into the inspector.
        auto* words = story->inspector()->findChild<QPlainTextEdit*>(QStringLiteral("node_text"));
        REQUIRE(words != nullptr);
        words->setPlainText(QStringLiteral("Welcome, traveller."));
        auto* speaker = story->inspector()->findChild<QComboBox*>(QStringLiteral("node_speaker"));
        REQUIRE(speaker != nullptr);
        speaker->setCurrentText(QStringLiteral("Elder"));
        CHECK(story->editor()->node("start")->text == "Welcome, traveller.");
        CHECK(story->editor()->node("start")->speaker == "Elder");

        // Settling on the speaker shows how they look and sound: a portrait and a voice
        // from the project's assets, and a pitch.
        Q_EMIT speaker->lineEdit()->editingFinished();
        QApplication::processEvents();
        auto* portrait =
            story->inspector()->findChild<QComboBox*>(QStringLiteral("speaker_portrait"));
        REQUIRE(portrait != nullptr);
        CHECK(portrait->findText(QStringLiteral("faces/elder.png")) > 0);
        portrait->setCurrentText(QStringLiteral("faces/elder.png"));
        auto* voice = story->inspector()->findChild<QComboBox*>(QStringLiteral("speaker_voice"));
        REQUIRE(voice != nullptr);
        CHECK(voice->findText(QStringLiteral("sfx/blip.wav")) > 0);
        voice->setCurrentText(QStringLiteral("sfx/blip.wav"));
        auto* pitch =
            story->inspector()->findChild<QDoubleSpinBox*>(QStringLiteral("speaker_pitch"));
        REQUIRE(pitch != nullptr);
        pitch->setValue(1.5);
        const engine::game::Speaker* elder = story->editor()->conversation().speaker("Elder");
        REQUIRE(elder != nullptr);
        CHECK(elder->portrait == "faces/elder.png");
        CHECK(elder->voice == "sfx/blip.wav");
        CHECK(elder->pitch == doctest::Approx(1.5));
        auto* mood = story->inspector()->findChild<QComboBox*>(QStringLiteral("node_portrait"));
        REQUIRE(mood != nullptr);
        mood->setCurrentText(QStringLiteral("faces/elder.png"));
        CHECK(story->editor()->node("start")->portrait == "faces/elder.png");

        // A node after it, said by the same speaker, and a choice leading back.
        const std::string next = story->new_node();
        CHECK(story->editor()->node("start")->next == next);
        CHECK(story->editor()->node(next)->speaker == "Elder");
        CHECK(story->selected() == next);
        auto* add = story->inspector()->findChild<QPushButton*>(QStringLiteral("add_choice"));
        REQUIRE(add != nullptr);
        add->click();
        QApplication::processEvents();
        auto* leads = story->inspector()->findChild<QComboBox*>(QStringLiteral("choice_next:0"));
        REQUIRE(leads != nullptr);
        leads->setCurrentText(QStringLiteral("start"));
        CHECK(story->editor()->node(next)->choices.at(0).next == "start");
        // The graph: two boxes, chosen by clicking (selecting) one.
        int boxes = 0;
        for (QGraphicsItem* item : story->graph()->items()) {
            boxes += item->flags().testFlag(QGraphicsItem::ItemIsMovable) ? 1 : 0;
        }
        CHECK(boxes == 2);
        story->select("start");
        CHECK(story->selected() == "start");
        // A picture for the docs (docs/dev/story.png), when asked for.
        if (const QString shot = qEnvironmentVariable("SPARXEL_STORY_SCREENSHOT");
            !shot.isEmpty()) {
            story->resize(1200, 700);
            QApplication::processEvents();
            story->grab().save(shot);
        }
        CHECK(story->check().empty());

        REQUIRE(story->save());
        CHECK_FALSE(story->editor()->modified());
        CHECK(window.document()->assets().find("talk/elder.dtalk") != nullptr);
        CHECK(window.open_story(QStringLiteral("talk/elder.dtalk")) == story);
        CHECK(window.new_story(QStringLiteral("elder")) == nullptr); // taken
        story->undo();
        story->close();
    }
    std::error_code ignored;
    fs::remove_all(path, ignored);
}
