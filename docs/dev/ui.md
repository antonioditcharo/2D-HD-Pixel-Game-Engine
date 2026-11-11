# UI: text and dialogue

> **Status: v0.1.** A built-in pixel font, so every game can show text without a font
> asset; measuring and word-wrapping; pixel-exact text drawing; and the **dialogue**
> object, whose *"Say [text]"* action shows a message box that the player reads and closes
> with Interact while the game waits. Tested with exact pixels.
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

Imported fonts (FreeType and HarfBuzz, ADR-010), choices, portraits and the UI editor come
later (§5).

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
| *"A message is open"* | Condition | Whether a message box is on screen |
| *"When the message closes"* | Event | Runs when the player closes the **last** message waiting |

In the game:

- **The player stands still** while a message is open. Movement and physics see no
  controls held, so the hero stops walking and pushes nothing.
- **Interact turns the pages.** A message longer than the box shows a page at a time;
  Interact shows the next page, and on the last page closes the message. The press is
  used up: it does not run *"When Interact is pressed"* Rules and does not talk to the
  person in front of the player, so closing the elder's words does not open them again.
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
| The sample | `sparxel-player` prints the welcome as `said: …` in both render styles |

## 5. Next increments

1. **Choices:** *"Ask [question] with [answers]"* and the answer chosen in a variable,
   which the dialogue graph (feature G1) builds on.
2. **Speakers and portraits:** a name above the box, a face beside it, and the text
   appearing letter by letter with a blip sound.
3. **Imported fonts:** pixel fonts from a PNG sheet, then vector fonts through FreeType
   and HarfBuzz (ADR-010), with fallbacks per language (feature G7).
4. **Box styles** in the project's Style Guide (feature A11): colours, a 9-slice frame,
   and the position on screen.
5. **UI widgets** for menus: a title screen, pause, options and the save and load screens
   (feature K3).
