# Battles

> **Status: v0.1.** Turn-based battles, as classic RPGs fight them, with every number from
> the data tables: a party and enemies, commands chosen each round (Attack, Skill, Defend,
> Item, Flee), everyone acting in order of speed, rewards, and a battle screen played with
> the same keys as the rest of the game. Tested as rules, in a running game driven by the
> keys, and drawn.
>
> Code: [`engine/game/include/engine/game/battle.h`](../../engine/game/include/engine/game/battle.h)
> (the rules) and [`battle_screen.h`](../../engine/game/include/engine/game/battle_screen.h)
> (the party and battle objects, and the screen). Tests:
> [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp).

## 1. What it is for

The RPG kit's heart (`plan/02` F2): fights a creator sets up with tables, not code.

## 2. Setting one up

Four tables in `assets/data/` ([tables.md](tables.md)) hold the numbers. Every column is
optional; what is missing takes the value in brackets.

| Table | A row per | Columns |
|---|---|---|
| `data/party` | Party member | `name` (the row's), `max_hp` (10), `max_mp` (0), `attack` (5), `defense` (0), `speed` (5), `skills` (rows of `data/skills`, separated by commas) |
| `data/enemies` | Kind of enemy | the same, plus `xp` and `gold` given when it is beaten, and `image`: a picture of it, by its path in `assets/`, drawn above its name |
| `data/skills` | Skill | `name`, `mp_cost` (0), `power` (10), `heals` (no: it hurts an enemy; yes: it heals someone of the party), `all` (no: one target; yes: every enemy, or every ally) |
| `data/items` | Item usable in battle | `name`, `heal`: HP it gives back. The party uses what the inventory holds |

Logic then uses:

| Block | What it does |
|---|---|
| *{name} joins the party*, *{name} leaves the party* | Party members by their row of `data/party`, at most six. With nobody in the party, a battle takes the first row |
| *Heal the party* | Everyone to full HP and MP, as at an inn |
| *Start a battle against {enemies}* | Enemies by their rows, separated by commas: *slime, slime, bat* (two of a kind are told apart: *Slime A*, *Slime B*) |
| *{name} is in the party*, *A battle is on* | Conditions |
| *The number in the party*, *The party's experience* | Values |
| *When the party wins*, *loses*, *gets away* | Events of the battle object |

## 3. Fighting

The world dims and stops behind the battle screen: the enemies across the top, each with
its picture and an HP bar; the party's HP and MP bottom left; the command menu bottom
right. Up and Down choose, Interact picks, Cancel goes back (from a list to the menu, or
from the menu to the member before, taking back their choice).

1. Each party member still standing picks, in turn (the chooser is in gold, with a `>`):
   **Attack** (then an enemy), **Skill** (those with enough MP, then a target unless it
   hits all), **Defend**, **Item** (the inventory's rows of `data/items`, then someone of the
   party) or **Flee**.
2. The enemies each pick someone of the party to attack.
3. Everyone acts, fastest first (on a tie, the party first). A target beaten before its
   turn comes is replaced by the next of its side still standing.
4. What happens is told line by line in the menu's place (*Slime A takes 7 damage.*), read
   with Interact.

**Numbers.** Damage is the skill's power (0 for an attack) plus attack × 2, minus the
target's defence, at least 1; halved for a defender; then up to a tenth more or less, at
random. Healing is the skill's power, or the item's `heal`, up to the maximum. Fleeing
works half the time, more for faster members. The random numbers come from a seed set when
the battle starts, so a recorded game replays the same.

**When it ends**, after its last line is read: the party's HP and MP are kept for the next
battle (and saved with games), items used are taken from the inventory, a win gives the
enemies' experience to the party and their gold to the inventory as *gold*, and Rules on
*won*, *lost* or *fled* run. Losing leaves the party down; Rules decide what happens (a
game over, or waking at an inn).

## 4. Next increments

1. **Encounters**: random battles in areas of the map, visible enemies that start one on
   touch, and battle backgrounds.
2. **Levels and growth**: experience raising stats through formula columns, equipment, and
   status effects (poison, sleep).
3. **Smarter enemies**: skills, and Brains choosing them.
4. **A battle-screen designer** in the UI workspace, and animations for attacks.
