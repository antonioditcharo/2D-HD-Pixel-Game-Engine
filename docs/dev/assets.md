# Assets

> **Status: v0.1, the first step of Spike A.** The asset database gives every file in a
> project's assets folder a GUID kept in a `.meta` file, detects what changed between
> scans, survives renames and moves, and imports PNG images. Every behaviour below is
> covered by tests. Cooking, the cache and hot reload come next (§6).
>
> Code: [`engine/assets/include/engine/assets/assets.h`](../../engine/assets/include/engine/assets/assets.h)
> and [`src/assets.cpp`](../../engine/assets/src/assets.cpp). Tests:
> [`tests/assets/assets_test.cpp`](../../tests/assets/assets_test.cpp).

## 1. What it is for

A project's sprites, maps, logic and sounds are files in its `assets/` folder
(`plan/03` §4). Everything that refers to an asset, such as a scene, a logic block or a
prefab, refers to its **GUID**, never its path. Creators can then rename and reorganise
freely without breaking anything. This module keeps those GUIDs and tells the rest of the
engine what changed, which drives importing and hot reload (`plan/03` §5).

## 2. Using it

```cpp
assets::AssetDatabase db(project / "assets");
assets::AssetChanges changes = db.refresh();     // scan; call again whenever needed

for (engine::Guid guid : changes.modified) { /* re-import, hot reload */ }
const assets::AssetRecord* hero = db.find("sprites/hero.png");

std::string error;
auto bytes = db.read(hero->guid);
std::optional<assets::Image> image = assets::decode_png(*bytes, error);  // RGBA8
```

## 3. `.meta` files

Next to every asset, for example `sprites/hero.png.meta`:

```json
{
  "format": "sparxel.meta",
  "guid": "0f8e2a6c1b7d4e3fa9c25d1e7b3a4c90",
  "importer": "image",
  "version": 1
}
```

- Created automatically for any file that has none. They are text, deterministic and
  meant to be committed with the asset.
- The **importer** is chosen from the extension when the file is first seen: `image` for
  PNG, `file` for everything else. Later importers (Aseprite, glTF, audio) add their own
  settings here.

## 4. What `refresh()` does

| Situation | Result |
|---|---|
| A new file | A `.meta` with a new GUID is written; reported in `added` |
| Content changed | Reported in `modified`. Size and modification time are checked first, and a file is hashed (FNV-1a) only when they change |
| Renamed or moved **with** its `.meta` | Same GUID, reported in `moved` |
| Renamed or moved **without** its `.meta` (in a file manager) | Recognised by identical content and size, so it **keeps its GUID**. A new `.meta` is written next to it, and the stale one left behind is removed, since it held only that GUID |
| Copied **with** its `.meta` (two files, one GUID) | The asset already known keeps the GUID (else the first by path); the copy gets a new one, and the repair is reported |
| Deleted | Reported in `removed`. A `.meta` left behind is reported and kept |
| A `.meta` that cannot be read | Reported; **the file is never overwritten** and the asset is skipped until it is fixed, because it may hold settings a person wrote |
| Names starting with `.` | Ignored, including folders such as `.sparxel/` (the local cache, `plan/03` §4) |

Every list in `AssetChanges` and the issues are sorted, so results are deterministic.

## 5. Images

- `decode_png()` turns PNG bytes into RGBA8 pixels, rows top to bottom, ready for
  `render::Device::create_texture()`. Images are converted to RGBA8 whatever their stored
  format; the tests cover RGBA images, and palette and greyscale PNGs are still to be
  tested.
- `encode_png()` writes RGBA8 as PNG, for tools and tests. A round trip is exact (tested).
- Both use the PNG support built into SDL 3.4, so no extra library is needed.

## 6. Next increments

Towards Spike A, "file watcher → import → cache → hot reload of a sprite into the running
viewport" (`plan/08` Phase 0):

1. **Import and cache:** importers produce cooked data in the derived-data cache under
   `.sparxel/`, keyed by content hash, importer version and settings (`plan/03` §5).
2. **Hot reload:** the editor calls `refresh()` when the operating system reports a change
   (ReadDirectoryChangesW on Windows), and reloads what changed into the running game.
3. **Aseprite slices** (Aseprite files themselves are imported: [sprite-studio.md](sprite-studio.md)).
4. **Packaging** for export: a dependency walk from the start scene.
