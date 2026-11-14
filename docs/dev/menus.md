# Menus

> **Status: v0.1.** A ready-made title screen and pause menu, as RPG Maker games come
> with them, turned on in the Project view, with an Options page for the player's volumes,
> text speed and keys. Tested driven by the keys, and drawn.
>
> Code: [`engine/game/include/engine/game/menus.h`](../../engine/game/include/engine/game/menus.h)
> and [`src/menus.cpp`](../../engine/game/src/menus.cpp); the runtime opens them.
> Tests: [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp).

## 1. What it is for

Every game needs a way in, a way to pause, and a place to save (`plan/02` F1, K3). These
come ready, so a first game has them before anyone designs their own.

## 2. Using them

The Project view's **Menus** group has two tick boxes (`title_screen`, `pause_menu` in the
project file), both off by default, so Rules keep the Menu key until a creator chooses.

**The title screen.** Before the game starts, the world shows dimmed behind the game's
name, twice the size, and **New game**, **Continue** and **Quit**. *When the game starts*
Rules run only on New game. Continue lists save slots 1 to 3 (*(empty)* where nothing is
saved; an empty one says so); choosing one loads it; Cancel goes back to the title.

**Save points.** Logic's *Open the save menu* (on the game object) opens the save slots
alone, without the pause menu beside them, as at a save point: a crystal whose Rule is
*When the player interacts: Open the save menu*. It works whether or not the project has
a pause menu; Cancel goes straight back to the game.

**Credits.** A project with `assets/credits.txt` has **Credits** on the title screen,
before Quit: its lines roll up the screen, centred, the first in the highlight colour,
round again until Interact or Cancel goes back to the title.

**The pause menu** opens on the Menu key (Escape, or Start on a gamepad) while nothing else
is open (not during a message or a battle): **Resume**, **Items** and **Skills** (below),
**Quests** (the journal: [game.md](game.md)), **Party** (each member's level, HP and MP now,
attack, defence and speed with what they wear, and the experience to the next level:
[battle.md](battle.md)), **Equip** (below), **Options** (below), **Save** and **Load** (slots 1 to 3; saving says *Saved in slot
2.*) and **Quit**. Up and Down choose, Interact picks, Cancel (or Menu) goes back, and from
the menu itself resumes. While a menu is open, nobody moves and Rules hear no keys.

**Items** lists what the player carries that heals (rows of `data/items` with a `heal`
above 0, not equipment), with how many: picking one asks *On whom?*, and each Interact on
a party member uses one (*Hero recovers 20 HP.*), until none are left. **Skills** asks
*Who uses a skill?*, then lists that member's healing skills at their level, with their
MP cost: picking one asks *On whom?*, and each use spends the caster's MP, until too
little is left (*Mira has too little MP.*). Members who are down can only be helped at an
inn (*Heal the party*). Cancel goes back a step. HP and MP changed here are those the next
battle starts with ([battle.md](battle.md)).

**Equip** dresses the party, as RPG Maker's Equipment screen does: choose a member (with
their level), then a slot (*Weapon*, *Shield*, *Head*, *Body*, *Accessory*, each with what
is worn there), then *(nothing)* or one of the pieces the player carries for that slot.
Below the list, the member's Max HP, Max MP, Attack, Defense and Speed; while choosing a
piece, each shows what it would become (*Attack 7 > 10*), in gold where it rises and grey
where it falls. Wearing a piece takes it from the inventory and puts back what it
replaces. Cancel goes back a step. Pieces are rows of `data/items` with a `slot`
([battle.md](battle.md)), so shops sell them like anything else ([shop.md](shop.md)).

**Options** are the player's, not the creator's:

| Line | What it does |
|---|---|
| **Music**, **Sounds** | Left and Right change it by 10%, from 0% to 100%. It scales the game's own volume for that bus, so Rules changing the music volume keep the player's choice |
| **Text speed** | Left and Right go through *The game's own* (the project's `text_speed`), *Instant*, *Slow* (20 letters a second), *Normal* (40) and *Fast* (80) |
| **Language** | Left and Right go through *The game's own* and each language of `data/translations` ([localization.md](localization.md)) |
| **Move up** … **Menu** | Each action's keys. Interact on one waits for a key (*press a key...*); the first key pressed becomes that action's only key (gamepad buttons stay as they were). An action that had that key loses it, and one left with no key takes the old keys of the action changed: the two swap |

They are kept at once in `options.json` beside the save slots (volumes, text speed and
each action's key codes), and read back when the game starts; a broken or missing file
keeps the defaults.

The title screen and the pause menu fade in over an eighth of a second when they open
(`plan/02` K7); moving between pages does not fade.

### The look of menus and messages

The Project view's **Menus and messages** group chooses the colours every ready-made box
is drawn with: the message box and its answers, the speakers' name plates, the title
screen and pause menu, shops ([shop.md](shop.md)) and the battle screen
([battle.md](battle.md)), so games do not all look alike (`plan/02` K1, themes). **Look**
picks a ready-made one: *Midnight* (the standard navy and gold), *Parchment* (cream with
brown words), *Forest*, *Crimson* or *Clean* (white boxes, blue choices). **Colours** has a
swatch for each part, to change one at a time with a colour picker (with see-through):
*Box*, *Border*, *Words*, *Hints* (and what cannot be chosen), *Chosen* (the cursor's line,
titles) and *Name plate*. Each change is one undo step.

They are kept in the project file as `ui` (`box`, `border`, `text`, `dim`, `highlight`,
`plate`, each `#rrggbb` or `#rrggbbaa`; written only when they differ from Midnight).
Words in a message keep any colour given with `[color=…]` ([ui.md](ui.md)); the rest take
the look's *Words*.

**Box frame** draws every one of those boxes (and achievement notices) with a picture of
the project instead of the *Box* and *Border* colours: a window skin, as RPG Maker and
GB Studio have. Its **Frame edge** (pixels, 3 by default) is kept as drawn at each side
while the middle stretches (9-slice, the same as Screen Pictures: [ui.md](ui.md)), so an
8-pixel frame makes boxes of any size with crisp corners. Opening boxes fade it in as they
do the colours. The RPG template frames its boxes with the gold's panel (`art/panel.png`,
edge 2). A missing picture leaves the colours. It is kept in the project file as
`box_frame` and `box_frame_edge` (written only when there is one). (`draw_box()` and
`BoxFrame` in `engine/game/ui_theme.h`.)

## 3. Next increments

1. **The UI designer** (`plan/02` K1): these screens as templates to restyle and rearrange,
   with a title picture, music and more slots.
2. **More options**: window size and full screen, gamepad buttons, and a reset to the
   defaults (U2).
3. **A Status page**: each member's numbers, equipment, skills and experience at a glance.
