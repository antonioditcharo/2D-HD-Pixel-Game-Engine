# Sprite Studio

> **Status: v0.1.** A window of the editor for drawing and animating pixel-art sprites,
> laid out as pixel artists know it from Aseprite and its peers: tools down the left, the
> canvas in the middle, colours, the palette and the layers on the right, and the timeline
> of frames along the bottom. Layers, frames with their own times, tags (the animations),
> onion skin and playback; the pencil with pixel-perfect strokes, the eraser, lines,
> rectangles, ellipses, fill, selection and move, mirroring, and undo for all of it. It
> saves a `.dsprite` file and, beside it, the sprite sheet PNG the game draws and a `.sheet`
> with its frame times and tags, which the game plays by name.
>
> Code: [`editor/core/include/editor/core/sprite.h`](../../editor/core/include/editor/core/sprite.h)
> and [`src/sprite.cpp`](../../editor/core/src/sprite.cpp) (the model, without Qt), and
> [`editor/app/src/sprite_studio.h`](../../editor/app/src/sprite_studio.h) and
> [`.cpp`](../../editor/app/src/sprite_studio.cpp) (the window). Tests:
> [`tests/editor/sprite_test.cpp`](../../tests/editor/sprite_test.cpp) and
> [`tests/editor_app/sprite_studio_test.cpp`](../../tests/editor_app/sprite_studio_test.cpp).

## 1. What it is for

The engine's promise is pixel games made in one place (`plan/02` B1–B5, `plan/06` §3.3).
Sprite Studio is where a creator draws a character, a tile or an item and animates it,
then sees it in the scene the moment it is saved. Artists who prefer another program keep
using it: a PNG changed elsewhere shows in the editor as soon as the editor is in front
again ([§5](#5-in-the-editor)).

## 2. Using it

**Opening.** Double-click a picture (`.png`) or a sprite (`.dsprite`) in the editor's
Assets, or use **File ▸ New sprite…** (Ctrl+Shift+N: a name and a size, a tile by default),
which makes `assets/art/<name>.dsprite` and its sheet `art/<name>.png`. A PNG that has a
`.dsprite` of the same name opens that. One without is cut into frames, left to right then
top to bottom: the editor offers the frame size the scene's sprites use with the picture
(or the whole picture), and saving makes its `.dsprite`. Each sprite has one window;
opening it again brings that window forward.

| Part | What it does |
|---|---|
| **Tools** (left) | One at a time, each with the key pixel artists expect: **Pencil** (B), **Eraser** (E), **Line** (L), **Rectangle** (U) and **Filled rectangle** (Shift+U), **Ellipse** (O) and **Filled ellipse** (Shift+O), **Fill** (G), **Select** (M), **Move** (V) and **Pick colour** (I). Lines, rectangles and ellipses follow the cursor while dragged and are drawn where the button comes up |
| **Brush** (top) | The brush's **size** (a square, 1 to 32 pixels); **Pixel-perfect**: one-pixel pencil strokes leave out the corner pixel of every L shape, so a curve drawn by hand is one pixel thick; **Fill all of a colour**: Fill paints every pixel of the clicked colour, joined or not; **Mirror across** and **Mirror down**: everything drawn is drawn again mirrored about the middle of the canvas; **Dither**: only the pixels of a checkerboard are drawn (with any tool), for gradients and textures; **Shade**: each pixel a tool passes over moves one step along the palette towards the colour drawn with (draw with a light colour to lighten along its ramp, a dark one to darken), once a stroke, as Aseprite's shading ink; see-through pixels and colours not in the palette stay |
| **Canvas** (middle) | The current frame, magnified to whole pixels over a checkerboard (see-through pixels show it), with a pixel grid from 8 times up. The **left button** draws with the first colour and the **right** with the second; **Alt-click** picks the colour under the cursor with any tool. The **wheel** zooms about the cursor; the **middle button**, or Space and a drag, moves the view; **Ctrl+0** fits the window. The pixel under the cursor is in the status bar |
| **Colours** (right) | The first and second colours (click either to choose any colour, with its opacity), **Swap** (X); the **palette**: click a colour for the first, right-click for the second, hold the button down to change or remove it; **Add the first colour**, and **From the sprite** (the palette becomes the colours the sprite uses) |
| **Layers** (right) | The layers, the top first as they are drawn, each with **Shown** and **Locked** tick boxes and its name (double-click to rename); the current layer's **Opacity**; **New**, **Duplicate**, **Delete**, **Up**, **Down** and **Merge down**. Hidden and locked layers refuse drawing, saying why |
| **Timeline** (bottom) | A grid of the layers (down) by the frames (across): ● where something is drawn, ○ where nothing is; clicking a cell chooses that layer and frame. **◀** and **▶** (`,` and `.`) step through the frames; **Play** (Enter) plays them at their own times, looping in the tag of the current frame (forward, backward or ping-pong), or through all of them; **New frame** (Alt+N), **Duplicate** (Alt+D), **Delete**, **Earlier** and **Later**; **Onion skin** shows the frames either side faintly behind; **Shows for** sets the frame's time in milliseconds, or every frame's |
| **Tags** (bottom right) | Named runs of frames, the sprite's animations (*walk*, *idle*): **Tag frames…** names a run and how it plays; double-click one to change it. A tag's name is written over its first frame in the timeline. Frames added inside a tag make it longer, and deleting a tag's only frame deletes it |
| **Menus** | **File:** Save (Ctrl+S), Close. **Edit:** Undo and Redo (saying what they change: *Undo Draw with the pencil*), Cut, Copy, Paste (onto the layer at the selection's corner, then selected, to move), Clear (Delete), Select all (Ctrl+A), Deselect (Ctrl+D), Flip horizontally (Shift+H) and vertically (Shift+V), Swap the colours. **Sprite:** Canvas size… (keeping the pixels at the top left or in the middle), Frames across in the sheet…. **View:** zoom, fit, the pixel grid, onion skin, and the panels |

**Selections** keep drawing inside them, as in other pixel editors: with a rectangle
selected, every tool draws only there. **Move** drags the selected pixels (or, with nothing
selected, the whole layer), and the selection goes with them.

The title shows the file, a star while there are unsaved changes, the size and the number
of frames. Closing with unsaved changes asks whether to save them, as does closing the
editor or opening another project.

## 3. The files

| File | What |
|---|---|
| `<name>.dsprite` | The sprite as made: its size, layers (name, shown, locked, opacity), frames (each one's time), tags (name, first and last frame, direction), palette, and each layer's pixels in each frame as a PNG in base64. JSON with sorted keys (`format` *sparxel.sprite*, `version` 1), so it diffs well and the same sprite always saves the same text. Empty cels are not written |
| `<name>.sheet` | What the game reads of the sprite besides its pixels: the frame size, each frame's time and the tags ([scene2d.md](scene2d.md) §3). An Animation whose *Tag* is *walk* plays the *walk* tag, each frame for its own time, in its direction; the inspector lists the sheet's tags to pick from (a name not drawn yet may still be typed) |
| `<name>.png` | The sprite sheet: every frame flattened (shown layers, bottom first, blended by opacity), left to right then top to bottom, as a Sprite's `frame_width` and `frame_height` read it ([scene2d.md](scene2d.md) §3). Frames go across as far as 4,096 pixels, then wrap; a sprite opened from a sheet keeps that sheet's frames across, so the frame numbers a scene uses stay the same. *Frames across in the sheet…* changes it |

Both are written to a temporary file first and then renamed over the old one, so a failed
save never leaves half a file. The game never reads the `.dsprite`, and exported games
leave it out ([export.md](export.md)).

**Limits** a file cannot exceed: 1,024 pixels each way, 64 layers, 1,024 frames, 256
palette colours, frame times of 1 to 65,535 milliseconds. Files that break them, or whose
pixels cannot be read, are refused in plain language (*"The pixels of layer 2, frame 3
cannot be read."*); tags outside the frames are dropped.

## 4. How it works

- **`sprite::Sprite`** is a value: the size, layers, frames, tags, palette and
  `cels[layer][frame]`. A cel is a `shared_ptr` to its pixels, null where nothing is drawn.
- **`sprite::Editor`** holds a sprite being edited, with the current layer, frame and
  selection. **Undo by snapshots**, as the editor's Document does ([editor.md](editor.md)
  §3): every edit records the whole state before and after, which is correct for every kind
  of edit. Cels are shared between snapshots until one is drawn on (a copy is made then),
  so a snapshot of a large sprite costs a few pointers per cel. 256 steps are kept.
  Consecutive changes to one value (an opacity slider dragged, a frame time typed, a
  colour) merge into one step until `end_merge()`, as typing does in the inspector.
- **Drags** (`begin()`, `drag()`, `end()`): the cel is redrawn from the pixels it had when
  the drag began, every time the cursor moves. A shape is therefore the shape to where the
  cursor is now, and a pencil stroke is the stroke so far, filtered for pixel-perfect
  corners as a whole. Points a fast drag skips are joined with Bresenham lines; ellipses use
  Alois Zingl's integer algorithm, which is exact for every rectangle, flat ones included.
  A drag that changes nothing is no undo step.
- **Blending** is straight-alpha "over", with the layer's opacity scaling the pixel's.
- **Where the game sees it:** Save writes both files, then the main window refreshes the
  asset database; the GUID of the PNG stays the same (its `.meta` is kept), so every Sprite
  and Tilemap that uses it draws the new pixels.

## 5. In the editor

- **Hot reload.** `MainWindow::reload_assets()` refreshes the asset database and hands the
  changes to the scene view's preview, which forgets the textures of the pictures that
  changed (`scene2d::TextureCache::apply()`) and draws the scene again. It runs after
  Sprite Studio saves and whenever the editor window comes back to the front, so a PNG
  saved in another program shows too. The inspector's picture lists and the tile palette
  follow.
- The scene's own undo is not involved: drawing a sprite changes a file, not the scene.

## 6. Tests

| Test | Checks |
|---|---|
| Blank sprite | One layer named *Layer 1*, one frame of 100 ms, the 32-colour palette starting with black; sizes kept within 1 to 1,024 |
| Colours | Hex in and out, `#rgb`, `#rrggbb` and `#rrggbbaa`; bad text refused |
| Pencil and eraser | A stroke is one undo step labelled *Draw with the pencil*; undo, redo, saved and modified; erasing; a stroke that changes nothing is no step |
| Pixel-perfect | A staircase drawn through its corners keeps 3 pixels, not 5; joined fast drags |
| Shapes | A line follows the cursor; a 4 × 3 rectangle's edge is 10 pixels and filled 12; an ellipse touches the middle of each side of its rectangle and not the corners, and filled covers its middle |
| Fill | Joined pixels only, stopped by a wall, or every pixel of the colour; filling with the same colour changes nothing |
| Mirroring and brush size | Four pixels from one dot mirrored both ways; a 3-pixel brush paints 9 |
| Hidden and locked | Drawing refused with the reason; selecting still works |
| Selection | Drawing clipped to it; Move takes its pixels and the selection along, undone exactly; Move without a selection takes the layer; clear, copy and paste |
| Flip | Left to right, then top to bottom |
| Layers | Added and named, opacity merged into one step, blended 50% over another, renamed, ordered, merged down, duplicated, deleted down to the last |
| Frames and tags | Copied frames share pixels until drawn on; tags grow, move and go with frames; unique tag names; times merged while typed, and for every frame; frames moved |
| Palette | Added once, changed (merged), made from the sprite's colours |
| Canvas | Resized keeping the pixels at the corner or in the middle; sizes out of range refused |
| Sheet | Frames left to right then down; frames across; read back as the same frames with empty trailing cells left out; a picture that does not divide is refused, saying so |
| Files | The `.sheet` beside it holds the frame size, times and tags; saved and read back the same (layers, frames, tags, palette, pixels, and byte-identical text); the sheet beside it; files of another format, of a size out of range, or missing are refused |
| The window | *New sprite* makes both files and opens a window; names leaving the art folder or taken are refused; drawing with the mouse (left and right buttons), Alt-click picking, Fill and undo; the timeline's rows and columns, choosing a cell, hiding a layer by its tick box; saving; opening again brings the same window |
| In the scene | The hero's picture opens cut into its scene's frames; painted over and saved, the scene view draws it differently; a sound is refused |


## Aseprite files

Artists who draw in Aseprite keep doing so (`plan/02` B12). Put a `.aseprite` (or `.ase`)
file anywhere in `assets/`: the editor imports it as a sprite sheet beside it, as Sprite
Studio saves one, so it is used like any other: `art/hero.aseprite` makes `art/hero.png`
(every frame side by side) and `art/hero.sheet` (each frame's duration, and Aseprite's
tags as the animations, forward, reverse or ping-pong). Visible layers are drawn bottom to
top with their opacity and each cel's; hidden layers, groups and tilemap layers are left
out; blend modes other than normal are drawn as normal. RGBA, grayscale and indexed
(palette) files are read, compressed or not, with linked cels
(`assets::read_aseprite()`, following Aseprite's published file format, with no need for
Aseprite itself).

It is imported again whenever the file changes: save in Aseprite, come back to the editor,
and the scene shows the new frames (**live reload**). Double-click an Aseprite file in the
Assets, or right-click it ▸ *Import again from Aseprite*, to import it at once. A damaged
file is refused in plain language in the status bar, and the old sheet stays.

## 7. Next increments

2. **More pixel-art helpers** (`plan/02` B2): dither patterns besides the checkerboard,
   brushes, tile and wrap mode, an outline generator, and lasso and magic-wand selection.
3. **Reference layers and layer groups** (B3), palette-swap variants (B4), frame events,
   hitboxes and pivots per frame (B6, B7).
4. **Aseprite slices** as 9-slices and pivots, and its user data as frame events.
5. **Lighting maps** (B9): a normal-map generator and brush, previewed under the scene's
   lights.
