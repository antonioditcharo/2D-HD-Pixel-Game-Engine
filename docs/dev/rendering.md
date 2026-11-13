# Rendering

> **Status: v0.2, Spike R under way.** A GPU device on the SDL3 GPU API, textures,
> offscreen render targets with readback, a batched sprite renderer, and a 3D world
> renderer with a depth buffer, alpha cutout, per-quad lighting colour and distance fog,
> with shaders compiled offline from HLSL, point lights, shadow decals shaped by a blob or
> by any picture, and post-processing. Tested pixel by pixel on a software GPU.
>
> Code: [`engine/render/include/engine/render/render.h`](../../engine/render/include/engine/render/render.h),
> [`src/render.cpp`](../../engine/render/src/render.cpp) and the shaders in
> [`engine/render/shaders/`](../../engine/render/shaders/). Tests:
> [`tests/render/render_test.cpp`](../../tests/render/render_test.cpp) and
> [`world_test.cpp`](../../tests/render/world_test.cpp).

## 1. What it is for

Everything the player sees goes through this module: the diorama scene, sprites, the UI
and the editor's viewports (`plan/04`). It is the thin in-house layer over the SDL3 GPU
API that ADR-004 calls for. SDL types never appear in its public header, so a native
Direct3D 12 backend can be added later without touching the rest of the engine.

## 2. Using it

```cpp
std::string error;
auto device = render::Device::create({.headless = true}, error);   // null if no GPU
auto target = device->create_render_target(320, 180);
auto sprites = render::SpriteRenderer::create(*device);
auto hero = device->create_texture(16, 16, hero_rgba);              // RGBA8, rows top-down

sprites->begin(*target, {20, 24, 40, 255});
sprites->draw(*hero, {0, 0, 16, 16}, {152, 82, 16, 16});            // source, destination
sprites->end();

std::vector<std::uint8_t> pixels = target->read_pixels();            // waits for the GPU
```

| Object | What it is |
|---|---|
| `Device` | The GPU. Headless (no window, for tests, tools and offline rendering) or ordinary. Reports `driver_name()`: `vulkan`, `direct3d12` or `metal` |
| `Texture` | RGBA8 pixels uploaded to the GPU |
| `RenderTarget` | An offscreen image to draw into; `read_pixels()` copies it back |
| `SpriteRenderer` | Draws textured, tinted rectangles in batches, or any four-sided shapes (turned, scaled or flipped pictures) |
| `WorldRenderer` | Draws textured quads in 3D with a depth buffer, alpha cutout, see-through quads blended farthest first, lighting colour, point lights per pixel, shadow decals and fog (§7) |

## 3. Sprites

- **Coordinates are pixels of the target:** (0, 0) is the top-left corner and y grows
  downwards, as in every pixel-art tool.
- **Nearest-neighbour sampling** and exact pixel placement: a 2 × 2 texture drawn at
  8 × 8 becomes four clean 4 × 4 blocks, with no blurred edges (tested pixel by pixel).
- **Tint** multiplies the texture's colour; its alpha fades the sprite.
- **Turned and scaled sprites:** `draw(texture, source, corners, tint)` takes where the
  source's top-left, top-right, bottom-right and bottom-left go, instead of a rectangle.
  Nothing is culled, so a flipped picture (its corners in the other order) is drawn too.
  The rectangle form is the same two triangles.
- **Alpha blending,** with sprites drawn in the order given, so later ones are on top.
- **Clearing or keeping:** `begin(target, colour)` clears the target first;
  `begin_over(target)` keeps what it holds, to draw over a finished frame, such as text
  and message boxes over the world ([ui.md](ui.md)).
- **Batching:** one draw call per run of consecutive sprites that share a texture. 1,000
  sprites from one texture are one draw call; alternating textures cost a call per run, to
  keep the order. Packing sprites into atlases (the asset pipeline's job) keeps runs long.

## 4. Windows and presenting

- `Device::create_window(title, width, height)` opens a resizable window connected to
  the GPU.
- `Window::present(image, letterbox)` shows a render target **pixel-perfect**: upscaled
  by the largest whole number that fits, centred, with the letterbox colour around it
  (`plan/04` §3). `fit_integer()` computes that placement. For example, 320 × 180 fills
  1920 × 1080 at exactly 6×, and sits at 4× with a 43-pixel border in 1366 × 768. A
  window smaller than the image shows it at 1×, centred and cropped.
- A **headless** device runs on SDL's offscreen video driver, which offers Vulkan
  surfaces only. So it can show windows with Vulkan, but not with Direct3D 12, whose
  swapchain needs a real window handle (checked in SDL 3.4.18's source).
  `create_window()` then fails with a message that says so; an ordinary device has no
  such limit.
- Tests open a real window with a Vulkan swapchain through the offscreen driver on
  lavapipe, and present small and cropped images to it. On other backends the test
  checks the message instead.
- **Pacing.** Every submission takes a fence, and at most 3 may be unfinished: past that,
  the device waits for the oldest. A window's swapchain paces frames, but a headless run
  has nothing else to. Before this, headless runs queued work faster than the GPU
  finished it: memory grew every frame (230 MB after 300 frames, 390 MB after 900) and
  lavapipe crashed after about 900 to 1,850 frames, in both styles. Now a 3,000-frame
  run stays at about 115 MB, and the `player_long_run` test plays 2,000 frames.

## 5. Shaders (ADR-015)

- Shaders are written once, in **HLSL**, in `engine/render/shaders/` as
  `<name>.<vert|frag>.hlsl`.
- At build time, **SDL_shadercross** compiles each to **SPIR-V** (Vulkan) and **DXIL**
  (Direct3D 12), and `cmake/EmbedFile.cmake` embeds the results in the engine as byte
  arrays. Nothing is compiled while the game runs, and no shader files ship separately.
- shadercross is a vcpkg *host* dependency: a tool used during the build, not linked into
  the engine. The device picks the format its backend accepts.
- Resource bindings follow the SDL GPU rules: vertex-shader uniforms in `(b, space1)`;
  fragment-shader textures and samplers in `(t, space2)` and `(s, space2)`.

## 6. Testing without a graphics card

- The tests render headless and compare pixels exactly, except one blend that allows a
  single step of rounding.
- **Mesa's lavapipe**, a Vulkan driver that runs on the CPU, is enough. On Ubuntu:
  `sudo apt-get install mesa-vulkan-drivers`.
- Without any GPU the render tests are **skipped with a message**. With
  `SPARXEL_REQUIRE_GPU=1` a missing GPU **fails** them instead. Linux CI installs lavapipe
  and sets this, so rendering is really tested there. On the Windows CI runners SDL finds
  a Direct3D 12 device (the log reports `GPU driver: direct3d12`), so the render tests
  run there too, except presenting to a window (§4). Windows CI does not set the flag,
  so a runner without a device would skip them.
- **AddressSanitizer:** lavapipe keeps some memory reachable only from its own globals.
  When SDL unloads the driver at exit, LeakSanitizer reports that memory (112 bytes) with
  misattributed stack frames. With the driver kept loaded
  (`LD_PRELOAD="$(gcc -print-file-name=libasan.so) /usr/lib/x86_64-linux-gnu/libvulkan_lvp.so"`)
  no leak is reported, so it is not Sparxel's.

## 7. The world renderer (3D)

`WorldRenderer` draws textured quads in 3D for the Diorama style (`plan/04` §4); the scene
side is in [scene2d.md](scene2d.md) §7.

```cpp
const Mat4 camera = Mat4::perspective(fov, aspect, near, far) * Mat4::look_at(eye, target, up);
world->begin(*target, sky, camera, eye, Fog{haze, 20.0F, 60.0F});
world->draw(*texture, {top_left, top_right, bottom_right, bottom_left}, source, {r, g, b, a});
world->end();
```

- **Coordinates:** right-handed, y up. `Mat4` is stored row by row and applied to column
  vectors; `perspective()` and `orthographic()` give the GPU's clip space (depth 0 at the
  near plane, 1 at the far one; y up), which SDL GPU uses on every backend. The vertex
  shader receives the matrix as four rows and applies it with dot products, so packing
  rules cannot differ between SPIR-V and DXIL.
- **Depth:** a depth buffer the size of the target, in the most precise format the GPU
  offers (32-bit float, else 24- or 16-bit), cleared each frame; nearer wins whatever the
  draw order (tested, and the test fails with depth testing off).
- **Alpha cutout:** texels below half opacity are discarded, neither drawn nor hiding what
  is behind. Pixel art keeps hard edges and needs no sorting, as `plan/04` §5.2 asks:
  sprites are alpha-tested and write depth.
- **Colour:** each quad's colour multiplies its texels: the ambient and sun light times
  the tint.
- **Point lights,** per pixel: `begin()` takes up to `max_point_lights` (16)
  `PointLight`s (position, radius, colour times strength), and each quad's `Surface`
  says how it takes them: its normal, and a wrap from 0 (Lambert: lit only from the
  front) to 1 (half-Lambert, for billboards, which should not go black when a light is
  behind them). Each light adds `albedo × colour × falloff × facing` to the quad's
  colour, where the falloff is `(1 − (d / r)²)²`: 1 at the light, smoothly 0 at its
  radius (`light_falloff()`). A quad with no normal takes no point light. Broken lights
  (not finite, or no radius) are skipped. Tested pixel by pixel: 0.5625 of a red light
  2 units in front of a wall at half its radius; dimmer along the wall and nothing past
  the radius; nothing from behind without wrap, and half side-on with wrap 1; albedo;
  two lights adding up; only the first 16 counting.
- **No atlas bleeding:** texture coordinates stay a hundredth of a texel inside the
  source, because in perspective a coordinate exactly on a texel's edge can round into
  the neighbouring tile of an atlas and show along tile seams (found in the sample
  village; [player.md](player.md) §3).
- **Fog:** linear with distance from the eye, from `start` to `end`, towards the fog
  colour; off when `end` is not past `start`.
- **Shadow decals:** `draw_shadow(corners, darkness)` queues a soft round shadow, drawn
  after every quad by a second pipeline that multiplies what is there (it only darkens),
  depth-tested but not depth-written, so it lies on surfaces and hides behind nearer
  ones. Tested: exactly half as bright at the middle with darkness 0.5, a soft rim, the
  corners of its square untouched, hidden behind a nearer quad, and darkness 0 drawing
  nothing.
- **Shaped shadows:** `draw_shadow(shape, corners, source, darkness)` takes its shape from
  the alpha of a picture's `source` pixels, and a darkness for each corner, blended
  between them: a character's silhouette cast on the ground by the sun (scene2d §7).
  Tested on a picture half opaque and half clear, dark 1 along the top fading to 0 at
  the bottom: the exact grey in three rows, the clear half and the rest untouched.
  Shadows sharing a shape are drawn together, one draw call per run.
- **The sun's shadows** (`set_sun_shadow()`, `SunShadow`): before the frame, every
  opaque quad that `casts_shadow` is drawn from the sun into a 2048 × 2048 depth texture
  (`sun_depth.frag.hlsl`: depth only, cut-out texels cast nothing, with a slope-scaled
  depth bias). The world shader then looks each pixel up in it, a little off its
  surface along its normal, nine samples for a soft edge, and multiplies the sun's part
  of its light (the vertex colour) by 1 − darkness where it is hidden. Faces turned away
  from the sun are left as their shading made them; billboards always take it; point
  lights still shine into shadows. The map is cleared once when the renderer is made,
  so it is valid in frames without the sun's shadows. Tested: a card 2 in front of a
  wall with the sun at 45 degrees darkens exactly the wall 2 to its left, by half (128
  of 255); nothing without the sun's shadows, with a card that does not cast, or on a
  wall facing away from the sun.
- **Batching:** one draw call per run of quads sharing a texture, as for sprites.
- **See-through quads** (glass, water, fading characters): a quad whose colour's alpha is
  below 1, or whose texture has partly see-through pixels (`Texture::translucent()`,
  found when it is made), is drawn after the opaque quads and the shadows, by a third
  pipeline that blends it over what is behind it, farthest first (by its middle's
  distance from the eye; equally far, in the order given). It is depth-tested but not
  depth-written, so it never hides what is behind it, and the frame stays opaque. The
  same pixel shader serves both, in a blended mode that keeps the texel's alpha times
  the colour's instead of cutting it out. Opaque quads are cut out at alpha one half, as
  before. Fully clear quads are not drawn. Tested: two half see-through quads given
  nearest first still blend farthest first to the exact colour; one over a solid quad
  given after it but lying behind it; a picture with a half-alpha texel blends on its
  own; `translucent()` true only for partial alpha; the counts in `Stats`.

## 8. Post-processing: the Look

`PostProcessor` applies a `Look` to a finished frame (`plan/04` §8): the world is drawn into
one target, and `apply(source, destination, look)` writes it, processed, into another of
the same size.

| Setting | Does | Range (default) |
|---|---|---|
| `tilt_shift`, `focus_center`, `focus_height`, `blur`, `blur_ramp` | Tilt-shift depth of field, the "miniature diorama" look: a band across the screen stays sharp, and rows above and below blur more the further they are, up to `blur` pixels over `blur_ramp` of the height | off; centre 0 to 1 (0.55), band 0 to 1 (0.35), 0 to 12 px (3), 0 to 1 (0.25) |
| `bloom`, `bloom_threshold`, `bloom_radius` | Bright parts glow: what is brighter than the threshold spreads by the radius, added back times `bloom` | 0 to 2 (0: off), 0 to 1 (0.75), 0 to 16 px (6) |
| `exposure`, `contrast`, `saturation` | Colour grading: brightness; around mid-grey; from grey (0) to vivid | 0 to 4, 0 to 2, 0 to 2 (all 1) |
| `vignette` | Darkens towards the corners | 0 to 1 (0) |

- **Two full-screen passes.** The first blurs along rows, by each row's tilt-shift radius
  and for bloom, into two targets at once; the second finishes both down the columns,
  adds the bloom, grades and applies the vignette. Blurs are Gaussian, up to 12 pixels
  each side for tilt-shift and 16 for bloom; pixels are read with `Load`, so nothing is
  filtered by accident.
- **Neutral is exact.** The defaults change nothing (`neutral()`), and a neutral pass
  copies a frame byte for byte (tested), so games without a look stay pixel-perfect;
  the runtime skips the pass entirely then.
- Values out of range are clamped; the source and destination must be different targets
  of the same size.
- **Tests (GPU):** a neutral look copies a 37 × 23 frame exactly; tilt-shift keeps the
  band's rows exactly and turns one-pixel black and white columns mid-grey far from it;
  bloom spreads a glow from a white dot that fades with distance, and none from a dim
  one below the threshold; exposure 1.5, contrast 0, saturation 0 (the luma) and the
  vignette give the expected colours, and exposure 100 is clamped to 4; targets that are
  the same or of different sizes are refused.

## 9. Next increments

Towards the reference scene of Spike R (`plan/08` Phase 0, `plan/04` §4):

1. **Frame pacing in windows:** vsync choice and frame-time measurement.
2. **Water that moves:** scrolling and rippling surfaces, and reflections (`plan/04` §5).
3. **Shadows** from point lights, cascades for very large maps, and normal-mapped
   sprites
   (`plan/04` §5 and §7).
4. **More of the Look** (`plan/04` §8): LUT colour grading, physical depth of field for
   cutscenes, volumetric light, and Looks that blend by zone and time of day.
5. **GPU timing** to measure frames against the `plan/04` §11 budgets on reference
   hardware.
6. **The ADR-004 decision:** record whether the SDL GPU API covers the reference scene, or
   a native Direct3D 12 backend is needed.

**The light's colour.** The Look also has a tint (`tint_r`, `tint_g`, `tint_b`, each 0 to 2,
1 unchanged), multiplying each channel after grading: day and night colour the frame with it
([game.md](game.md), Day and night). A Look that becomes non-neutral while a game plays (night
falling) gets its post-processing pass then.
