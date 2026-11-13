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
| `data/party` | Party member | `name` (the row's), `max_hp` (10), `max_mp` (0), `attack` (5), `defense` (0), `speed` (5), `skills` (rows of `data/skills`, separated by commas; `fire@3` is learned at level 3), and for each of the five numbers a `<number>_growth` column (0): how much it rises each level (`attack_growth`) |
| `data/enemies` | Kind of enemy | the same, plus `xp` and `gold` given when it is beaten, `image`: a picture of it, by its path in `assets/`, drawn above its name, and `inflicts` with `chance`: a state its attacks may give (a bat whose bite puts to sleep) |
| `data/skills` | Skill | `name`, `mp_cost` (0), `power` (10), `heals` (no: it hurts an enemy; yes: it heals someone of the party), `all` (no: one target; yes: every enemy, or every ally), `inflicts` (a row of `data/states` it gives) with `chance` (100, in percent), `cures` (rows of `data/states` it ends, or *all*). A skill of power 0 that inflicts or cures does only that |
| `data/items` | Item usable in battle, or equipment | `name`, `heal`: HP it gives back. The party uses what the inventory holds. **Equipment** has a `slot`: *weapon*, *shield*, *head*, *body* or *accessory*, and adds its `max_hp`, `max_mp`, `attack`, `defense` and `speed` to whoever wears it; it is not offered as an item in battle. Items may also have `cures` (as for skills: an *Antidote* with `cures` *poison*), and equipment `resists` (rows of `data/states` its wearer cannot get) |
| `data/states` | Status effect | `name` (said as *Slime is poisoned!*: an adjective such as *Poisoned*, *Asleep*), `turns` (3: rounds it lasts), `hp_per_turn` (0: below 0 hurts at each round's end, above heals), `skip_turn` (no: yes, and they cannot act), `ends_on_hit` (no: yes, and damage ends it, as sleep), and `attack`, `defense`, `speed` added while it lasts |
| `data/levels` | Level (named `1`, `2`, … or in order) | `xp`: all the experience needed to reach it (a formula such as `row * row * 10` works). Its last row is the highest level. Without the table: 10 for level 2, 30 for 3, 60 for 4, 100 for 5 (5 × level × (level − 1)), up to 99 |

Logic then uses:

| Block | What it does |
|---|---|
| *{name} joins the party*, *{name} leaves the party* | Party members by their row of `data/party`, at most six. With nobody in the party, a battle takes the first row |
| *Heal the party* | Everyone to full HP and MP, as at an inn |
| *Start a battle against {enemies}* | Enemies by their rows, separated by commas: *slime, slime, bat* (two of a kind are told apart: *Slime A*, *Slime B*) |
| *Show {picture} behind battles* | A picture (a path in `assets/`) behind every battle from now on; empty for the world |
| *{name} is in the party*, *A battle is on* | Conditions |
| *The number in the party*, *The party's experience* | Values |
| *Give {name} {xp} experience* | Experience for one member, as a battle gives it |
| *The level of {name}*, *The experience of {name}*, *The experience {name} needs for the next level* | Values |
| *When {name} reaches a new level* | An event of the party object, with the member's row as {name} |
| *Equip {item} on {name}*, *Take off the {slot} of {name}* | Equipment from and back to the inventory; what was worn in that slot goes back to it |
| *The {slot} {name} wears* | A value: the item's row, or empty |
| *When the party wins*, *loses*, *gets away* | Events of the battle object |

**Random encounters.** An **Encounter Area** (`encounter_area`) on an entity covers
*width* × *height* tiles centred on the bottom of its position, like a Trigger Area. While
the player walks inside it, the tiles walked count; every *At least* to *At most* tiles
(12 to 24 by default, picked at random each time), a battle starts against one of its
*Enemy groups*, picked at random: `slime, slime | bat` (groups separated by `|`). Nothing
counts during a message, a battle or a menu, or when arriving in a scene. Turn *Enabled* off
for an area cleared of monsters. Its count and choices are saved with games
(`update_encounters()`). Its **Battle background** is a picture behind its battles.

**Enemies on the map.** An **Enemy on the Map** (`enemy_on_map`) is a monster the player
sees, as in many modern RPGs: give an entity a picture (and a Walker to wander or chase,
[game.md](game.md)) and this component, with its *Enemies* (`bat, bat`), the box the
player walks into (*width* × *height*, 1 × 1 by default, centred on the bottom of its
position) and an optional *Battle background*. Walking into it starts the battle (not
during a message, a menu, a shop or a fade, and once per walking in: standing in it, or
having fled, the player must step out and back in). Won, it is **beaten**: hidden, no more
battles, and Rules on its *When it is beaten* event run (drop something, open a door).
Turn *Beaten* off to bring it back. Saved with games (`update_enemies_on_map()`).

**Battle backgrounds.** A battle started by an Encounter Area or an Enemy on the Map with
a background, or after *Show {picture} behind battles*, shows that picture behind the
battle screen, covering the screen in proportion and dimmed a little; otherwise the world
shows, dimmed.

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

**Status effects.** A state given says so (*Slime is poisoned!*); given again, it lasts
its full turns again; a wearer of equipment that resists it is spared (*Hero is not
asleep.*). At each round's end, states hurt or heal (*Slime takes 2 damage
(poisoned).*), count down, and end (*Slime is no longer poisoned.*). Someone who cannot act
says so on their turn and is not asked to choose; when no one of the party can choose,
rounds go on by themselves. Damage ends states that end on a hit; falling ends them all.
The battle screen lists each fighter's states in purple, beside the party's HP and under
the enemies' bars. States end with the battle.

**Numbers.** Damage is the skill's power (0 for an attack) plus attack × 2, minus the
target's defence, at least 1; halved for a defender; then up to a tenth more or less, at
random. Healing is the skill's power, or the item's `heal`, up to the maximum. Fleeing
works half the time, more for faster members. The random numbers come from a seed set when
the battle starts, so a recorded game replays the same.

**When it ends**, after its last line is read: the party's HP and MP are kept for the next
battle (and saved with games), items used are taken from the inventory, a win gives the
enemies' experience to the party (each member still standing gets all of it) and their gold
to the inventory as *gold*, and Rules on *won*, *lost* or *fled* run.

**Levels.** Each member's level comes from their experience. Reaching one is said in the
message box (*Hero is now level 2!*), with each skill it teaches (*Hero learns Fire!*), and
Rules on *When {name} reaches a new level* run. In battle, a member's numbers are the
table's plus their growth for every level past the first; HP and MP lost stay lost (a new
level does not heal). The pause menu's Party page shows each member's level and the
experience to the next. Saved games keep each member's experience. Losing leaves the party down; Rules decide what happens (a
game over, or waking at an inn).

## 4. Next increments

1. **Enemies on the map that chase** when the player comes near, and return when left
   behind (Brains, `plan/02` H3).
2. **States that last** after battles (poison walking on the map), and cures at inns.
3. **Smarter enemies**: skills, and Brains choosing them.
4. **A battle-screen designer** in the UI workspace, and animations for attacks.
