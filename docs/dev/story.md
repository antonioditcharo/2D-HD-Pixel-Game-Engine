# Story: conversations

> **Status: v0.1.** Conversations as graphs of nodes, made in the editor's Story window and
> played by the game through its message boxes, with each speaker's name above the box,
> their portrait standing on it and their voice blipping as the words are typed.
> Choices, nodes said only if something holds, and effects on the inventory, the quests
> and the story's flags. Tested on their own, in a running game, and in the editor.
>
> Code: [`engine/game/include/engine/game/conversation.h`](../../engine/game/include/engine/game/conversation.h)
> (the format and the player), [`dialogue.h`](../../engine/game/include/engine/game/dialogue.h)
> (speakers, flags, starting one), [`editor/core/include/editor/core/story.h`](../../editor/core/include/editor/core/story.h)
> (editing with undo) and [`editor/app/src/story_window.h`](../../editor/app/src/story_window.h)
> (the window). Tests: [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp),
> [`tests/editor/story_test.cpp`](../../tests/editor/story_test.cpp) and
> [`tests/editor_app/story_test.cpp`](../../tests/editor_app/story_test.cpp).

## 1. What it is for

Talking to people is most of an RPG (`plan/02` G1, G3). Rules can already say and ask
things one at a time ([ui.md](ui.md)); a conversation puts a whole exchange in one place,
as the dialogue tools of other engines do: who says what, what the player can answer, where
each answer leads, and what it changes.

## 2. Making one

**File ▸ New conversation…** makes `assets/talk/<name>.dtalk` with one node, *start*, and
opens it in the **Story** window; double-clicking a `.dtalk` in the Assets (or finding it
with Ctrl+K) opens one. Each conversation has its own window.

| Part | What it does |
|---|---|
| **Graph** (middle) | Each node is a box: its name (*(start)* on the one it starts with, outlined in green), who says it, and its words. A grey arrow leads to the node after it; a blue arrow for each choice, labelled with the choice's words (dashed when it leads nowhere yet). *if…* and *does…* mark nodes with conditions and effects. Boxes drag anywhere (one undo step per drag); clicking one chooses it |
| **Nodes** (left) | Every node with its speaker and the start of its words; clicking one chooses it |
| **Node** (right) | The chosen node: its **Name** (renaming it renames every arrow to it; letters, digits and _), **Speaker** (pick one, or type a new name, which adds it), and how that speaker looks and sounds in all their lines: **Name colour** (the plate's, picked from a colour dialog), **Portrait** (a picture in assets/), **Voice** (a short sound in assets/) and **Voice pitch** (0.25 to 4; 1 as recorded). Then what it **Says**, **Portrait here** (another picture of the speaker for this line only: a mood), and **Then** (the node after it, or *the conversation ends*). **Choices**: each with its words, where it leads, and an item it **needs** (offered only if the player carries it); *Add a choice*. **Only if**: conditions that must all hold for the node to be said (the player has an item, a flag is set, a quest is under way or complete, each with *not*); otherwise the conversation goes on to *Then*. **Does**: effects as it is said (give or take items, set or clear a flag, start or complete a quest; *how many* is a number of items, or a quest's title) |
| **Commands** | *New node after this one* (Ctrl+Enter: below the chosen node, which now leads to it, said by the same speaker), *Delete node* (Delete; what led to it now ends there; the start node stays), *Make it the start*, *Check the conversation* (F7), Undo and Redo, Save (Ctrl+S) and Close (asking to save) |

Typing into a field is one undo step; so is each drag. *Check the conversation*, and
saving, say in the status bar what would go wrong: a way that leads to a node that is not
there, two nodes with one name, a start that is not a node, a node nothing leads to.
Project Doctor ([editor.md](editor.md)) lists the same for every conversation of the
project.

## 3. Playing one

Logic starts a conversation with the dialogue object's *Start the conversation {file}*
(`talk/elder.dtalk`), or *Start the conversation {file} at {node}*. Each step, once no
message is open, the game goes on:

1. To the node after the one shown, or, after a question, to where the answer picked leads.
2. A node whose conditions do not all hold is passed over, to its *Then* (at most 256 in a
   row, so a ring of such nodes cannot hang the game).
3. Its effects are done.
4. It is shown: as a message, or, with choices whose conditions hold, as a question with
   those answers (a `|` in a choice's words shows as `/`, since `|` separates answers). The
   speaker's name is on a plate above the box's left end.
5. With nowhere left to go, the conversation ends, and Rules on the dialogue's *When a
   conversation ends* run, with its file as {name}.

A file that is not there, or not a conversation, is reported in the game's problems.
Rules on *When the message closes* still run between a conversation's messages.

**Flags** are the story's yes/no facts (*met_the_elder*): conversations set, clear and
test them, and so do Rules (*Set the flag {name}*, *Clear the flag {name}*, *The flag
{name} is set*). Saved games keep them; loading stops a conversation that was playing.

**Speakers** in Rules: *{speaker} says {text}* shows a message with a name plate too.

**How speakers look and sound.** The name plate is in the speaker's colour. Their portrait
stands on the message box's right end, as large as it is drawn (smaller only if the screen
has no room above the box), so pixel portraits stay crisp; a node's own portrait replaces
it for that line. While the words are typed (a text speed above 0 in the Project view, or
chosen by the player on the Options page: [menus.md](menus.md)), the speaker's voice
plays every other letter (spaces do not count), at most once a step, its pitch varied a
little so it sounds like talk; showing the page whole does not play a burst. A speaker's
look comes from the conversation playing; Rules' *{speaker} says* uses the look that
speaker has in the project's conversations (the first one found).

## 4. The file

A `.dtalk` file is JSON (`format` *sparxel.conversation*, `version` 1), with sorted keys,
so the same conversation is always the same text: `start`, `speakers` (`name`, plate
`color`, and, only if set, `portrait`, `voice` and `pitch`), and `nodes`, each with `id`, `speaker`, `text`, `next`, `choices` (`text`,
`next`, `if`), `portrait` (only if set), `if` (conditions: `test` *has_item*, *flag*, *quest_active* or
*quest_done*, `name`, `not`), `do` (effects: `effect` *give_item*, *take_item*,
*set_flag*, *clear_flag*, *start_quest* or *complete_quest*, `name`, `value`) and its
place in the graph (`x`, `y`). Unknown tests and effects are passed over; nodes without an
id are dropped; at most 2,000 nodes. Exported games carry `.dtalk` files, as the game reads
them.

## 5. Next increments

1. **A project-wide cast** (G3): speakers kept once for every conversation, with portraits
   animated (blinking, talking) from Sprite Studio.
3. **More conditions and effects**: variables, numbers of items, any action of the shared
   logic vocabulary.
4. **Playing from a node in the editor**, and a list of the conversation's lines for
   translation (G7).
