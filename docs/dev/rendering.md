# Rendering

> **Status: v0.1, the first step of Spike R.** A GPU device on the SDL3 GPU API, textures,
> offscreen render targets with readback, and a batched sprite renderer, with shaders
> compiled offline from HLSL. Tested pixel by pixel on a software GPU. The HD-2D features
> (lit billboards, shadows, depth of field, bloom) come next (§6).
>
> Code: [`engine/render/include/engine/render/render.h`](../../engine/render/include/engine/render/render.h),
> [`src/render.cpp`](../../engine/render/src/render.cpp) and the shaders in
> [`engine/render/shaders/`](../../engine/render/shaders/). Tests:
> [`tests/render/render_test.cpp`](../../tests/render/render_test.cpp).

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

## 4. Shaders (ADR-015)

- Shaders are written once, in **HLSL**, in `engine/render/shaders/` as
  `<name>.<vert|frag>.hlsl`.
- At build time, **SDL_shadercross** compiles each to **SPIR-V** (Vulkan) and **DXIL**
  (Direct3D 12), and `cmake/EmbedFile.cmake` embeds the results in the engine as byte
  arrays. Nothing is compiled while the game runs, and no shader files ship separately.
- shadercross is a vcpkg *host* dependency: a tool used during the build, not linked into
  the engine. The device picks the format its backend accepts.
- Resource bindings follow the SDL GPU rules: vertex-shader uniforms in `(b, space1)`;
  fragment-shader textures and samplers in `(t, space2)` and `(s, space2)`.

## 5. Testing without a graphics card

- The tests render headless and compare pixels exactly, except one blend that allows a
  single step of rounding.
- **Mesa's lavapipe**, a Vulkan driver that runs on the CPU, is enough. On Ubuntu:
  `sudo apt-get install mesa-vulkan-drivers`.
- Without any GPU the render tests are **skipped with a message**. With
  `SPARXEL_REQUIRE_GPU=1` a missing GPU **fails** them instead. Linux CI installs lavapipe
  and sets this, so rendering is really tested there. The Windows CI runners have no GPU,
  so the render tests are skipped on Windows until GPU-equipped runners exist (ADR-016).
- **AddressSanitizer:** lavapipe keeps some memory reachable only from its own globals.
  When SDL unloads the driver at exit, LeakSanitizer reports that memory (112 bytes) with
  misattributed stack frames. With the driver kept loaded
  (`LD_PRELOAD="$(gcc -print-file-name=libasan.so) /usr/lib/x86_64-linux-gnu/libvulkan_lvp.so"`)
  no leak is reported, so it is not Sparxel's.

## 6. Next increments

Towards the reference scene of Spike R (`plan/08` Phase 0, `plan/04` §4):

1. **Windows and presenting:** a window, a swapchain and frame pacing.
2. **3D:** a depth buffer, a perspective camera, and meshes for the diorama.
3. **Lit billboard sprites** with normal maps and shadows (`plan/04` §5).
4. **Post-processing:** tilt-shift depth of field, bloom and fog (`plan/04` §8).
5. **GPU timing** to measure frames against the `plan/04` §11 budgets on reference
   hardware.
6. **The ADR-004 decision:** record whether the SDL GPU API covers the reference scene, or
   a native Direct3D 12 backend is needed.
