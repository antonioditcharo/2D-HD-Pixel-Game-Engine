# Localization

> **Status: v0.1.** Games in other languages: the texts a game shows collected into one
> translations table with a key each, filled in by translators in the Table window or a
> spreadsheet, and shown in the language the project or the player chooses, with a pseudo
> language for checking. Tested on its own, in a running game, and in the editor.
>
> Code: [`engine/game/include/engine/game/localization.h`](../../engine/game/include/engine/game/localization.h)
> and [`src/localization.cpp`](../../engine/game/src/localization.cpp); collecting in
> [`editor/core/include/editor/core/document.h`](../../editor/core/include/editor/core/document.h)
> (`texts_to_translate()`, `collect_translations()`). Tests:
> [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp) and
> [`tests/editor/document_test.cpp`](../../tests/editor/document_test.cpp).

## 1. What it is for

A game written once can be played in many languages (`plan/02` G7). Creators write their
words as usual, in Rules and conversations; translators get every one of them in a single
table, and nothing in the game's logic changes.

## 2. Translating a game

1. In the Project view's **Languages** group, **Translate into** lists the languages, as
   codes separated by commas (*fr, de, ja*; `languages` in the project file).
2. **Game ▸ Collect texts to translate** puts every text the game shows in
   `assets/data/translations.dtable` (made if missing) and opens it in the Table window
   ([tables.md](tables.md)): a row per text, named by a key made from its words
   (*t_1a2b3c4d*), the words as written in **source**, and a column per language. Run it
   again whenever words are added: new texts get rows, and translations already written
   stay. A **notes** column, if added, is for translators and is never shown.
3. Translators fill in the language columns there, or in a spreadsheet: the Table window
   exports CSV and reads it back.
4. **Shown in** (`language`) is the language the game starts in: a language column, or
   empty for the words as written. Players choose another on the pause menu's Options page
   (*Language*: *The game's own*, then each language column, kept with their options:
   [menus.md](menus.md)).

What is collected, each once, in the order found:

| From | Texts |
|---|---|
| Conversations ([story.md](story.md)) | Speakers' names, each line, each choice |
| Rules, Sequences and Brains | What *Say*, *Ask*, *{speaker} says* and *{speaker} asks* show, when written as text, and each of the answers (separated by `|`) |
| The menus | The title screen's and the pause menu's choices |

## 3. Playing in another language

Messages and questions are shown translated as they come; each answer is translated on its
own; speakers' name plates and the menus' choices are translated where they are drawn. A
text without a translation shows as written, so a half-translated game still plays.
Logic is not changed: *The answer's words* is the answer as written (*Yes*, not *Oui*), so
Rules that compare it work in every language, and the log of what was said keeps the words
as written.

**The pseudo language.** With **Shown in** set to *pseudo*, every text shows with its
vowels doubled, in brackets (*[Heelloo, traaveelleer!]*): text without brackets was not
collected (built from parts, say), and text that no longer fits shows where a longer
language would overflow. `[tags]` of rich text and `{names}` stay as they are.

## 4. Next increments

1. **Everything else translated**: item, skill and enemy names from the data tables, the
   battle's messages, and the rest of the menus' words.
2. **Fonts per language**, for letters the built-in pixel font lacks (accents, kana).
3. **XLIFF** for translation tools, and an in-editor view of each text where it is used.
