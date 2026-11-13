# Menus

> **Status: v0.1.** A ready-made title screen and pause menu, as RPG Maker games come
> with them, turned on in the Project view. Tested driven by the keys, and drawn.
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

**The pause menu** opens on the Menu key (Escape, or Start on a gamepad) while nothing else
is open (not during a message or a battle): **Resume**, **Items** (what the inventory holds),
**Quests** (the journal: [game.md](game.md)), **Party** (members and HP, and the experience
won: [battle.md](battle.md)), **Save** and **Load** (slots 1 to 3; saving says *Saved in slot
2.*) and **Quit**. Up and Down choose, Interact picks, Cancel (or Menu) goes back, and from
the menu itself resumes. While a menu is open, nobody moves and Rules hear no keys.

## 3. Next increments

1. **The UI designer** (`plan/02` K1): these screens as templates to restyle and rearrange,
   with a title picture, music and more slots.
2. **Options**: volume, text speed and key bindings for the player (U2).
3. **Equipment and skills pages** in the pause menu, with the RPG kit's growth.
