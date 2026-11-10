# The Reflection Registry

> **Status: first version (v0).** Types, properties and choice lists are in place and
> tested. Actions, conditions and events (the no-code blocks) are the next increment
> (§6).
>
> Code: [`engine/foundation/include/engine/foundation/reflect.h`](../../engine/foundation/include/engine/foundation/reflect.h)
> and [`src/reflect.cpp`](../../engine/foundation/src/reflect.cpp). Tests:
> [`tests/foundation/reflect_test.cpp`](../../tests/foundation/reflect_test.cpp).

## 1. Why it exists

The plan makes reflection the single source of truth for everything a non-coder sees
(`plan/03` §1, principle 2). An engine feature describes its data **once**, in C++, with
the metadata a non-programmer needs. From that one description the engine generates:

- the editor's inspector panels: labels, tooltips, sliders with ranges and units, choice
  dropdowns, and the Basic and Advanced split;
- the no-code vocabulary: blocks that read and set properties (`plan/05` §2);
- search and the generated documentation pages;
- what is saved with a game (the "saved" flag) and, later, project-file serialization.

This is the structural fix for the lesson from Godot's VisualScript (`research/04`, L1):
because blocks are generated, no engine feature can be missing from the no-code layer.

## 2. Describing a type

```cpp
#include "engine/foundation/reflect.h"

enum class Facing : std::uint8_t { North, East, South, West };

struct PointLight {
    bool enabled = true;
    float radius = 4.0F;
    double intensity = 1.0;
    Facing facing = Facing::South;
};

void register_lighting(engine::reflect::Registry& registry) {
    using namespace engine::reflect;

    // Choice lists (enums) are registered before the types that use them.
    registry.add_choice_list<Facing>("facing")
        .entry(Facing::North, "north")
        .entry(Facing::East, "east")
        .entry(Facing::South, "south")
        .entry(Facing::West, "west");

    registry.add_type<PointLight>("point_light")
        .display_name("Point Light")
        .category("Lighting")
        .tooltip("Lights up everything around it.")
        .property<&PointLight::enabled>("enabled")
            .tooltip("Turns the light on or off.")
        .property<&PointLight::radius>("radius")
            .unit(Unit::Tiles)
            .range(0.0, 64.0)
            .step(0.5)
        .property<&PointLight::intensity>("intensity")
            .range(0.0, 10.0)
            .flags(property_flag::advanced)
        .property<&PointLight::facing>("facing");
}
```

What the registry fills in automatically:

| Item | Where it comes from |
|---|---|
| Kind (Yes/No, Whole number, Number, Text, Choice) | The member's C++ type |
| Default value | The member's value in a default-constructed object |
| Display name | Derived from the id: `radius_tiles` → "Radius tiles", unless set |
| Type id | A stable hash of the registered name (§4) |

## 3. Reading and writing values

```cpp
const TypeInfo* info = registry.find_type<PointLight>();
const PropertyInfo* radius = info->find_property("radius");

PointLight light;
SetResult result = set_property(light, *radius, Value{12.5}); // SetResult::Ok
std::optional<Value> value = get_property(light, *radius);     // holds 12.5
```

`set_property()` checks everything before it writes, and leaves the object unchanged
otherwise:

| Result | Meaning |
|---|---|
| `Ok` | Written |
| `WrongType` | The property belongs to a different C++ type than the object |
| `ReadOnly` | The property has `property_flag::read_only` |
| `WrongKind` | For example, text given to a number property |
| `OutOfRange` | Below `min` or above `max` (bounds are inclusive) |
| `UnknownChoice` | Not one of the choice list's entries |
| `NotRepresentable` | Fits the kind but not the member's C++ type, for example 1000 in an `int8` |

Values travel as `Value`, a `std::variant` of `bool`, `std::int64_t` (whole numbers and
choices), `double` (numbers) and `std::string` (text).

## 4. Rules and design decisions

| Rule | Why |
|---|---|
| **Ids are lowercase letters, digits and underscores, starting with a letter.** | Ids are written into project files, so they must be stable and portable. Display names are free text and can change at any time. |
| **Type ids are the 64-bit FNV-1a hash of the registered name.** | The same in every build and on every platform, so they can be stored in files. A collision between two names is reported (`TypeIdCollision`). |
| **No RTTI.** Types are matched to their descriptions by a per-type static address. | Runtime code is built without RTTI (ADR-002). The address variable is deliberately not `const`, so that MSVC's identical-data folding (`/OPT:ICF`) cannot merge two types' keys. |
| **No exceptions and no aborts during registration.** Problems are collected in `Registry::errors()`. | A broken description must not crash the editor. Tests check that the engine's registry is clean. |
| **Choice lists before the types that use them.** | Keeps registration single-pass and the error message exact (`UnknownChoiceList`). |
| **Ranges are checked against the member's default.** | A default outside its own range is almost always a typo (`DefaultOutOfRange`). |
| **Units never convert values.** | A unit only changes how a number is shown and edited ("12.5 tiles"). |
| **Supported member types:** `bool`, integers (not `char` types, not 64-bit unsigned), `float`, `double`, `std::string`, registered enums. | Each maps to exactly one no-code kind. Unsupported types fail at compile time with a message saying what is supported. |
| **Descriptions are never removed.** | Pointers returned by `find_type()` and friends stay valid for the registry's lifetime, so the editor can hold them. |

## 5. How it connects to the rest of the plan

| Consumer | How it will use the registry | Plan reference |
|---|---|---|
| Inspector (editor) | One widget per property: checkbox, spin box or slider with unit, text field, dropdown. Advanced and hidden flags decide visibility | `plan/06` §1, §3 |
| No-code blocks | "Set [property] of [object] to [value]" and "[property] of [object]" blocks for every property | `plan/05` §2, §8 |
| Save system | Properties flagged `saved` are written to save files automatically | `plan/03` §10; feature M1 |
| Project files | Ids and type ids are the keys used in the JSON project format | `plan/03` §4 |
| ECS (flecs) | Registered types become components; flecs reflection is fed from this registry, not the other way round | ADR-006 |
| Documentation | Every type and property gets a generated docs page from its metadata | Feature Q2 |

## 6. Next increments

In order:

1. **Actions, conditions and events.** Register functions with typed, described
   parameters and a type-erased invoker. These become the no-code blocks of `plan/05` §2.
   This also needs Logic IR integration, decided in the logic spike (ADR-007).
2. **More value kinds:** Colour, Position (2D and 3D), Direction, Time, object references
   and asset references (GUIDs), and lists.
3. **JSON serialization** of reflected objects, for the project format (`plan/03` §4).
4. **flecs bridge:** register reflected types as components (ADR-006).
5. **Docs generation:** produce a reference page per type from the metadata.
6. **Localization of metadata:** display names and tooltips become string-table keys.
