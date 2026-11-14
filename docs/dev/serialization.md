# Serialization

> **Status: v0.2.** Reflected objects, logic assets and scenes are written to and read from
> JSON, as the project format requires (ADR-011, `plan/03` §4). Every behaviour below is
> covered by tests. §7 lists what comes next.
>
> Code: [`engine/serialize/include/engine/serialize/json.h`](../../engine/serialize/include/engine/serialize/json.h),
> [`src/json.cpp`](../../engine/serialize/src/json.cpp) and
> [`src/scene.cpp`](../../engine/serialize/src/scene.cpp). Tests:
> [`json_test.cpp`](../../tests/serialize/json_test.cpp) and
> [`scene_test.cpp`](../../tests/serialize/scene_test.cpp) in `tests/serialize/`.

## 1. What it is for

Everything a creator makes is saved as JSON text, so projects diff and merge cleanly in
version control and users own their data (ADR-011, pillar 6). This module turns the
engine's in-memory data into that text and back:

- **reflected objects**, the properties of anything registered with the reflection
  registry ([reflection.md](reflection.md)); scenes, prefabs and save games are built from
  these;
- **logic assets**, the Logic IR of Rules and Sequences ([logic-runtime.md](logic-runtime.md));
- **scenes**, the entities of a map with their components ([world.md](world.md)).

It uses nlohmann/json (MIT) [S105].

## 2. The rules from the plan, and how they are met

| Rule (`plan/03` §4) | How |
|---|---|
| **Deterministic:** saving the same content twice gives byte-identical files | Object keys are always sorted, lists keep their order, and numbers use the shortest text that reads back exactly. `to_text()` always uses two-space indentation and ends with a newline. Tested by saving, loading and saving again |
| **Versioned:** every file carries a schema version, and older versions are migrated on load | Logic files start with `"format": "sparxel.logic"` and `"version"`. Loading runs the migration from each older version in order. A file from a newer version is refused with a message, never read wrongly |
| **Never silently drop data** | Every key that cannot be used is reported as an `Issue`, with its place in the file |
| **Stable references** | Choices are saved as their entry id (`"north"`), never as numbers, so reordering an enum in C++ cannot change what a file means. Entities are identified by GUID (§5) |

## 3. Reflected objects

```cpp
Json json = serialize::object_to_json(ObjectRef::of(lamp), *registry.find_type<Lamp>());
std::string text = serialize::to_text(json);

std::vector<serialize::Issue> issues;
std::optional<Json> parsed = serialize::parse(text, issues);
issues = serialize::object_from_json(*parsed, ObjectRef::of(lamp), *lamp_type, "/objects/0");
```

```json
{
  "brightness": 50,
  "facing": "west",
  "label": "Lamp",
  "lit": true,
  "radius": 4.5,
  "small": 0
}
```

- **Read-only properties are not written:** they are computed or set by the game, not
  authored. A read-only key found when loading is reported and skipped.
- **Loading checks every value** as the inspector would (kind, range, choice entries, what
  the C++ member can hold) and leaves a property unchanged when its value does not fit.
  Properties missing from the file keep their current value, so adding a property to a
  type never breaks old files.
- **Whole numbers** may be written as `40` or `40.0`, but not `40.5`. Numbers accept either.

Messages from the tests:

| Path | Message |
|---|---|
| `/objects/0/brightness` | 'Brightness' must be between 0 and 100, but is 101. It was not loaded. |
| `/objects/0/facing` | 'Facing' must be one of: north, east, south, west, but is 'up'. It was not loaded. |
| `/objects/0/small` | 'Small' cannot hold 300: it is too large or too small. It was not loaded. |
| `/objects/0/colour~1name~0x` | 'Lamp' has no property called 'colour/name~x'. It was ignored. |

Paths are JSON Pointers (RFC 6901): `/` and `~` inside a key are written as `~1` and `~0`.

## 4. Logic assets

```json
{
  "format": "sparxel.logic",
  "name": "chest",
  "rules": [
    {
      "block": 1,
      "do": [
        { "block": 2, "kind": "do", "member": "unlock", "object": "chest" }
      ],
      "event": "opened",
      "if": [
        {
          "kind": "call", "member": "has_item", "object": "player",
          "operands": [ { "kind": "literal", "value": "key" } ]
        }
      ],
      "object": "chest"
    }
  ],
  "version": 1
}
```

(Shown compacted; files have one key per line.)

| Item | Keys |
|---|---|
| Document | `format`, `version`, `name`, `variables`, `sequences`, `rules`, `brains` |
| Variable | `id`, `kind` (`yes_no`, `whole_number`, `number`, `text`, `choice`), `choice_list`, `initial` |
| Sequence | `block`, `name`, `steps` |
| Rule | `block`, `object`, `event`, `if`, `do`, `otherwise` |
| Brain | `block`, `name`, `initial`, `states`, `from_any` (transitions) |
| Brain state | `block`, `name`, `enter`, `while`, `exit` (steps), `go` (transitions) |
| Transition | `block`, `to`, `if` (conditions), `after` (seconds in the state) |
| Step | `kind` (`do`, `set`, `set_variable`, `wait`, `wait_until`, `if`, `repeat`, `run`, `log`, `for_each_tagged` with its tag as `member`), `block`, `object`, `member`, `values`, `body`, `otherwise` |
| Value | `kind` (`literal`, `property`, `call`, `variable`, `event_value`, `not`, `all`, `any`, `compare`, `arithmetic`), `block`, `value`, `object`, `member`, `op`, `operands` |

- **Empty text and empty lists are left out**, which keeps files short; they read back as
  empty.
- **A literal's JSON type is its kind:** `true`, `3` (whole number), `3.0` (number) or
  `"text"`. Choice literals are their entry id, as text.
- **Loading is all or nothing for blocks.** A block that cannot be read (an unknown step
  kind, a missing id) fails the whole load rather than dropping the block. An unknown key
  is only reported, and the asset still loads.
- A loaded asset is equal (`==`) to the one saved, and compiles the same.

## 5. Scenes

A scene (`.dscene`) holds the entities of one map with their components
([world.md](world.md)):

```json
{
  "entities": [
    { "guid": "aaaaaaaaaaaa4aaa8aaaaaaaaaaaaaaa", "name": "House" },
    {
      "components": { "sprite": { "image": "door.dsprite", "kind": "prop", "layer": 0 } },
      "guid": "bbbbbbbbbbbb4bbb8bbbbbbbbbbbbbbb",
      "name": "Door",
      "parent": "aaaaaaaaaaaa4aaa8aaaaaaaaaaaaaaa"
    }
  ],
  "format": "sparxel.scene",
  "version": 1
}
```

(Shown compacted; files have one key per line.)

- **Parents first:** roots in creation order, each followed by its descendants in order,
  so the hierarchy and its order rebuild exactly. Saving a loaded scene gives the same
  bytes (tested).
- **Components are keyed by their registered type name** and hold their properties as in
  §3.
- **Loading is checked in two passes.** The first checks the whole document: every entity
  has a valid GUID, no GUID is repeated or already in the world, and every parent is an
  earlier entity in the file or one already in the world. If anything fails, **nothing is
  created**, so a broken file never leaves a half-loaded map. The second pass creates the
  entities.
- **Problems inside components do not stop the load:** an unknown component type, for
  example from a plugin that is not installed, or a property value that does not fit, is
  reported and the rest of the entity loads.
- A scene can be loaded **under an entity already in the world**, by naming it as a
  parent.
- Saving and loading never recurse, so very deep hierarchies are safe (tested with
  20,000 levels).

## 6. Safety

| Input | Result | Test |
|---|---|---|
| Text that is not JSON | `parse()` returns nothing and an issue such as "The file is not valid JSON: parse error at line 1, column 10: ..." | "Hostile input is refused without crashing" |
| 100,000 nested lists | Parsed without overflowing the stack (nlohmann's parser does not recurse) | Same |
| Steps nested more than 64 deep | Refused: "This block is nested too deeply.", as the compiler refuses them | Same |
| Invalid UTF-8 in a string being saved | Replaced with U+FFFD rather than failing | Same |
| A file from a newer Sparxel | Refused with "saved by a newer version of Sparxel" | "Invalid logic documents are reported with their place" |

No function in this module throws for bad input.

## 7. Next increments

In order:

1. **Prefabs** (`.dprefab`) and prefab overrides in scenes, with the world's prefabs.
2. **Project settings** (`project.sparxel.json`).
3. **Save games:** properties flagged `saved`, with their own format version.
4. **Asset metadata** (`.meta` files with GUIDs and import settings).
