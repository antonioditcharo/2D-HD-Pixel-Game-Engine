#include "engine/render/render.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>

#include "sprite_frag_dxil.h"
#include "sprite_frag_spirv.h"
#include "sprite_vert_dxil.h"
#include "sprite_vert_spirv.h"
#include "world_frag_dxil.h"
#include "world_frag_spirv.h"
#include "world_vert_dxil.h"
#include "world_vert_spirv.h"

namespace engine::render {

// ---------------------------------------------------------------------------------------
// Native state
// ---------------------------------------------------------------------------------------

struct Device::Native {
    SDL_GPUDevice* device{nullptr};
    bool headless = false;
    /// Fences of submitted work not yet known to be finished, oldest first.
    std::deque<SDL_GPUFence*> in_flight;
};

struct Texture::Native {
    SDL_GPUDevice* device{nullptr};
    SDL_GPUTexture* texture{nullptr};
};

struct RenderTarget::Native {
    Device* owner{nullptr};
    SDL_GPUTexture* texture{nullptr};
};

namespace {

/// How many submissions may be unfinished on the GPU at once.
constexpr std::size_t max_submissions_in_flight = 3;

/// Submits `commands`, keeping at most max_submissions_in_flight submissions unfinished:
/// beyond that, it waits for the oldest. A window's swapchain paces frames, but a headless
/// run has nothing else to: without this, it queues work faster than the GPU finishes it,
/// and memory grows every frame until the driver fails (a software GPU crashed after about
/// 900 frames before this was added).
bool submit_paced(Device::Native& device, SDL_GPUCommandBuffer* commands) {
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (fence == nullptr) {
        return false;
    }
    device.in_flight.push_back(fence);
    while (device.in_flight.size() > max_submissions_in_flight) {
        SDL_GPUFence* oldest = device.in_flight.front();
        device.in_flight.pop_front();
        static_cast<void>(SDL_WaitForGPUFences(device.device, true, &oldest, 1));
        SDL_ReleaseGPUFence(device.device, oldest);
    }
    return true;
}

/// The pixel format of every texture and target: 8-bit RGBA, as pixel art is authored.
constexpr SDL_GPUTextureFormat pixel_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

[[nodiscard]] std::string sdl_error(const char* what) {
    return std::string(what) + ": " + SDL_GetError();
}

/// The byte size of an RGBA8 image, or 0 if it does not fit a 32-bit GPU transfer.
[[nodiscard]] std::uint32_t image_bytes(std::uint32_t width, std::uint32_t height) {
    const std::uint64_t bytes = std::uint64_t{width} * height * 4;
    return bytes > std::numeric_limits<std::uint32_t>::max() ? 0
                                                             : static_cast<std::uint32_t>(bytes);
}

// SDL reports an "invalid parameter" error when asked to release null, which would hide
// the real error; these release only what exists.
void release(SDL_GPUDevice* device, SDL_GPUTransferBuffer* buffer) {
    if (buffer != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, buffer);
    }
}
void release(SDL_GPUDevice* device, SDL_GPUBuffer* buffer) {
    if (buffer != nullptr) {
        SDL_ReleaseGPUBuffer(device, buffer);
    }
}
void release(SDL_GPUDevice* device, SDL_GPUShader* shader) {
    if (shader != nullptr) {
        SDL_ReleaseGPUShader(device, shader);
    }
}

/// One corner of a sprite, as the vertex shader reads it.
struct Vertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};
static_assert(sizeof(Vertex) == 32);

} // namespace

// ---------------------------------------------------------------------------------------
// Texture and RenderTarget
// ---------------------------------------------------------------------------------------

Texture::Texture(std::unique_ptr<Native> native, std::uint32_t width, std::uint32_t height)
    : native_(std::move(native)), width_(width), height_(height) {}

Texture::~Texture() {
    SDL_ReleaseGPUTexture(native_->device, native_->texture);
}

RenderTarget::RenderTarget(std::unique_ptr<Native> native, std::uint32_t width,
                           std::uint32_t height)
    : native_(std::move(native)), width_(width), height_(height) {}

RenderTarget::~RenderTarget() {
    SDL_ReleaseGPUTexture(native_->owner->native().device, native_->texture);
}

std::vector<std::uint8_t> RenderTarget::read_pixels() {
    Device& owner = *native_->owner;
    SDL_GPUDevice* device = owner.native().device;
    const std::uint32_t bytes = image_bytes(width_, height_);

    SDL_GPUTransferBufferCreateInfo buffer_info{};
    buffer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    buffer_info.size = bytes;
    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(device, &buffer_info);
    if (buffer == nullptr) {
        owner.last_error_ = sdl_error("Creating a download buffer failed");
        return {};
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        owner.last_error_ = sdl_error("Acquiring a command buffer failed");
        SDL_ReleaseGPUTransferBuffer(device, buffer);
        return {};
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureRegion region{};
    region.texture = native_->texture;
    region.w = width_;
    region.h = height_;
    region.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = buffer;
    SDL_DownloadFromGPUTexture(copy, &region, &destination);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    std::vector<std::uint8_t> pixels;
    if (fence == nullptr || !SDL_WaitForGPUFences(device, true, &fence, 1)) {
        owner.last_error_ = sdl_error("Waiting for the GPU failed");
    } else if (const auto* mapped = static_cast<const std::uint8_t*>(
                   SDL_MapGPUTransferBuffer(device, buffer, false))) {
        pixels.assign(mapped, mapped + bytes);
        SDL_UnmapGPUTransferBuffer(device, buffer);
    } else {
        owner.last_error_ = sdl_error("Reading the pixels failed");
    }
    if (fence != nullptr) {
        SDL_ReleaseGPUFence(device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(device, buffer);
    return pixels;
}

// ---------------------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------------------

Device::Device(std::unique_ptr<Native> native) : native_(std::move(native)) {}

Device::~Device() {
    for (SDL_GPUFence* fence : native_->in_flight) {
        static_cast<void>(SDL_WaitForGPUFences(native_->device, true, &fence, 1));
        SDL_ReleaseGPUFence(native_->device, fence);
    }
    native_->in_flight.clear();
    SDL_DestroyGPUDevice(native_->device);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

std::unique_ptr<Device> Device::create(const DeviceOptions& options, std::string& error) {
    // The GPU backends load their drivers through the video subsystem. Without a window,
    // the "offscreen" video driver provides that without a display. SDL counts
    // initialisations, so each device initialises and quits the subsystem once.
    if (options.headless && SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        error = sdl_error("Starting SDL video failed");
        return nullptr;
    }
    SDL_GPUDevice* device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL, options.debug, nullptr);
    if (device == nullptr) {
        error = sdl_error("No supported GPU was found");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    auto native = std::make_unique<Native>();
    native->device = device;
    native->headless = options.headless;
    return std::unique_ptr<Device>(new Device(std::move(native)));
}

std::string Device::driver_name() const {
    const char* name = SDL_GetGPUDeviceDriver(native_->device);
    return name == nullptr ? std::string() : std::string(name);
}

std::unique_ptr<Texture> Device::create_texture(std::uint32_t width, std::uint32_t height,
                                                std::span<const std::uint8_t> rgba) {
    const std::uint32_t bytes = image_bytes(width, height);
    if (width == 0 || height == 0 || bytes == 0 || rgba.size() != bytes) {
        last_error_ =
            "A texture needs a size above zero and exactly width x height x 4 bytes of pixels.";
        return nullptr;
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = pixel_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(native_->device, &info);
    if (texture == nullptr) {
        last_error_ = sdl_error("Creating a texture failed");
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo buffer_info{};
    buffer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    buffer_info.size = bytes;
    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(native_->device, &buffer_info);
    void* mapped =
        buffer == nullptr ? nullptr : SDL_MapGPUTransferBuffer(native_->device, buffer, false);
    SDL_GPUCommandBuffer* commands =
        mapped == nullptr ? nullptr : SDL_AcquireGPUCommandBuffer(native_->device);
    if (commands == nullptr) {
        last_error_ = sdl_error("Uploading a texture failed");
        if (mapped != nullptr) {
            SDL_UnmapGPUTransferBuffer(native_->device, buffer);
        }
        release(native_->device, buffer);
        SDL_ReleaseGPUTexture(native_->device, texture);
        return nullptr;
    }
    std::memcpy(mapped, rgba.data(), bytes);
    SDL_UnmapGPUTransferBuffer(native_->device, buffer);

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = buffer;
    SDL_GPUTextureRegion region{};
    region.texture = texture;
    region.w = width;
    region.h = height;
    region.d = 1;
    SDL_UploadToGPUTexture(copy, &source, &region, false);
    SDL_EndGPUCopyPass(copy);
    const bool submitted = submit_paced(*native_, commands);
    SDL_ReleaseGPUTransferBuffer(native_->device, buffer); // Freed once the GPU is done.
    if (!submitted) {
        last_error_ = sdl_error("Uploading a texture failed");
        SDL_ReleaseGPUTexture(native_->device, texture);
        return nullptr;
    }

    auto native = std::make_unique<Texture::Native>();
    native->device = native_->device;
    native->texture = texture;
    return std::unique_ptr<Texture>(new Texture(std::move(native), width, height));
}

std::unique_ptr<RenderTarget> Device::create_render_target(std::uint32_t width,
                                                           std::uint32_t height) {
    if (width == 0 || height == 0 || image_bytes(width, height) == 0) {
        last_error_ = "A render target needs a size above zero.";
        return nullptr;
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = pixel_format;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(native_->device, &info);
    if (texture == nullptr) {
        last_error_ = sdl_error("Creating a render target failed");
        return nullptr;
    }
    auto native = std::make_unique<RenderTarget::Native>();
    native->owner = this;
    native->texture = texture;
    return std::unique_ptr<RenderTarget>(new RenderTarget(std::move(native), width, height));
}

// ---------------------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------------------

Placement fit_integer(std::uint32_t image_width, std::uint32_t image_height,
                      std::uint32_t window_width, std::uint32_t window_height) noexcept {
    Placement placement;
    if (image_width == 0 || image_height == 0) {
        return placement;
    }
    placement.scale = std::max<std::uint32_t>(
        1, std::min(window_width / image_width, window_height / image_height));
    placement.width = std::uint64_t{image_width} * placement.scale;
    placement.height = std::uint64_t{image_height} * placement.scale;
    placement.x =
        (static_cast<std::int64_t>(window_width) - static_cast<std::int64_t>(placement.width)) / 2;
    placement.y =
        (static_cast<std::int64_t>(window_height) - static_cast<std::int64_t>(placement.height)) /
        2;
    return placement;
}

struct Window::Native {
    Device* owner{nullptr};
    SDL_Window* window{nullptr};
};

Window::Window(std::unique_ptr<Native> native) : native_(std::move(native)) {}

Window::~Window() {
    SDL_ReleaseWindowFromGPUDevice(native_->owner->native().device, native_->window);
    SDL_DestroyWindow(native_->window);
}

std::uint32_t Window::width() const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(native_->window, &w, &h);
    return static_cast<std::uint32_t>(std::max(w, 0));
}

std::uint32_t Window::height() const {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(native_->window, &w, &h);
    return static_cast<std::uint32_t>(std::max(h, 0));
}

bool Window::present(const RenderTarget& image, Color letterbox) {
    Device& owner = *native_->owner;
    SDL_GPUDevice* device = owner.native().device;
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (commands == nullptr) {
        owner.last_error_ = sdl_error("Acquiring a command buffer failed");
        return false;
    }
    SDL_GPUTexture* swapchain = nullptr;
    std::uint32_t swap_width = 0;
    std::uint32_t swap_height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, native_->window, &swapchain, &swap_width,
                                               &swap_height)) {
        owner.last_error_ = sdl_error("Acquiring the window's image failed");
        SDL_CancelGPUCommandBuffer(commands);
        return false;
    }
    if (swapchain != nullptr) {
        const Placement place = fit_integer(image.width(), image.height(), swap_width, swap_height);
        // The part of the image inside the window, in image pixels, and where it lands.
        const std::int64_t scale = place.scale;
        const std::int64_t skip_x = place.x < 0 ? (-place.x + scale - 1) / scale : 0;
        const std::int64_t skip_y = place.y < 0 ? (-place.y + scale - 1) / scale : 0;
        const std::int64_t shown_w = std::min<std::int64_t>(
            image.width() - skip_x, (swap_width - std::max<std::int64_t>(place.x, 0)) / scale);
        const std::int64_t shown_h = std::min<std::int64_t>(
            image.height() - skip_y, (swap_height - std::max<std::int64_t>(place.y, 0)) / scale);

        SDL_GPUBlitInfo blit{};
        blit.source.texture = image.native().texture;
        blit.source.x = static_cast<std::uint32_t>(skip_x);
        blit.source.y = static_cast<std::uint32_t>(skip_y);
        blit.source.w = static_cast<std::uint32_t>(std::max<std::int64_t>(shown_w, 0));
        blit.source.h = static_cast<std::uint32_t>(std::max<std::int64_t>(shown_h, 0));
        blit.destination.texture = swapchain;
        blit.destination.x = static_cast<std::uint32_t>(std::max<std::int64_t>(place.x, 0));
        blit.destination.y = static_cast<std::uint32_t>(std::max<std::int64_t>(place.y, 0));
        blit.destination.w = blit.source.w * place.scale;
        blit.destination.h = blit.source.h * place.scale;
        blit.load_op = SDL_GPU_LOADOP_CLEAR; // clears the whole window to the letterbox colour
        blit.clear_color = SDL_FColor{
            static_cast<float>(letterbox.r) / 255.0F, static_cast<float>(letterbox.g) / 255.0F,
            static_cast<float>(letterbox.b) / 255.0F, static_cast<float>(letterbox.a) / 255.0F};
        blit.filter = SDL_GPU_FILTER_NEAREST;
        if (blit.source.w > 0 && blit.source.h > 0) {
            SDL_BlitGPUTexture(commands, &blit);
        }
    }
    if (!submit_paced(owner.native(), commands)) {
        owner.last_error_ = sdl_error("Presenting failed");
        return false;
    }
    return true;
}

std::unique_ptr<Window> Device::create_window(std::string_view title, std::uint32_t width,
                                              std::uint32_t height) {
    if (width == 0 || height == 0) {
        last_error_ = "A window needs a size above zero.";
        return nullptr;
    }
    // SDL's offscreen video driver, used by headless devices, offers Vulkan surfaces only:
    // its windows have no native handle for a Direct3D 12 swapchain.
    if (native_->headless && driver_name() != "vulkan") {
        last_error_ = "A headless device can show windows only with Vulkan; this one uses " +
                      driver_name() + ".";
        return nullptr;
    }
    const std::string name(title);
    SDL_Window* window =
        SDL_CreateWindow(name.c_str(), static_cast<int>(width), static_cast<int>(height),
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        last_error_ = sdl_error("Creating a window failed");
        return nullptr;
    }
    if (!SDL_ClaimWindowForGPUDevice(native_->device, window)) {
        last_error_ = sdl_error("Connecting the window to the GPU failed");
        SDL_DestroyWindow(window);
        return nullptr;
    }
    auto native = std::make_unique<Window::Native>();
    native->owner = this;
    native->window = window;
    return std::unique_ptr<Window>(new Window(std::move(native)));
}

// ---------------------------------------------------------------------------------------
// SpriteRenderer
// ---------------------------------------------------------------------------------------

struct SpriteRenderer::Native {
    SDL_GPUDevice* device{nullptr};
    SDL_GPUGraphicsPipeline* pipeline{nullptr};
    SDL_GPUSampler* sampler{nullptr};
    SDL_GPUBuffer* vertex_buffer{nullptr};
    std::uint32_t vertex_capacity = 0; ///< In vertices.

    // The frame being recorded.
    RenderTarget* target{nullptr};
    Color clear;
    bool keep = false; ///< Draw over what the target holds instead of clearing it.
    std::vector<Vertex> vertices;
    std::vector<const Texture*> textures; ///< One per sprite.
};

SpriteRenderer::SpriteRenderer(Device& device, std::unique_ptr<Native> native)
    : device_(device), native_(std::move(native)) {}

SpriteRenderer::~SpriteRenderer() {
    release(native_->device, native_->vertex_buffer);
    SDL_ReleaseGPUSampler(native_->device, native_->sampler);
    SDL_ReleaseGPUGraphicsPipeline(native_->device, native_->pipeline);
}

std::unique_ptr<SpriteRenderer> SpriteRenderer::create(Device& device) {
    SDL_GPUDevice* gpu = device.native().device;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(gpu);
    const bool spirv = (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
    if (!spirv && (formats & SDL_GPU_SHADERFORMAT_DXIL) == 0) {
        device.last_error_ = "The GPU accepts neither SPIR-V nor DXIL shaders.";
        return nullptr;
    }

    SDL_GPUShaderCreateInfo vertex_info{};
    vertex_info.code = spirv ? sprite_vert_spirv.data() : sprite_vert_dxil.data();
    vertex_info.code_size = spirv ? sprite_vert_spirv.size() : sprite_vert_dxil.size();
    vertex_info.entrypoint = "main";
    vertex_info.format = spirv ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.num_uniform_buffers = 1;
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(gpu, &vertex_info);

    SDL_GPUShaderCreateInfo fragment_info{};
    fragment_info.code = spirv ? sprite_frag_spirv.data() : sprite_frag_dxil.data();
    fragment_info.code_size = spirv ? sprite_frag_spirv.size() : sprite_frag_dxil.size();
    fragment_info.entrypoint = "main";
    fragment_info.format = vertex_info.format;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.num_samplers = 1;
    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(gpu, &fragment_info);

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        device.last_error_ = sdl_error("Creating the sprite shaders failed");
    } else {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.slot = 0;
        buffer.pitch = sizeof(Vertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[3]{};
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0};
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8};
        attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16};

        SDL_GPUColorTargetDescription color{};
        color.format = pixel_format;
        color.blend_state.enable_blend = true;
        color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex_shader;
        info.fragment_shader = fragment_shader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 3;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
    }
    if (pipeline == nullptr && vertex_shader != nullptr && fragment_shader != nullptr) {
        device.last_error_ = sdl_error("Creating the sprite pipeline failed");
    }
    release(gpu, vertex_shader);
    release(gpu, fragment_shader);
    if (pipeline == nullptr) {
        return nullptr;
    }

    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(gpu, &sampler_info);
    if (sampler == nullptr) {
        device.last_error_ = sdl_error("Creating the sprite sampler failed");
        SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
        return nullptr;
    }

    auto native = std::make_unique<Native>();
    native->device = gpu;
    native->pipeline = pipeline;
    native->sampler = sampler;
    return std::unique_ptr<SpriteRenderer>(new SpriteRenderer(device, std::move(native)));
}

void SpriteRenderer::begin(RenderTarget& target, Color clear) {
    native_->target = &target;
    native_->clear = clear;
    native_->keep = false;
    native_->vertices.clear();
    native_->textures.clear();
}

void SpriteRenderer::begin_over(RenderTarget& target) {
    begin(target, Color{});
    native_->keep = true;
}

void SpriteRenderer::draw(const Texture& texture, Rect source, Rect destination, Color tint) {
    if (native_->target == nullptr) {
        return;
    }
    const auto tex_w = static_cast<float>(texture.width());
    const auto tex_h = static_cast<float>(texture.height());
    const float u0 = source.x / tex_w;
    const float v0 = source.y / tex_h;
    const float u1 = (source.x + source.w) / tex_w;
    const float v1 = (source.y + source.h) / tex_h;
    const float x0 = destination.x;
    const float y0 = destination.y;
    const float x1 = destination.x + destination.w;
    const float y1 = destination.y + destination.h;
    const float r = static_cast<float>(tint.r) / 255.0F;
    const float g = static_cast<float>(tint.g) / 255.0F;
    const float b = static_cast<float>(tint.b) / 255.0F;
    const float a = static_cast<float>(tint.a) / 255.0F;
    const Vertex corners[6] = {
        {x0, y0, u0, v0, r, g, b, a}, {x1, y0, u1, v0, r, g, b, a}, {x0, y1, u0, v1, r, g, b, a},
        {x1, y0, u1, v0, r, g, b, a}, {x1, y1, u1, v1, r, g, b, a}, {x0, y1, u0, v1, r, g, b, a},
    };
    native_->vertices.insert(native_->vertices.end(), std::begin(corners), std::end(corners));
    native_->textures.push_back(&texture);
}

bool SpriteRenderer::end() {
    Native& n = *native_;
    RenderTarget* target = n.target;
    n.target = nullptr;
    if (target == nullptr) {
        return false;
    }
    stats_ = Stats{};
    const auto vertex_count = static_cast<std::uint32_t>(n.vertices.size());
    const std::uint32_t bytes = vertex_count * static_cast<std::uint32_t>(sizeof(Vertex));

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(n.device);
    if (commands == nullptr) {
        device_.last_error_ = sdl_error("Acquiring a command buffer failed");
        return false;
    }

    SDL_GPUTransferBuffer* upload = nullptr;
    if (vertex_count > 0) {
        if (vertex_count > n.vertex_capacity) {
            release(n.device, n.vertex_buffer);
            SDL_GPUBufferCreateInfo buffer_info{};
            buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            buffer_info.size = bytes;
            n.vertex_buffer = SDL_CreateGPUBuffer(n.device, &buffer_info);
            n.vertex_capacity = n.vertex_buffer == nullptr ? 0 : vertex_count;
        }
        SDL_GPUTransferBufferCreateInfo upload_info{};
        upload_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        upload_info.size = bytes;
        upload = n.vertex_buffer == nullptr ? nullptr
                                            : SDL_CreateGPUTransferBuffer(n.device, &upload_info);
        void* mapped =
            upload == nullptr ? nullptr : SDL_MapGPUTransferBuffer(n.device, upload, false);
        if (mapped == nullptr) {
            device_.last_error_ = sdl_error("Uploading sprites failed");
            release(n.device, upload);
            SDL_CancelGPUCommandBuffer(commands);
            return false;
        }
        std::memcpy(mapped, n.vertices.data(), bytes);
        SDL_UnmapGPUTransferBuffer(n.device, upload);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTransferBufferLocation from{};
        from.transfer_buffer = upload;
        SDL_GPUBufferRegion to{};
        to.buffer = n.vertex_buffer;
        to.size = bytes;
        // Cycling gives the upload fresh memory if the previous frame still reads the buffer.
        SDL_UploadToGPUBuffer(copy, &from, &to, true);
        SDL_EndGPUCopyPass(copy);
    }

    SDL_GPUColorTargetInfo color{};
    color.texture = target->native().texture;
    color.clear_color =
        SDL_FColor{static_cast<float>(n.clear.r) / 255.0F, static_cast<float>(n.clear.g) / 255.0F,
                   static_cast<float>(n.clear.b) / 255.0F, static_cast<float>(n.clear.a) / 255.0F};
    color.load_op = n.keep ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
    if (vertex_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, n.pipeline);
        SDL_GPUBufferBinding binding{};
        binding.buffer = n.vertex_buffer;
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        // Pixels to clip space: x from [0, width] to [-1, 1], y from [0, height] to [1, -1].
        const float transform[4] = {2.0F / static_cast<float>(target->width()),
                                    -2.0F / static_cast<float>(target->height()), -1.0F, 1.0F};
        SDL_PushGPUVertexUniformData(commands, 0, transform, sizeof(transform));
        // One draw per run of sprites sharing a texture, in the order they were queued.
        std::size_t first = 0;
        while (first < n.textures.size()) {
            std::size_t last = first + 1;
            while (last < n.textures.size() && n.textures[last] == n.textures[first]) {
                ++last;
            }
            SDL_GPUTextureSamplerBinding sampler{};
            sampler.texture = n.textures[first]->native().texture;
            sampler.sampler = n.sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
            SDL_DrawGPUPrimitives(pass, static_cast<std::uint32_t>((last - first) * 6), 1,
                                  static_cast<std::uint32_t>(first * 6), 0);
            ++stats_.draw_calls;
            first = last;
        }
    }
    SDL_EndGPURenderPass(pass);
    const bool submitted = submit_paced(device_.native(), commands);
    release(n.device, upload);
    if (!submitted) {
        device_.last_error_ = sdl_error("Submitting the frame failed");
        return false;
    }
    stats_.sprites = n.textures.size();
    return true;
}

// ---------------------------------------------------------------------------------------
// Mat4
// ---------------------------------------------------------------------------------------

namespace {

[[nodiscard]] Vec3 subtract(Vec3 a, Vec3 b) noexcept {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return Vec3{(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}
[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
[[nodiscard]] Vec3 normalize(Vec3 v) noexcept {
    const float length = std::sqrt(dot(v, v));
    return length > 0.0F ? Vec3{v.x / length, v.y / length, v.z / length} : v;
}

} // namespace

Mat4 Mat4::perspective(float fov_y, float aspect, float near, float far) {
    const float f = 1.0F / std::tan(fov_y / 2.0F);
    Mat4 r;
    r.m = {f / aspect, 0, 0,  0, 0, f, 0, 0, 0, 0, far / (near - far), (near * far) / (near - far),
           0,          0, -1, 0};
    return r;
}

Mat4 Mat4::orthographic(float left, float right, float bottom, float top, float near, float far) {
    Mat4 r;
    r.m = {2.0F / (right - left),
           0,
           0,
           -(right + left) / (right - left),
           0,
           2.0F / (top - bottom),
           0,
           -(top + bottom) / (top - bottom),
           0,
           0,
           1.0F / (near - far),
           near / (near - far),
           0,
           0,
           0,
           1};
    return r;
}

Mat4 Mat4::look_at(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(subtract(target, eye));
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 r;
    r.m = {s.x,  s.y,  s.z,  -dot(s, eye), u.x, u.y, u.z, -dot(u, eye),
           -f.x, -f.y, -f.z, dot(f, eye),  0,   0,   0,   1};
    return r;
}

std::array<float, 4> Mat4::transform(Vec3 p) const noexcept {
    std::array<float, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        out[row] = (m[(row * 4) + 0] * p.x) + (m[(row * 4) + 1] * p.y) + (m[(row * 4) + 2] * p.z) +
                   m[(row * 4) + 3];
    }
    return out;
}

Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
    Mat4 r;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += a.m[(row * 4) + k] * b.m[(k * 4) + column];
            }
            r.m[(row * 4) + column] = sum;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------------------
// WorldRenderer
// ---------------------------------------------------------------------------------------

namespace {

/// One corner of a world quad, as the vertex shader reads it.
struct WorldVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
    float nx; ///< The surface's normal and wrap, for point lights.
    float ny;
    float nz;
    float wrap;
    float albedo_r;
    float albedo_g;
    float albedo_b;
};

} // namespace

struct WorldRenderer::Native {
    SDL_GPUDevice* device{nullptr};
    SDL_GPUGraphicsPipeline* pipeline{nullptr};
    SDL_GPUSampler* sampler{nullptr};
    SDL_GPUTextureFormat depth_format{SDL_GPU_TEXTUREFORMAT_D16_UNORM};
    SDL_GPUTexture* depth{nullptr};
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
    SDL_GPUBuffer* vertex_buffer{nullptr};
    std::uint32_t vertex_capacity = 0; ///< In vertices.

    // The frame being recorded.
    RenderTarget* target{nullptr};
    Color clear;
    Mat4 view_projection;
    Vec3 eye;
    Fog fog;
    std::vector<PointLight> lights;
    std::vector<WorldVertex> vertices;
    std::vector<const Texture*> textures; ///< One per quad.
};

WorldRenderer::WorldRenderer(Device& device, std::unique_ptr<Native> native)
    : device_(device), native_(std::move(native)) {}

WorldRenderer::~WorldRenderer() {
    release(native_->device, native_->vertex_buffer);
    if (native_->depth != nullptr) {
        SDL_ReleaseGPUTexture(native_->device, native_->depth);
    }
    SDL_ReleaseGPUSampler(native_->device, native_->sampler);
    SDL_ReleaseGPUGraphicsPipeline(native_->device, native_->pipeline);
}

std::unique_ptr<WorldRenderer> WorldRenderer::create(Device& device) {
    SDL_GPUDevice* gpu = device.native().device;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(gpu);
    const bool spirv = (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
    if (!spirv && (formats & SDL_GPU_SHADERFORMAT_DXIL) == 0) {
        device.last_error_ = "The GPU accepts neither SPIR-V nor DXIL shaders.";
        return nullptr;
    }
    // The most precise depth format the GPU can render to.
    SDL_GPUTextureFormat depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    for (const SDL_GPUTextureFormat candidate :
         {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM,
          SDL_GPU_TEXTUREFORMAT_D16_UNORM}) {
        if (SDL_GPUTextureSupportsFormat(gpu, candidate, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            depth_format = candidate;
            break;
        }
    }
    if (depth_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        device.last_error_ = "The GPU has no depth buffer format.";
        return nullptr;
    }

    SDL_GPUShaderCreateInfo vertex_info{};
    vertex_info.code = spirv ? world_vert_spirv.data() : world_vert_dxil.data();
    vertex_info.code_size = spirv ? world_vert_spirv.size() : world_vert_dxil.size();
    vertex_info.entrypoint = "main";
    vertex_info.format = spirv ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vertex_info.num_uniform_buffers = 1;
    SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(gpu, &vertex_info);

    SDL_GPUShaderCreateInfo fragment_info{};
    fragment_info.code = spirv ? world_frag_spirv.data() : world_frag_dxil.data();
    fragment_info.code_size = spirv ? world_frag_spirv.size() : world_frag_dxil.size();
    fragment_info.entrypoint = "main";
    fragment_info.format = vertex_info.format;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.num_samplers = 1;
    fragment_info.num_uniform_buffers = 1;
    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(gpu, &fragment_info);

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        device.last_error_ = sdl_error("Creating the world shaders failed");
    } else {
        SDL_GPUVertexBufferDescription buffer{};
        buffer.slot = 0;
        buffer.pitch = sizeof(WorldVertex);
        buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute attributes[5]{};
        attributes[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0};
        attributes[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 12};
        attributes[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 20};
        attributes[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 36};
        attributes[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 52};

        SDL_GPUColorTargetDescription color{};
        color.format = pixel_format;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex_shader;
        info.fragment_shader = fragment_shader;
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 5;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = true;
        info.target_info.depth_stencil_format = depth_format;
        pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &info);
    }
    if (pipeline == nullptr && vertex_shader != nullptr && fragment_shader != nullptr) {
        device.last_error_ = sdl_error("Creating the world pipeline failed");
    }
    release(gpu, vertex_shader);
    release(gpu, fragment_shader);
    if (pipeline == nullptr) {
        return nullptr;
    }

    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(gpu, &sampler_info);
    if (sampler == nullptr) {
        device.last_error_ = sdl_error("Creating the world sampler failed");
        SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
        return nullptr;
    }

    auto native = std::make_unique<Native>();
    native->device = gpu;
    native->pipeline = pipeline;
    native->sampler = sampler;
    native->depth_format = depth_format;
    return std::unique_ptr<WorldRenderer>(new WorldRenderer(device, std::move(native)));
}

float light_falloff(float distance, float radius) noexcept {
    if (!(radius > 0.0F) || !std::isfinite(distance) || !std::isfinite(radius) ||
        distance >= radius) {
        return 0.0F;
    }
    const float x = distance / radius;
    const float near = 1.0F - (x * x);
    return near * near;
}

void WorldRenderer::begin(RenderTarget& target, Color clear, const Mat4& view_projection, Vec3 eye,
                          Fog fog, std::span<const PointLight> lights) {
    native_->target = &target;
    native_->clear = clear;
    native_->view_projection = view_projection;
    native_->eye = eye;
    native_->fog = fog;
    native_->lights.assign(lights.begin(), lights.begin() + static_cast<std::ptrdiff_t>(std::min(
                                                                lights.size(), max_point_lights)));
    native_->vertices.clear();
    native_->textures.clear();
}

void WorldRenderer::draw(const Texture& texture, const std::array<Vec3, 4>& corners, Rect source,
                         std::array<float, 4> color, const Surface& surface) {
    if (native_->target == nullptr) {
        return;
    }
    const auto tex_w = static_cast<float>(texture.width());
    const auto tex_h = static_cast<float>(texture.height());
    // In perspective, a texture coordinate exactly on a texel edge can round into the
    // neighbouring texel, so the next tile of an atlas bleeds in along tile seams. A
    // hundredth of a texel inside the edges cannot show, and keeps sampling in the source.
    constexpr float inset = 0.01F;
    const float u0 = (source.x + inset) / tex_w;
    const float v0 = (source.y + inset) / tex_h;
    const float u1 = (source.x + source.w - inset) / tex_w;
    const float v1 = (source.y + source.h - inset) / tex_h;
    const auto corner = [&](const Vec3& p, float u, float v) {
        return WorldVertex{p.x,
                           p.y,
                           p.z,
                           u,
                           v,
                           color[0],
                           color[1],
                           color[2],
                           color[3],
                           surface.normal.x,
                           surface.normal.y,
                           surface.normal.z,
                           surface.wrap,
                           surface.albedo[0],
                           surface.albedo[1],
                           surface.albedo[2]};
    };
    const WorldVertex tl = corner(corners[0], u0, v0);
    const WorldVertex tr = corner(corners[1], u1, v0);
    const WorldVertex br = corner(corners[2], u1, v1);
    const WorldVertex bl = corner(corners[3], u0, v1);
    const WorldVertex quad[6] = {tl, tr, bl, tr, br, bl};
    native_->vertices.insert(native_->vertices.end(), std::begin(quad), std::end(quad));
    native_->textures.push_back(&texture);
}

bool WorldRenderer::end() {
    Native& n = *native_;
    RenderTarget* target = n.target;
    n.target = nullptr;
    if (target == nullptr) {
        return false;
    }
    stats_ = Stats{};
    // The depth buffer matches the target's size.
    if (n.depth == nullptr || n.depth_width != target->width() ||
        n.depth_height != target->height()) {
        if (n.depth != nullptr) {
            SDL_ReleaseGPUTexture(n.device, n.depth);
        }
        SDL_GPUTextureCreateInfo depth_info{};
        depth_info.type = SDL_GPU_TEXTURETYPE_2D;
        depth_info.format = n.depth_format;
        depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depth_info.width = target->width();
        depth_info.height = target->height();
        depth_info.layer_count_or_depth = 1;
        depth_info.num_levels = 1;
        n.depth = SDL_CreateGPUTexture(n.device, &depth_info);
        n.depth_width = n.depth == nullptr ? 0 : target->width();
        n.depth_height = n.depth == nullptr ? 0 : target->height();
        if (n.depth == nullptr) {
            device_.last_error_ = sdl_error("Creating the depth buffer failed");
            return false;
        }
    }

    const auto vertex_count = static_cast<std::uint32_t>(n.vertices.size());
    const std::uint32_t bytes = vertex_count * static_cast<std::uint32_t>(sizeof(WorldVertex));
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(n.device);
    if (commands == nullptr) {
        device_.last_error_ = sdl_error("Acquiring a command buffer failed");
        return false;
    }
    SDL_GPUTransferBuffer* upload = nullptr;
    if (vertex_count > 0) {
        if (vertex_count > n.vertex_capacity) {
            release(n.device, n.vertex_buffer);
            SDL_GPUBufferCreateInfo buffer_info{};
            buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            buffer_info.size = bytes;
            n.vertex_buffer = SDL_CreateGPUBuffer(n.device, &buffer_info);
            n.vertex_capacity = n.vertex_buffer == nullptr ? 0 : vertex_count;
        }
        SDL_GPUTransferBufferCreateInfo upload_info{};
        upload_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        upload_info.size = bytes;
        upload = n.vertex_buffer == nullptr ? nullptr
                                            : SDL_CreateGPUTransferBuffer(n.device, &upload_info);
        void* mapped =
            upload == nullptr ? nullptr : SDL_MapGPUTransferBuffer(n.device, upload, false);
        if (mapped == nullptr) {
            device_.last_error_ = sdl_error("Uploading the world failed");
            release(n.device, upload);
            SDL_CancelGPUCommandBuffer(commands);
            return false;
        }
        std::memcpy(mapped, n.vertices.data(), bytes);
        SDL_UnmapGPUTransferBuffer(n.device, upload);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTransferBufferLocation from{};
        from.transfer_buffer = upload;
        SDL_GPUBufferRegion to{};
        to.buffer = n.vertex_buffer;
        to.size = bytes;
        SDL_UploadToGPUBuffer(copy, &from, &to, true);
        SDL_EndGPUCopyPass(copy);
    }

    SDL_GPUColorTargetInfo color{};
    color.texture = target->native().texture;
    color.clear_color =
        SDL_FColor{static_cast<float>(n.clear.r) / 255.0F, static_cast<float>(n.clear.g) / 255.0F,
                   static_cast<float>(n.clear.b) / 255.0F, static_cast<float>(n.clear.a) / 255.0F};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = n.depth;
    depth.clear_depth = 1.0F;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, &depth);
    if (vertex_count > 0) {
        SDL_BindGPUGraphicsPipeline(pass, n.pipeline);
        SDL_GPUBufferBinding binding{};
        binding.buffer = n.vertex_buffer;
        SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
        std::array<float, 20> camera{};
        std::copy(n.view_projection.m.begin(), n.view_projection.m.end(), camera.begin());
        camera[16] = n.eye.x;
        camera[17] = n.eye.y;
        camera[18] = n.eye.z;
        SDL_PushGPUVertexUniformData(commands, 0, camera.data(), sizeof(camera));
        // The pixel shader's uniforms: fog, then the point lights (see world.frag.hlsl).
        const bool fog_on = n.fog.end > n.fog.start;
        std::array<float, 4 * (3 + (2 * max_point_lights))> frame{};
        frame[0] = static_cast<float>(n.fog.color.r) / 255.0F;
        frame[1] = static_cast<float>(n.fog.color.g) / 255.0F;
        frame[2] = static_cast<float>(n.fog.color.b) / 255.0F;
        frame[3] = 1.0F;
        frame[4] = n.fog.start;
        frame[5] = fog_on ? 1.0F / (n.fog.end - n.fog.start) : 0.0F;
        frame[6] = fog_on ? 1.0F : 0.0F;
        std::size_t count = 0;
        for (const PointLight& light : n.lights) {
            const bool usable = light.radius > 0.0F && std::isfinite(light.radius) &&
                                std::isfinite(light.position.x) &&
                                std::isfinite(light.position.y) &&
                                std::isfinite(light.position.z) &&
                                std::all_of(light.color.begin(), light.color.end(),
                                            [](float c) { return std::isfinite(c); });
            if (!usable) {
                continue;
            }
            const std::size_t position = 12 + (4 * count);
            const std::size_t colour = 12 + (4 * max_point_lights) + (4 * count);
            frame.at(position) = light.position.x;
            frame.at(position + 1) = light.position.y;
            frame.at(position + 2) = light.position.z;
            frame.at(position + 3) = light.radius;
            frame.at(colour) = light.color[0];
            frame.at(colour + 1) = light.color[1];
            frame.at(colour + 2) = light.color[2];
            ++count;
        }
        frame[8] = static_cast<float>(count);
        SDL_PushGPUFragmentUniformData(commands, 0, frame.data(), sizeof(frame));
        std::size_t first = 0;
        while (first < n.textures.size()) {
            std::size_t last = first + 1;
            while (last < n.textures.size() && n.textures[last] == n.textures[first]) {
                ++last;
            }
            SDL_GPUTextureSamplerBinding sampler{};
            sampler.texture = n.textures[first]->native().texture;
            sampler.sampler = n.sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, &sampler, 1);
            SDL_DrawGPUPrimitives(pass, static_cast<std::uint32_t>((last - first) * 6), 1,
                                  static_cast<std::uint32_t>(first * 6), 0);
            ++stats_.draw_calls;
            first = last;
        }
    }
    SDL_EndGPURenderPass(pass);
    const bool submitted = submit_paced(device_.native(), commands);
    release(n.device, upload);
    if (!submitted) {
        device_.last_error_ = sdl_error("Submitting the frame failed");
        return false;
    }
    stats_.quads = n.textures.size();
    return true;
}

} // namespace engine::render
