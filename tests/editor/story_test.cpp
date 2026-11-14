// The Story editor's model: nodes added, renamed, changed, moved and deleted, choices,
// speakers and the start, with undo; saved and read back.

#include "engine/foundation/guid.h"
#include "engine/game/conversation.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "editor/core/story.h"

namespace fs = std::filesystem;
namespace game = engine::game;

TEST_CASE("Story: nodes are added after others, renamed with what leads to them, and undone") {
    editor::StoryEditor story(editor::StoryEditor::blank());
    CHECK(story.conversation().start == "start");
    CHECK(game::check_conversation(story.conversation()).empty());
    const std::string second = story.add_node(260, 0, "Elder", "start");
    CHECK(second == "node_1");
    CHECK(story.node("start")->next == "node_1");
    CHECK(story.undo_text() == "Add node_1");
    std::string error;
    CHECK(story.rename_node("node_1", "ask", error));
    CHECK(story.node("start")->next == "ask");
    CHECK_FALSE(story.rename_node("ask", "has space", error));
    CHECK(error == "A node's name is letters, digits and _ only, such as ask_about_cat.");
    CHECK_FALSE(story.rename_node("ask", "start", error));
    CHECK(story.rename_node("start", "hello", error));
    CHECK(story.conversation().start == "hello");

    // Typing into a node merges into one step; a choice leads to another node.
    game::TalkNode ask = *story.node("ask");
    ask.text = "W";
    CHECK(story.set_node("ask", ask, error));
    ask.text = "Will you help?";
    CHECK(story.set_node("ask", ask, error));
    story.end_merge();
    CHECK(story.undo_text() == "Change ask");
    const std::optional<std::size_t> yes = story.add_choice("ask", error);
    REQUIRE(yes.has_value());
    if (!yes) {
        return;
    }
    game::TalkNode with_choice = *story.node("ask");
    with_choice.choices[*yes].next = "nowhere";
    CHECK_FALSE(story.set_node("ask", with_choice, error));
    CHECK(error == "That leads to a node the conversation does not have.");
    with_choice.choices[*yes].next = "hello";
    CHECK(story.set_node("ask", with_choice, error));
    CHECK(story.move_node("ask", 300, 40));
    CHECK(story.move_node("ask", 320, 60));
    CHECK(story.undo_text() == "Move ask");
    CHECK(story.add_speaker("Elder"));
    CHECK_FALSE(story.add_speaker("Elder"));

    // Deleting a node: what led to it ends; the start is kept.
    CHECK_FALSE(story.remove_node("hello", error));
    CHECK(error == "The conversation starts here. Make another node the start first.");
    CHECK(story.set_start("ask", error));
    CHECK(story.remove_node("hello", error));
    CHECK(story.node("ask")->choices[0].next.empty());
    CHECK(story.undo());
    CHECK(story.node("ask")->choices[0].next == "hello");
    CHECK(story.redo());

    // Undoing everything gives the blank conversation back.
    while (story.can_undo()) {
        story.undo();
    }
    CHECK(story.conversation() == editor::StoryEditor::blank());
    CHECK_FALSE(story.modified());
}

TEST_CASE("Story: conversations are saved and read back the same") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-story-" + engine::Guid::generate().to_string());
    fs::create_directories(folder);
    editor::StoryEditor story(editor::StoryEditor::blank());
    story.add_node(200, 50, "Mira", "start");
    std::string error;
    REQUIRE(editor::StoryEditor::save(story.conversation(), folder / "mira.dtalk", error));
    const std::optional<game::Conversation> back =
        editor::StoryEditor::load(folder / "mira.dtalk", error);
    REQUIRE(back.has_value());
    CHECK(back.value_or(game::Conversation{}) == story.conversation());
    CHECK_FALSE(editor::StoryEditor::load(folder / "none.dtalk", error).has_value());
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}

TEST_CASE("Story: a speaker's look is changed with undo, typing as one step") {
    editor::StoryEditor story(editor::StoryEditor::blank());
    REQUIRE(story.add_speaker("Elder"));
    game::Speaker look = *story.conversation().speaker("Elder");
    look.name = "Someone else"; // the name stays
    look.portrait = "f";
    CHECK(story.set_speaker("Elder", look));
    look.portrait = "faces/elder.png";
    look.pitch = 9.0; // kept to 4
    CHECK(story.set_speaker("Elder", look));
    const game::Speaker* elder = story.conversation().speaker("Elder");
    REQUIRE(elder != nullptr);
    CHECK(elder->portrait == "faces/elder.png");
    CHECK(elder->pitch == 4.0);
    CHECK_FALSE(story.set_speaker("Nobody", look));
    CHECK(story.undo()); // both changes, one step
    CHECK(story.conversation().speaker("Elder")->portrait.empty());
}
