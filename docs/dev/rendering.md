# Rendering

> **Status: v0.2, Spike R under way.** A GPU device on the SDL3 GPU API, textures,
> offscreen render targets with readback, a batched sprite renderer, and a 3D world
> renderer with a depth buffer, alpha cutout, per-quad lighting colour and distance fog,
> with shaders compiled offline from HLSL. Tested pixel by pixel on a software GPU. Shadows,
> normal-mapped lighting, depth of field and bloom come next (§8).
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
| `SpriteRenderer` | Draws textured, tinted rectangles in batches |
| `WorldRenderer` | Draws textured quads in 3D with a depth buffer, alpha cutout, lighting colour and fog (§7) |

## 3. Sprites

- **Coordinates are pixels of the target:** (0, 0) is the top-left corner and y grows
  downwards, as in every pixel-art tool.
- **Nearest-neighbour sampling** and exact pixel placement: a 2 × 2 texture drawn at
  8 × 8 becomes four clean 4 × 4 blocks, with no blurred edges (tested pixel by pixel).
- **Tint** multiplies the texture's colour; its alpha fades the sprite.
- **Alpha blending,** with sprites drawn in the order given, so later ones are on top.
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
- **Colour:** each quad's colour multiplies its texels: lighting times tint.
- **Fog:** linear with distance from the eye, from `start` to `end`, towards the fog
  colour; off when `end` is not past `start`.
- **Batching:** one draw call per run of quads sharing a texture, as for sprites.
- Translucent quads (blended, sorted back to front) are not supported yet: every texel is
  either fully drawn or cut out.

## 8. Next increments

Towards the reference scene of Spike R (`plan/08` Phase 0, `plan/04` §4):

1. **Frame pacing:** vsync choice and frame-time measurement.
2. **Translucency:** sorted, blended quads for glass, water and fading sprites.
3. **Lit billboard sprites** with normal maps, point lights and shadows (`plan/04` §5).
4. **Post-processing:** tilt-shift depth of field, bloom and fog (`plan/04` §8).
5. **GPU timing** to measure frames against the `plan/04` §11 budgets on reference
   hardware.
6. **The ADR-004 decision:** record whether the SDL GPU API covers the reference scene, or
   a native Direct3D 12 backend is needed.
