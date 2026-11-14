# UI: text and dialogue

> **Status: v0.1.** A built-in pixel font, so every game can show text without a font
> asset; measuring and word-wrapping; pixel-exact text drawing; and the **dialogue**
> object, whose *"Say [text]"* action shows a message box that the player reads and closes
> with Interact while the game waits, and whose *"Ask"* action offers answers to pick
> from. Tested with exact pixels.
>
> Code: [`engine/ui/include/engine/ui/text.h`](../../engine/ui/include/engine/ui/text.h),
> [`src/text.cpp`](../../engine/ui/src/text.cpp), and the dialogue object in
> [`engine/game/include/engine/game/dialogue.h`](../../engine/game/include/engine/game/dialogue.h).
> Tests: [`tests/ui/text_test.cpp`](../../tests/ui/text_test.cpp) and the dialogue tests in
> [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp).

## 1. What it is for

RPGs are told in words: the elder's greeting, a sign by the road, the name of an item.
ADR-010 makes **bitmap pixel fonts** first-class, because text that is not on the same
pixel grid as the art looks wrong in a pixel-art game. This module starts there:

- a font built into the engine, drawn in its source, so text works in a new project with
  nothing imported;
- text measured and wrapped in whole pixels;
- a message box that no-code logic opens with one action.

Imported fonts (FreeType and HarfBuzz, ADR-010), portraits and the UI editor come later
(§5).

## 2. Text

```cpp
const ui::PixelFont& font = ui::PixelFont::builtin();
auto atlas = device.create_texture(font.atlas_width(), font.atlas_height(), font.atlas());

int width = ui::measure(font, "Hi");                      // 9 pixels
std::vector<std::string> lines = ui::wrap(font, text, 120); // lines of at most 120 pixels

sprites.begin_over(target);                                // draw over what is there
ui::draw_text(sprites, *atlas, font, lines[0], 8, 8, render::Color{255, 255, 255, 255});
sprites.end();
```

**The built-in font** covers printable ASCII (95 characters, space to `~`). Anything else,
and malformed UTF-8, shows as `?`.

- Each glyph is drawn in a 5 × 8 cell: rows 0 to 6 stand on the baseline, and row 7 holds
  the descenders of g, j, p, q and y. Lines are 9 pixels apart.
- The font is **proportional**: each glyph is as wide as its pixels, plus one pixel of
  space after it (`H` moves the pen 6 pixels, `i` 4, `.` 3). A space is 4 pixels.
- Its **atlas** is one row of white glyphs on transparent pixels, so the colour passed to
  `draw_text` tints it. It is built when the font is first used, from bit rows in
  `text.cpp`.

**A game's own font** (feature G7): the Project view's *Style guide* chooses a picture of
the project as the **Font**, and names **Its letters** in the order drawn (empty: the space
to `~` in ASCII order). The picture is a strip of letters side by side, each separated from
the next by a column of one colour, the colour of its top-left pixel, as Love2D's and
Construct's image fonts are. So letters may be any width and height, and any characters
at all (accents, other alphabets) can be named. Letters are white where drawn, so the
look's word colours tint them; a space with no letter of its own is half the height wide,
and lines are the height plus one pixel apart. Every word the game shows uses it:
messages, menus, shops, battles, Screen Text. A picture that does not match its letters
falls back to the built-in font, with a message when the game opens and a warning in
*Check the project*. (`ui::PixelFont::from_strip()`; `ProjectSettings::font` and
`font_characters`.)

| Function | Does |
|---|---|
| `decode_utf8(text)` | The code points of UTF-8 text. A malformed byte becomes U+FFFD (shown as `?`), and decoding goes on with the next byte |
| `measure(font, text)` | The width of one line, in pixels, without the space after the last glyph |
| `wrap(font, text, width)` | Lines no wider than `width`: broken at spaces, keeping words whole; a word wider than a whole line is broken where it overflows (never inside a UTF-8 character). Line breaks in the text are kept |
| `draw_text(sprites, atlas, font, text, x, y, colour)` | Queues one line with its top-left at (x, y). Glyphs land on whole pixels when x and y are whole |

`SpriteRenderer::begin_over(target)` begins a batch that **keeps** what the target holds
(`SDL_GPU_LOADOP_LOAD`), where `begin()` clears it. Text and boxes are drawn this way, over
a frame that is already drawn, in either render style.

## 3. Dialogue

The **Dialogue** object (`dialogue`, category Story) gives logic:

| Block | Kind | Does |
|---|---|---|
| *"Say [text]"* | Action | Shows the text in a message box. Said while one is showing, it waits its turn: messages show in the order they were said |
| *"Ask [question] with answers [answers]"* | Action | Shows the question with answers to pick from, separated by `\|` (`Yes, please\|No, thanks`). Spaces around each are dropped, and so are empty ones; with none left it is an ordinary message |
| *"A message is open"* | Condition | Whether a message box is on screen |
| *"The answer chosen"* | Value | The last answer picked, counted from 1; 0 before any |
| *"The answer's text"* | Value | The words of the last answer picked |
| *"When an answer is picked"* | Event | Runs when the player picks an answer, with `answer` counted from 1 |
| *"When the message closes"* | Event | Runs when the player closes the **last** message waiting, after "answered" if it was a question |

In the game:

- **The player stands still** while a message is open. Movement and physics see no
  controls held, so the hero stops walking and pushes nothing.
- **Opening.** When a message shows after none, the box opens over a tenth of a second,
  growing from its middle line as it fades in, as RPG windows open; its words come once it
  is open (`plan/02` K7). Messages one after another do not open it again.
- **Which button goes on.** Once a page is all shown (and it is not a question's answers),
  the button that turns it blinks on the box's bottom edge, named for the device the player
  used last: *[Z]* on a keyboard, *[Pad A]* after a gamepad (`InputState::prompt()`,
  `plan/02` K4). Rebinding Interact on the Options page changes it too.
- **Interact turns the pages.** A message longer than the box shows a page at a time;
  Interact shows the next page, and on the last page closes the message. The press is
  used up: it does not run *"When Interact is pressed"* Rules and does not talk to the
  person in front of the player, so closing the elder's words does not open them again.
- **Questions.** On a question's last page, its answers show in a box above the message,
  at the right, with a `>` cursor on the chosen one (white; the others dimmer). Up and
  Down move the cursor, wrapping round at the ends, and do not walk or run *"When Up is
  pressed"* Rules. Interact picks the answer and closes the question. Before the last
  page, Up and Down do nothing and Interact turns the page.
- **Ask and wait.** *"Ask"*, then *"Wait until [not: a message is open]"*, then *"If [the
  answer chosen] = 1"* branches a conversation. The sample's elder does this.
- **Say and wait.** In a Rule or Sequence, *"Say [text]"* followed by *"Wait until [not: a
  message is open]"* pauses there until the player has read it, as `plan/05` §3.4's
  *"Show dialogue … and wait"* does. *Wait until* checks its condition at once and then
  once per step ([logic-runtime.md](logic-runtime.md) §7).
- **Loading a save** closes every message without running *"When the message closes"*,
  because the save does not hold them and the Sequences that said them stop
  ([game.md](game.md) §8).
- `Runtime::dialogue().said()` lists every message said, oldest first, and
  `sparxel-player` prints each as `said: …`, so tests and creators can follow a
  conversation without a screen.

**The message box** is drawn after the world, at the bottom of the screen:

| Part | Size |
|---|---|
| Margin to the screen's edges | 4 pixels |
| Border | 1 pixel, (230, 232, 240) |
| Fill | (18, 20, 40) at alpha 240, so the game shows through a little |
| Padding from the box's edge to the text | 5 pixels |
| Lines | `clamp((height / 3 − 10) / 9, 1, 3)`: 3 lines in a 320 × 180 game, 1 in a tiny one |

So a 320 × 180 game shows three lines of up to 302 pixels in a 312 × 36 box at the
bottom. The text is white.

## 4. Tests

| Test | Checks |
|---|---|
| Glyph widths | Advances of `H`, `i`, `l`, `.` and space; `measure("Hi")` is 9 and of nothing 0; characters outside the font measure as `?`; the descender row; the atlas's size |
| UTF-8 | Two- and four-byte characters; a character cut short and an invalid byte each replaced, and decoding continuing |
| Wrapping | Breaks at spaces with whole words kept; line breaks kept; empty text one empty line; a word too wide broken into pieces that fit and join back to it; every line of a long text fits |
| Exact pixels (GPU) | "Hi" drawn at (1, 1) in a colour: both sides and the bar of the H on their rows, the i's dot, gap, stem and foot where they belong, and the gaps inside and between the letters left dark |
| Talking | Interact at the elder runs its Rule, which says a line and waits until it is closed; the next Interact closes it without running the Interact Rule or talking again; then "closed" and the rest of the elder's Rule run |
| Standing still | While the message is open, the hero does not move or walk, though Left is held; it walks the step after the message closes |
| Pages and queueing | A long message takes exactly one Interact per wrapped line in a one-line box; a second message waits its turn; "closed" runs once, after the last |
| Loading | Loading a save closes an open message without running "closed" |
| The box (GPU) | Border pixels at the box's corners, the screen outside it untouched, the dark fill, and white text inside |
| Questions | Answers parsed from `\|` with spaces and empty ones dropped; picking out of range refused; Down, Down, Down (round to the first), Up (round to the last), Up, then Interact picks the second answer: "answered" with 2 and its text, then "closed"; the hero did not move |
| Long questions | Down on the first of two pages does nothing; Interact turns the page; then Down and Interact pick the second answer |
| The answers box (GPU) | In a 64 × 64 game: its border exactly 2 pixels above the message box and right-aligned with it; the cursor and first answer white, the second dimmer |
| The sample's elder | Playing the sample with recorded input: the welcome closed, the elder's greeting closed, "No, thanks" picked, and the elder's reply to it shown; every line said in order, no problems |
| The sample | `sparxel-player` prints the welcome as `said: …` in both render styles |


## Text effects and the typewriter

Messages (and conversations' lines) carry effects as tags in their words, as RPG Maker's
message codes do (`plan/02` G4; `ui::parse_rich()`):

| Tag | Effect |
|---|---|
| `[wave]…[/wave]` | The letters bob up and down, one after another |
| `[shake]…[/shake]` | They tremble, a pixel either way |
| `[color=gold]…[/color]` | A colour: *red*, *gold*, *green*, *blue*, *grey*, *white*, or `#rrggbb` |
| `[speed=2]…[/speed]` | Typed twice as fast (below 1, slower) |
| `[pause=0.5]` | Typing waits half a second there |
| `[[` | A `[` |

Anything else in brackets is shown as written. Tags nest. Paging and wrapping use the words
without their tags.

**The typewriter.** The Project view's *Text speed* (letters a second; `text_speed`, 0 by
default, which shows each message whole at once) types messages out. Interact while a page
is still typing shows the page whole; the next press turns the page or closes the message,
as before. A question's answers show once its words are all typed.


## Screen Text, Screen Bar and Screen Picture

Words and bars fixed to the screen rather than the map, made like any other part of a scene
(an entity with the component; `plan/02` K2): the gold carried, the hero's HP, a score, a
countdown. They are drawn over the world and the weather, under messages, and not over
menus or battles.

| Component | Settings |
|---|---|
| **Screen Text** (`screen_text`) | *Text* with values in braces (below), its *Place* on the screen (a corner, a side or the middle), *X* and *Y* pixels in from there, *Size* (1, or 2 for twice as large) and *Visible*. Words have a one-pixel shadow so they read over any ground, in the look's word colour, and are translated ([localization.md](localization.md)) |
| **Screen Bar** (`screen_bar`) | *Value* and *Most* (each a value in braces or a number), filled that far in its *Colour* (`#rrggbb`) inside a frame in the look's colours, its *Place*, *X*, *Y*, *Width*, *Height* and *Visible* |
| **Screen Picture** (`screen_picture`) | A *Picture* (an icon, a portrait, a panel), its *Place*, *X* and *Y*, *Width* and *Height* (0 keeps the picture's own), *Frame edge* and *Opacity* (0 to 1). Pictures are drawn under the bars and words, so a panel can sit behind them |

**Layouts.** A **Screen Layout** on an object lines up the Screen Text, Bars and Pictures
of the objects under it, in the order they are listed: **Down** (one under another: a
party's names and HP bars), **Across** (side by side: hearts, icons) or **Grid** (rows of
*Columns*, each cell as large as the largest, things centred in it), *Spacing* pixels
apart. The block is fixed to the screen by the layout's own *Place*, *X* and *Y*; in a
column, things line up on the side of its Place (on the right for a layout on the right),
and in a row likewise. The children's own Places are not used, and one hidden is left out
(the rest close up); a hidden layout hides them all. Put an object under another with a
right-click in the Entities list, *Put under ▸* (or *Put at the top*), one undo step. In
the scene view, clicking one of a layout's things selects the layout, and dragging moves
it. (`ScreenLayout`, `lay_out()` and `screen_place()` in `engine/game/hud.h`, shared by
the game and the editor.)

**Frames that stretch (9-slice).** With a *Frame edge* of, say, 2, a picture's outer two
pixels on each side are drawn as they are while the middle stretches to the *Width* and
*Height*: an 8-pixel frame makes a panel of any size whose corners and rim stay crisp,
as in RPG Maker's windows or Godot's NinePatchRect. An edge too wide for the picture or the
size is narrowed to fit; 0 stretches the whole picture. The RPG template puts the gold on
such a panel (`art/panel.png`, 8 × 8, stretched to 60 × 14). (`nine_slice()` in
`engine/game/hud.h` gives the pieces; the game and the editor's scene view both draw from
it, so they match.)

Values in braces, filled every frame:

| Value | What it is |
|---|---|
| `{gold}`, `{potion}` (or `{item:potion}`) | How many of it the player carries |
| `{hp:hero}`, `{max_hp:hero}`, `{mp:hero}`, `{max_mp:hero}`, `{level:hero}` | A party member's, by their row of `data/party`, with what they wear |
| `{var:score}` | A logic variable, by its name |
| `{time}` | The time of day, as 7:05 |
| `{timer:race}` | Whole seconds left on a timer ([game.md](game.md)) |

In the editor, the scene view shows them where the game will, with their words as
written (braces and all), bars half full and pictures stretched as the game will, so they
are placed by eye. **Click one** to select it (it is outlined, and the inspector shows it)
and **drag it** to move it: its *X* and *Y* change by whole pixels of the game's screen,
measured from its *Place*, so one fixed to the bottom right stays fixed there when the
screen's size changes. A drag is one undo step (*Move gold_panel on the screen*). X and Y
may be negative, to place something left of or above the middle.

`{{` writes a brace. Logic shows and hides them by their *Visible*, and changes their words
like any other property.

## 5. Next increments

1. **Vector fonts** through FreeType and HarfBuzz (ADR-010), with fallbacks per language
   (feature G7); pixel fonts from a picture are done (§2). The editor's scene view still
   shows Screen Text in the system's font.
2. **Layouts, further:** layouts inside layouts, and a list made from a table's rows (a
   party's members, the items carried) without one object per row (feature K1).
