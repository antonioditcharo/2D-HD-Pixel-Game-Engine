# Research 01: The HD-2D Visual Language

> **What this document answers:** what the "HD-2D" look actually is, which games define it,
> how the people who made it describe it, and what it technically demands from an engine.
>
> **How to read citations:** `[S#]` refers to an entry in [`../sources.md`](../sources.md).
> Paragraphs labelled **Analysis** are engineering interpretation by the author of this plan,
> not facts reported by a source. They are included because an engine plan needs them, but
> they are kept clearly separate from verified facts.

---

## 1. Definition and origin

- "HD-2D" is the name Square Enix uses for a style that combines traditional 2D elements,
  such as pixel art and billboard sprites, with 3D environments [S1].
- The first HD-2D game was *Octopath Traveler* (2018), developed by Square Enix with
  Acquire on **Unreal Engine 4**. Epic's spotlight article reports that Acquire's lead
  programmer said the small team (six programmers at peak) relied heavily on UE4's tools
  and editors [S2].
- **Trademark:** Square Enix filed trademarks for "HD-2D" and "HD2D" in Europe and Japan in
  January 2019 [S11][S12]. A third-party trademark database lists a US registration
  (reg. no. 6040011, registered April 2020) [S13]; this was **not** checked against
  USPTO records directly. The trademark covers the *name*, not the art style itself [S1].
  - **Consequence for this project:** the art style is fair game, but the product should
    not be marketed with the term "HD-2D". The repository name "2D-HD" is close to that
    mark. See `plan/09-risks-quality-legal.md` (risk R-06).

## 2. Reference titles

| Title | First release | Engine | Notes |
|---|---|---|---|
| Octopath Traveler | 2018 | Unreal Engine 4 [S1][S2] | Origin of the term |
| Triangle Strategy | 4 Mar 2022 (Switch) | Unreal Engine 4 [S6] | Developed with Artdink; grid tactics |
| Live A Live (remake) | 22 Jul 2022 | Unreal Engine 4 [S7] | Developed with Historia |
| Octopath Traveler II | 24 Feb 2023 | Unreal Engine 4 [S5] | Dynamic day/night lighting [S3] |
| Dragon Quest III HD-2D Remake | 14 Nov 2024 [S10] | *not verified* | |
| Dragon Quest I & II HD-2D Remake | 30 Oct 2025 (Steam: 31 Oct) [S8] | *not verified* | |
| Octopath Traveler 0 | 4 Dec 2025 (Steam: 5 Dec) [S9] | *not verified* | |

Similar-looking games from other studios include *Wandering Sword* (2023) and
*Eiyuden Chronicle: Hundred Heroes* (2024) [S1]. The latter is built in **Unity** [S16].

**Analysis:** every reference title is a JRPG or a tactics RPG. That tells us which genre kits and templates the engine needs first (see
`plan/02-feature-scope.md`). Action games and platformers can use the style too, but they
are not where the style has been proven.

## 3. The recipe, layer by layer

The table below breaks the look into layers. The "What defines it" column is sourced. The
"Engine requirement" column is **Analysis**.

| # | Layer | What defines it | Engine requirement (Analysis) |
|---|---|---|---|
| 1 | **3D world** | Fully 3D backgrounds with complex camerawork. That is what separates HD-2D from older "2.5D" [S1]. | A real 3D scene graph and renderer, with tools to build 3D spaces out of pixel-art textures. |
| 2 | **Pixel-art characters** | Pixel-art sprites and billboards placed in the 3D world [S1]. | Billboard sprite rendering, directional sprite sets, and sprites that are depth-correct against the 3D world. |
| 3 | **Dynamic light and shadow** | A point light that makes characters and objects cast shadows [S1]. Octopath Traveler II made dynamic day/night lighting a major upgrade [S3]. | Real-time lights, sprites that both receive and cast shadows, and a time-of-day system. |
| 4 | **Lens and atmosphere** | Post-processing such as tilt-shift, depth of field, bloom, volumetric lighting, fog and particle effects [S1]. | A full post-processing stack with tilt-shift depth of field, all tuned for pixel art. |
| 5 | **Parallax** | Parallax scrolling is used to give the style a "more modern feel" [S1]. | Parallax backdrop layers inside a 3D scene. |
| 6 | **Camera discipline** | With Octopath Traveler II's heavier 3D camera moves, the team had to ask how far the camera could rotate before the image broke down [S3]. | Camera rigs with authored limits, because flat sprites break at extreme angles. |
| 7 | **Art density** | On large screens the sprites "looked lonely and simple", so the team increased the density of tile variations and colours [S4]. | Tools that make variation cheap: random tile variants, scatter brushes and palette variants. |

## 4. What the developers learned

Producer Masashi Takahashi and director Keisuke Miyauchi described an iterative search for
the look [S4]:

- The first attempts **lacked depth** and were "uninteresting".
- A later attempt "went too far on the resolution and saturation, losing the appeal of
  pixel art".
- It was hard to decide whether elements like **water** should be pixel art or photoreal.

For Octopath Traveler II, Acquire aimed for high-resolution art that still looked like it
belonged in a pixel game [S3].

**Analysis: what this means for a no-code engine.** The HD-2D look is a *balance*, and
first-time users will not find that balance on their own. The engine should therefore:

1. Ship **"Look" presets** that are already balanced: depth of field strength, bloom,
   saturation and fog tuned together, so users start from a good look and adjust from there.
2. Provide **guardrails** that warn when the result drifts away from pixel art. Examples
   include a texel-density checker, a saturation and contrast histogram, and a
   "pixel fidelity" indicator that flags textures or effects at the wrong scale.
3. Offer **water, foliage and sky presets** in matching pixel and stylized variants, so the
   "pixel vs photoreal" decision is made consistently across the project.

## 5. Related looks the engine should support

"2D-HD pixel games" covers more than Square Enix's exact recipe. Four families appear in the
games researched. Supporting all four from one renderer is realistic because they share
most of their building blocks.

Grouping games into these four families is **Analysis**. The examples and the facts in the
"Pixel-stability strategy" column are cited.

| Family | Example | How it works | Pixel-stability strategy |
|---|---|---|---|
| **A. Diorama (HD-2D style)** | Octopath Traveler [S1] | A 3D world rendered at display resolution with pixel-art textures and billboard sprites, heavy post-processing and a freely moving 3D camera. | Pixels live in *texture space*, not screen space, so the render does not need a pixel-locked camera. Texture filtering must prevent shimmer (§6.6). |
| **B. Retro 3D pixel** | Community "3D pixel art" techniques [S19][S20] | Render the 3D scene to a low-resolution target, then upscale with integer scaling. | Snap the camera and objects to the low-resolution pixel grid. Pixel creep "can only ever be solved for orthographic projections" [S19]. A 2026 paper proposes a perspective-stable method, with an unresolved disocclusion trade-off [S20]. |
| **C. Lit 2D pixel art** | Sea of Stars (Unity, custom render pipeline for full dynamic lighting) [S14][S15] | Pure 2D scene. Sprites carry normal and mask maps and are lit by 2D lights. Unity's URP 2D renderer offers this pattern [S26]. | Pixel-perfect 2D camera with integer scaling. |
| **D. 3D-baked pixel sprites** | Dead Cells [S17][S18] | Characters are modelled and animated in 3D, then batch-rendered into low-resolution sprite frames. | Baked offline, so runtime stability is not an issue. |

Two data points show how hard this is with current tools:

- Sabotage Studio built a **custom render pipeline** in Unity for Sea of Stars [S14]. One
  engineer worked full-time on the dynamic lighting for about five years, and all of the
  studio's assets "have to answer to the technical needs" of that lighting [S15].
- Dead Cells' 3D-to-pixel conversion relied on a **private in-house tool** that was never
  released publicly [S17].

**Analysis:** the highest-value thing this engine can offer is to make these pipelines
*built in and turnkey*. Today each of them costs a studio years of specialist work.

## 6. Technical challenges and how to address them

All of section 6 is **Analysis**. Each item lists the problem, then the mitigation the plan
adopts. These feed directly into `plan/04-rendering-pipeline.md`.

### 6.1 Texel-density mismatch
- **Problem:** if a wall texture shows 1 texel per 4 screen pixels but a character sprite
  shows 1 texel per 3, the world and the characters stop looking like one coherent pixel
  image.
- **Mitigation:** a project-wide **Pixels-Per-Unit (PPU)** setting. Sprite quads are sized
  automatically from their pixel dimensions. Imported meshes are UV-checked against the
  PPU, and a texel-density overlay in the viewport flags mismatches in red.

### 6.2 Billboard orientation and directional sprites
- **Problem:** flat sprites look wrong when the camera orbits too far (see §3, row 6).
- **Mitigation:**
  - Billboard modes: upright (rotates around Y only), full (always faces the camera), fixed
    (signs, decals) and ground-aligned.
  - Directional sprite sets with 1, 2, 4 or 8 directions. The engine picks the frame from the
    angle between the character's facing and the camera.
  - Per-camera rotation limits that are visible in the camera editor.

### 6.3 Sprites casting shadows
- **Problem:** a camera-facing card seen side-on from the light casts a sliver-thin shadow,
  or none at all.
- **Mitigation:**
  - In the shadow pass, cast shadows from an alpha-tested card that is re-oriented *toward
    the light*.
  - Offer "blob" shadows as a stylized fallback.
  - Let users choose per sprite: silhouette, blob, or none.

### 6.4 Sprites meeting 3D ground and walls
- **Problem:** feet sink into slopes, and sprites clip into walls they stand against.
- **Mitigation:**
  - The sprite pivot defaults to the feet.
  - A configurable depth offset pushes the sprite toward the camera.
  - Ground-snapping uses the collision surface.
  - An optional per-sprite depth map lets self-occlusion read correctly.

### 6.5 Lighting flat pixel art
- **Problem:** flat sprites have a single normal, so lighting reads flat. Automatically
  generated normal maps can look like "plastic wrap" [S50], and a 2022 study notes that
  normal-map methods built for 3D games may give poor results on pixel art [S83].
- **Mitigation:**
  - Auto-generate normal maps from presets ("rounded", "chiselled", "flat-shaded").
  - Provide a **normal-paint brush**: the artist picks a light direction and paints it per
    pixel.
  - Add rim and wrap lighting controls.
  - Offer an optional **palette-locked lighting** mode, where lit colours snap to the
    sprite's palette ramps, for purists.

### 6.6 Texture shimmer under perspective
- **Problem:** nearest-neighbour sampled textures crawl and shimmer when they move at
  non-integer scales. Distant textures produce moiré.
- **Mitigation:**
  - A "crisp-AA" sampling mode that blends only across texel edges, so textures stay sharp
    but stable.
  - Mipmaps generated with area or nearest filters.
  - Anisotropic filtering on ground planes.
  - All of these are configurable per material, with the defaults chosen by the selected
    Render Style.

### 6.7 Depth of field with alpha
- **Problem:** depth of field needs depth, and alpha-blended sprites do not write depth.
- **Mitigation:**
  - Pixel art is naturally hard-edged, so sprites default to **alpha-test** and write depth.
  - Truly translucent effects such as particles and glass are composited in a separate pass
    with a depth-aware blur approximation.

### 6.8 Pixel creep in the Retro family
- **Mitigation:**
  - Default the Retro family to an orthographic camera, with camera and object snapping
    [S19].
  - Offer perspective only as an "experimental" option until a stable technique is proven.
    Texel splatting [S20] is a candidate for a later research spike.

### 6.9 Crisp UI and text
- **Problem:** UI scaled with the 3D render resolution turns blurry.
- **Mitigation:** UI renders in its own pass at an integer multiple of a "UI pixel scale",
  independent of the 3D resolution. Bitmap pixel fonts are first-class, and TTF fonts are
  rasterized at exact pixel sizes.

### 6.10 Performance with heavy post-processing
- **Problem:** many lights, volumetrics and depth of field at 1440p or 4K are expensive.
- **Mitigation:**
  - Quality tiers (Low / Medium / High / Ultra) that turn individual features down or off.
  - Per-pass GPU budgets enforced by automated performance tests.
  - Internal render scaling that is decoupled from the output resolution.

## 7. Requirements derived from this research

These requirements are referenced by ID from the plan documents.

| ID | Requirement |
|---|---|
| R-VIS-01 | The renderer supports three Render Styles that share one pipeline: **Diorama** (Family A), **Retro 3D** (B) and **Classic 2D** (C). |
| R-VIS-02 | Billboard sprites in upright, full, fixed and ground-aligned modes, with 1/2/4/8 directional sets. |
| R-VIS-03 | Sprites receive dynamic lighting through normal, emissive and specular maps, and cast shadows (silhouette, blob or none). |
| R-VIS-04 | Point, spot, directional and area lights, plus a time-of-day system (sun and moon paths, colour ramps, sky). |
| R-VIS-05 | Post-processing stack: tilt-shift and physical depth of field, bloom, height and distance fog, volumetric light shafts, SSAO, colour grading (LUT), vignette, grain, chromatic aberration and an optional CRT filter. |
| R-VIS-06 | "Look" presets that bundle post, fog, grading and lighting, and that can blend by zone and time of day. |
| R-VIS-07 | A project-wide PPU setting, automatic sprite sizing and a texel-density checker overlay. |
| R-VIS-08 | Per-material texture-filtering modes (nearest, crisp-AA, bilinear) and a pixel-aware mipmap policy. |
| R-VIS-09 | Sprites are alpha-tested and write depth by default, and integrate correctly with depth of field and fog. |
| R-VIS-10 | Camera rigs with authored rotation and pitch limits, zones, rails and cutscene control. |
| R-VIS-11 | Parallax backdrop layers and pixel skyboxes inside 3D scenes. |
| R-VIS-12 | The Retro 3D style renders at a low internal resolution with integer upscaling and orthographic snapping. |
| R-VIS-13 | UI renders at an integer UI-pixel scale, with first-class bitmap fonts. |
| R-VIS-14 | Quality tiers with enforced per-pass GPU budgets (see `plan/04`). |
| R-VIS-15 | Built-in authoring for normal and lighting maps: auto-generation presets plus normal painting. |
| R-VIS-16 | Pixel-art water, foliage, sky and weather presets that keep a consistent level of stylization. |
| R-VIS-17 | (Later) a 3D-to-pixel sprite baker for Family D workflows. |
| R-VIS-18 | (Later, optional) palette-locked lighting mode. |
