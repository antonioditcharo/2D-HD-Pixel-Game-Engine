# Shops

> **Status: v0.1.** A ready-made shop screen, as RPG Maker's *Shop Processing* opens one:
> logic opens a shop selling rows of `data/items`, and the player buys and sells for gold
> with the keys. Tested on its own, in a running game driven by the keys, and drawn.
>
> Code: [`engine/game/include/engine/game/shop.h`](../../engine/game/include/engine/game/shop.h)
> and [`src/shop.cpp`](../../engine/game/src/shop.cpp); the runtime opens it. Tests:
> [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp).

## 1. What it is for

Every RPG has somewhere to spend the gold its battles give (`plan/02` F1, K3). A shop
comes ready, priced from the same table the battle's items live in, so a creator only
writes the prices and says what each shop sells.

## 2. Setting one up

Give `data/items` ([tables.md](tables.md)) the columns a shop reads; every one is optional:

| Column | What it does |
|---|---|
| `price` | Gold to buy one. Items without a price above 0 are not sold |
| `sell` | Gold paid for one sold back; without one, half the price (rounded down); 0: the shop does not buy it |
| `name` | As the shop shows it (*Potion*); without one, the row's name (*potion*) |
| `description` | A line shown under the list while the item is chosen (*Heals 20 HP.*) |

Logic then uses the inventory's blocks:

| Block | What it does |
|---|---|
| *Open a shop selling {items}* | The shop screen, selling those rows of `data/items`, separated by commas: *potion, ether, antidote*. Rows that are missing or have no price are left out and named in the game's problems |
| *The player is in a shop* | A condition |
| *When the player leaves a shop* | An event of the inventory |

Gold is what the inventory carries as *gold*, as battles give it ([battle.md](battle.md)).

## 3. Shopping

The world dims and stops behind the shop. Along the top: **Buy**, **Sell** and **Leave**,
and the gold carried. Below, the list: each ware's name, how many are carried, and its
price; those the player cannot afford (or has no room for) are grey. Under it, the chosen
ware's description, or what just happened (*Bought 3 Potion.*).

1. Up and Down choose; Interact picks; Cancel (or Menu) goes back, and from the top
   leaves.
2. **Buy** lists the shop's wares; **Sell** lists what the player carries that the shop
   buys (not gold).
3. Picking one asks how many: Left and Right change it by 1, Up and Down by 10, going round
   from the most to 1 and back. The most is what the gold pays for (at most 99 at once, and
   never more than the inventory holds), or, selling, all that is carried. The total shows
   beside it. Interact buys or sells; Cancel goes back to the list.

While the shop is open nobody moves, no encounters count, the pause menu does not open,
and Rules hear no keys. Loading a game closes it.

## 4. Next increments

1. **Equipment compared** in shops: whether each piece is better for each party member.
2. **Shop stock**: items a shop has only so many of, and prices that change (a sale, a
   better price for a friend).
3. **The UI designer** (`plan/02` K1): the shop screen as a template to restyle.
