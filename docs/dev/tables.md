# Data tables

> **Status: v0.1.** The game's data as tables, as RPG databases and spreadsheets keep it:
> typed columns, formula columns worked out from the row's other cells, CSV both ways, a
> grid editor with a form for the chosen row, and logic reading any cell.
>
> Code: [`engine/game/include/engine/game/tables.h`](../../engine/game/include/engine/game/tables.h)
> (the format, formulas, CSV and the "tables" object),
> [`editor/core/include/editor/core/table_edit.h`](../../editor/core/include/editor/core/table_edit.h)
> (editing with undo, on [`history.h`](../../editor/core/include/editor/core/history.h)) and
> [`editor/app/src/table_window.h`](../../editor/app/src/table_window.h). Tests:
> [`tests/game/runtime_test.cpp`](../../tests/game/runtime_test.cpp),
> [`tests/editor/table_test.cpp`](../../tests/editor/table_test.cpp) and
> [`tests/editor_app/table_test.cpp`](../../tests/editor_app/table_test.cpp).

## 1. What it is for

Items, enemies, skills, shops and levels are numbers and names that belong together
(`plan/02` T1–T5): a slime's attack, a potion's price, the experience each level needs.
Tables keep them in one place to balance at a glance, instead of scattered through logic,
and the RPG kit reads them.

## 2. Making one

**File ▸ New table…** makes `assets/data/<name>.dtable` (a *name* column and one row) and
opens it; double-clicking a `.dtable` in the Assets (or Ctrl+K) opens one.

| Part | What it does |
|---|---|
| **Grid** | A row per thing, its **Name** first (what the game finds it by), then a column per value, its heading saying what it holds. Typing into a cell changes it (one undo step per cell); renaming a row is typing its name. **Formula** columns are greyed and worked out. A cell that does not fit its column (*4.5* in a whole-number column, *maybe* in a yes/no one) is red, with the reason as its tip, and the status bar counts them |
| **Row** (right) | The chosen row as a form, field by field, as RPG Maker's database shows an entry |
| **Table** menu and toolbar | *Add a row* (Ctrl+Enter: after the chosen one, named *row_N*), *Delete the row*, *Move the row up* and *down*, *Add a column…*, *Change the column…* (or double-click its heading), *Delete the column*, Undo and Redo |
| **Columns** | A name (letters, digits and _; not *id*), what it **holds**: text, a whole number, a number, yes/no, or a **formula**, and the numbers it is kept within (*at least*, *at most*). Renaming a column carries its cells; a formula is checked on every row before it is taken |
| **File** | Save (Ctrl+S), *Import CSV…* (columns the table lacks are added, as numbers when every cell is one, else as text; rows are added or replaced by name), *Export CSV…* (formulas worked out; commas and quotes quoted) and Close |

**Formulas** use numbers, `+ - * / ^` (powers go right to left), brackets, `min(a, b)`,
`max(a, b)`, `floor(a)`, `round(a)`, the row's other columns by name, and `row` (the row's
place, from 1): *level * level * 10*, *(attack + defense) / 2*, *max(1, hp - 5)*. One may
read another formula column; two that read each other round in a ring are refused, as is
dividing by 0. Mistakes are said plainly: *The formula reads "speed", which is not a
column.*

Project Doctor ([editor.md](editor.md)) lists every table's cells that do not fit and
formulas that do not work out.

## 3. In the game

The **tables** object gives logic, by a table's path in `assets/` (`data/enemies`, with or
without `.dtable`):

| Block | Gives |
|---|---|
| *The {column} of {row} in {table}* | A cell as a number (yes is 1, no 0; formulas worked out); 0 if there is none |
| *The text {column} of {row} in {table}* | A cell as text, such as an item's name |
| *{table} has a row {row}* | Whether there is such a row |
| *The number of rows in {table}* | How many rows |

Each table is read on first use. Numbers are kept within their column's range.

## 4. The file

A `.dtable` file is JSON (`format` *sparxel.table*, `version` 1), keys sorted: `columns`
(`id`, `kind`: *text*, *whole*, *number*, *yes_no* or *formula*, `formula`, `min`, `max`)
and `rows` (`id` and each cell by column, as text; formula cells are not stored). At most
64 columns and 10,000 rows.

## 5. Next increments

1. **References** between tables (an enemy's drop is a row of *items*), with a list to pick
   from, and **curves** drawn for formula columns, to balance by eye.
2. **Bulk edit**: a value typed into many selected cells at once.
3. **Tables in the inspector**: a property chosen from a table's rows.
