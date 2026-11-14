#include "engine/render/render.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>

#include "post_blur_frag_dxil.h"
#include "post_blur_frag_spirv.h"
#include "post_final_frag_dxil.h"
#include "post_final_frag_spirv.h"
#include "post_vert_dxil.h"
#include "post_vert_spirv.h"
#include "shadow_frag_dxil.h"
#include "shadow_frag_spirv.h"
#include "sprite_frag_dxil.h"
#include "sprite_frag_spirv.h"
#include "sprite_vert_dxil.h"
#include "sprite_vert_spirv.h"
#include "sun_depth_frag_dxil.h"
#include "sun_depth_frag_spirv.h"
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
    auto made = std::unique_ptr<Texture>(new Texture(std::move(native), width, height));
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] != 0 && rgba[i] != 255) {
            made->translucent_ = true;
            break;
        }
    }
    return made;
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
    SDL_GPUGraphicsPipeline* light_pipeline{nullptr}; ///< Blend::Light.
    SDL_GPUSampler* sampler{nullptr};
    SDL_GPUBuffer* vertex_buffer{nullptr};
    std::uint32_t vertex_capacity = 0; ///< In vertices.

    // The frame being recorded.
    RenderTarget* target{nullptr};
    Color clear;
    bool keep = false; ///< Draw over what the target holds instead of clearing it.
    std::vector<Vertex> vertices;
    std::vector<const Texture*> textures; ///< One per sprite.
    std::vector<Blend> blends;            ///< One per sprite.
    Blend blend = Blend::Normal;
};

SpriteRenderer::SpriteRenderer(Device& device, std::unique_ptr<Native> native)
    : device_(device), native_(std::move(native)) {}

SpriteRenderer::~SpriteRenderer() {
    release(native_->device, native_->vertex_buffer);
    SDL_ReleaseGPUSampler(native_->device, native_->sampler);
    SDL_ReleaseGPUGraphicsPipeline(native_->device, native_->pipeline);
    SDL_ReleaseGPUGraphicsPipeline(native_->device, native_->light_pipeline);
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
    SDL_GPUGraphicsPipeline* light_pipeline = nullptr;
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
        // Light: what is there, times one plus the sprite's colour; alpha kept.
        SDL_GPUColorTargetDescription lit = color;
        lit.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
        lit.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        lit.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        lit.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        info.target_info.color_target_descriptions = &lit;
        light_pipeline = pipeline == nullptr ? nullptr : SDL_CreateGPUGraphicsPipeline(gpu, &info);
    }
    if ((pipeline == nullptr || light_pipeline == nullptr) && vertex_shader != nullptr &&
        fragment_shader != nullptr) {
        device.last_error_ = sdl_error("Creating the sprite pipeline failed");
    }
    release(gpu, vertex_shader);
    release(gpu, fragment_shader);
    if (pipeline == nullptr || light_pipeline == nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
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
        SDL_ReleaseGPUGraphicsPipeline(gpu, light_pipeline);
        return nullptr;
    }

    auto native = std::make_unique<Native>();
    native->device = gpu;
    native->pipeline = pipeline;
    native->light_pipeline = light_pipeline;
    native->sampler = sampler;
    return std::unique_ptr<SpriteRenderer>(new SpriteRenderer(device, std::move(native)));
}

void SpriteRenderer::begin(RenderTarget& target, Color clear) {
    native_->target = &target;
    native_->clear = clear;
    native_->keep = false;
    native_->vertices.clear();
    native_->textures.clear();
    native_->blends.clear();
    native_->blend = Blend::Normal;
}

void SpriteRenderer::set_blend(Blend blend) {
    native_->blend = blend;
}

void SpriteRenderer::begin_over(RenderTarget& target) {
    begin(target, Color{});
    native_->keep = true;
}

void SpriteRenderer::draw(const Texture& texture, Rect source, Rect destination, Color tint) {
    const float x0 = destination.x;
    const float y0 = destination.y;
    const float x1 = destination.x + destination.w;
    const float y1 = destination.y + destination.h;
    draw(texture, source, {Point{x0, y0}, Point{x1, y0}, Point{x1, y1}, Point{x0, y1}}, tint);
}

void SpriteRenderer::draw(const Texture& texture, Rect source, const std::array<Point, 4>& corners,
                          Color tint) {
    if (native_->target == nullptr) {
        return;
    }
    const auto tex_w = static_cast<float>(texture.width());
    const auto tex_h = static_cast<float>(texture.height());
    const float u0 = source.x / tex_w;
    const float v0 = source.y / tex_h;
    const float u1 = (source.x + source.w) / tex_w;
    const float v1 = (source.y + source.h) / tex_h;
    const float r = static_cast<float>(tint.r) / 255.0F;
    const float g = static_cast<float>(tint.g) / 255.0F;
    const float b = static_cast<float>(tint.b) / 255.0F;
    const float a = static_cast<float>(tint.a) / 255.0F;
    const auto& [tl, tr, br, bl] = corners;
    // Two triangles: top-left, top-right, bottom-left; top-right, bottom-right, bottom-left.
    const Vertex vertices[6] = {
        {tl.x, tl.y, u0, v0, r, g, b, a}, {tr.x, tr.y, u1, v0, r, g, b, a},
        {bl.x, bl.y, u0, v1, r, g, b, a}, {tr.x, tr.y, u1, v0, r, g, b, a},
        {br.x, br.y, u1, v1, r, g, b, a}, {bl.x, bl.y, u0, v1, r, g, b, a},
    };
    native_->vertices.insert(native_->vertices.end(), std::begin(vertices), std::end(vertices));
    native_->textures.push_back(&texture);
    native_->blends.push_back(native_->blend);
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
        // One draw per run of sprites sharing a texture and a blend, in the order they
        // were queued.
        Blend bound = Blend::Normal;
        std::size_t first = 0;
        while (first < n.textures.size()) {
            std::size_t last = first + 1;
            while (last < n.textures.size() && n.textures[last] == n.textures[first] &&
                   n.blends[last] == n.blends[first]) {
                ++last;
            }
            if (n.blends[first] != bound) {
                bound = n.blends[first];
                SDL_BindGPUGraphicsPipeline(pass,
                                            bound == Blend::Light ? n.light_pipeline : n.pipeline);
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
    // Shadow decals, drawn after everything else (shadow.frag.hlsl).
    SDL_GPUGraphicsPipeline* shadow_pipeline{nullptr};
    std::unique_ptr<Texture> shadow_mask; ///< A soft round blob.
    std::vector<WorldVertex> shadows;
    std::vector<const Texture*> shadow_shapes; ///< Per shadow; the blob is shadow_mask.
    // See-through quads, drawn last, blended, farthest first (world.frag.hlsl in its
    // blended mode).
    SDL_GPUGraphicsPipeline* blend_pipeline{nullptr};
    struct Translucent {
        std::array<WorldVertex, 6> vertices;
        const Texture* texture;
        float distance; ///< Squared, from the eye to the quad's middle.
    };
    std::vector<Translucent> translucent;
    // The sun's shadows: a depth pass from the sun (sun_depth.frag.hlsl) into its map.
    SDL_GPUGraphicsPipeline* sun_pipeline{nullptr};
    SDL_GPUTexture* sun_map{nullptr};
    std::vector<bool> casts; ///< Per opaque quad.
    std::optional<SunShadow> sun;
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
    if (native_->shadow_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(native_->device, native_->shadow_pipeline);
    }
    if (native_->blend_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(native_->device, native_->blend_pipeline);
    }
    if (native_->sun_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(native_->device, native_->sun_pipeline);
    }
    if (native_->sun_map != nullptr) {
        SDL_ReleaseGPUTexture(native_->device, native_->sun_map);
    }
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
    fragment_info.num_samplers = 2; // the quad's texture, the sun's shadow map
    fragment_info.num_uniform_buffers = 1;
    SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(gpu, &fragment_info);

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUGraphicsPipeline* shadow_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* blend_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* sun_pipeline = nullptr;
    // The shadow map: a depth format the GPU can both render to and sample.
    SDL_GPUTextureFormat sun_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    for (const SDL_GPUTextureFormat candidate :
         {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D16_UNORM,
          SDL_GPU_TEXTUREFORMAT_D24_UNORM}) {
        if (SDL_GPUTextureSupportsFormat(gpu, candidate, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                             SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
            sun_format = candidate;
            break;
        }
    }
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

        // Shadows: the same vertices, a pixel shader that darkens, multiplying the target;
        // tested against depth but not written, so they lie on what is already drawn.
        SDL_GPUShaderCreateInfo shadow_info = fragment_info;
        shadow_info.code = spirv ? shadow_frag_spirv.data() : shadow_frag_dxil.data();
        shadow_info.code_size = spirv ? shadow_frag_spirv.size() : shadow_frag_dxil.size();
        shadow_info.num_samplers = 1;
        shadow_info.num_uniform_buffers = 0;
        SDL_GPUShader* shadow_shader = SDL_CreateGPUShader(gpu, &shadow_info);
        if (shadow_shader != nullptr && pipeline != nullptr) {
            SDL_GPUColorTargetDescription multiply = color;
            multiply.blend_state.enable_blend = true;
            multiply.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            multiply.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
            multiply.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            multiply.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            multiply.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            multiply.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            SDL_GPUGraphicsPipelineCreateInfo shadow = info;
            shadow.fragment_shader = shadow_shader;
            shadow.target_info.color_target_descriptions = &multiply;
            shadow.depth_stencil_state.enable_depth_write = false;
            shadow.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            shadow_pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &shadow);
        }
        release(gpu, shadow_shader);
        // See-through quads: the same shaders, blended over what is behind, tested
        // against depth but not written, so they never hide what is drawn after them.
        if (pipeline != nullptr && shadow_pipeline != nullptr) {
            SDL_GPUColorTargetDescription over = color;
            over.blend_state.enable_blend = true;
            over.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            over.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            over.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            over.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            over.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            over.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            SDL_GPUGraphicsPipelineCreateInfo blend = info;
            blend.target_info.color_target_descriptions = &over;
            blend.depth_stencil_state.enable_depth_write = false;
            blend_pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &blend);
        }
        // The sun's depth pass: the world's vertex shader seeing from the sun, writing
        // depth only, into the shadow map's format.
        SDL_GPUShaderCreateInfo sun_info = fragment_info;
        sun_info.code = spirv ? sun_depth_frag_spirv.data() : sun_depth_frag_dxil.data();
        sun_info.code_size = spirv ? sun_depth_frag_spirv.size() : sun_depth_frag_dxil.size();
        sun_info.num_samplers = 1;
        sun_info.num_uniform_buffers = 0;
        SDL_GPUShader* sun_shader = sun_format != SDL_GPU_TEXTUREFORMAT_INVALID
                                        ? SDL_CreateGPUShader(gpu, &sun_info)
                                        : nullptr;
        if (sun_shader != nullptr && pipeline != nullptr) {
            SDL_GPUGraphicsPipelineCreateInfo sun = info;
            sun.fragment_shader = sun_shader;
            sun.target_info.num_color_targets = 0;
            sun.target_info.color_target_descriptions = nullptr;
            sun.target_info.depth_stencil_format = sun_format;
            sun.rasterizer_state.enable_depth_bias = true;
            sun.rasterizer_state.depth_bias_slope_factor = 1.5F;
            sun.rasterizer_state.depth_bias_constant_factor = 1.0F;
            sun_pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &sun);
        }
        release(gpu, sun_shader);
        if ((shadow_pipeline == nullptr || blend_pipeline == nullptr || sun_pipeline == nullptr) &&
            pipeline != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
            pipeline = nullptr;
            if (shadow_pipeline != nullptr) {
                SDL_ReleaseGPUGraphicsPipeline(gpu, shadow_pipeline);
                shadow_pipeline = nullptr;
            }
            if (blend_pipeline != nullptr) {
                SDL_ReleaseGPUGraphicsPipeline(gpu, blend_pipeline);
                blend_pipeline = nullptr;
            }
            if (sun_pipeline != nullptr) {
                SDL_ReleaseGPUGraphicsPipeline(gpu, sun_pipeline);
                sun_pipeline = nullptr;
            }
        }
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
    // The shadow blob: opaque in the middle, fading out to the rim of a circle.
    constexpr std::uint32_t mask_size = 32;
    std::vector<std::uint8_t> mask(std::size_t{mask_size} * mask_size * 4, 255);
    for (std::uint32_t y = 0; y < mask_size; ++y) {
        for (std::uint32_t x = 0; x < mask_size; ++x) {
            const float dx = ((static_cast<float>(x) + 0.5F) / mask_size * 2.0F) - 1.0F;
            const float dy = ((static_cast<float>(y) + 0.5F) / mask_size * 2.0F) - 1.0F;
            const float r = std::sqrt((dx * dx) + (dy * dy));
            const float a = std::clamp((1.0F - r) / 0.4F, 0.0F, 1.0F); // soft outer 40 %
            mask[(((std::size_t{y} * mask_size) + x) * 4) + 3] =
                static_cast<std::uint8_t>(std::lround(a * 255.0F));
        }
    }
    std::unique_ptr<Texture> shadow_mask = device.create_texture(mask_size, mask_size, mask);
    if (sampler == nullptr || shadow_mask == nullptr) {
        if (sampler == nullptr) {
            device.last_error_ = sdl_error("Creating the world sampler failed");
        } else {
            SDL_ReleaseGPUSampler(gpu, sampler);
        }
        SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu, shadow_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu, blend_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu, sun_pipeline);
        return nullptr;
    }

    // The shadow map, cleared to "nothing in the way" once, so it is valid to sample in
    // frames without the sun's shadows.
    SDL_GPUTextureCreateInfo map_info{};
    map_info.type = SDL_GPU_TEXTURETYPE_2D;
    map_info.format = sun_format;
    map_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    map_info.width = sun_shadow_size;
    map_info.height = sun_shadow_size;
    map_info.layer_count_or_depth = 1;
    map_info.num_levels = 1;
    SDL_GPUTexture* sun_map = SDL_CreateGPUTexture(gpu, &map_info);
    SDL_GPUCommandBuffer* clearing =
        sun_map != nullptr ? SDL_AcquireGPUCommandBuffer(gpu) : nullptr;
    if (clearing != nullptr) {
        SDL_GPUDepthStencilTargetInfo clear{};
        clear.texture = sun_map;
        clear.clear_depth = 1.0F;
        clear.load_op = SDL_GPU_LOADOP_CLEAR;
        clear.store_op = SDL_GPU_STOREOP_STORE;
        clear.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        clear.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_EndGPURenderPass(SDL_BeginGPURenderPass(clearing, nullptr, 0, &clear));
    }
    if (clearing == nullptr || !submit_paced(device.native(), clearing)) {
        device.last_error_ = sdl_error("Creating the sun's shadow map failed");
        if (sun_map != nullptr) {
            SDL_ReleaseGPUTexture(gpu, sun_map);
        }
        SDL_ReleaseGPUSampler(gpu, sampler);
        SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu, shadow_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu, blend_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(gpu, sun_pipeline);
        return nullptr;
    }

    auto native = std::make_unique<Native>();
    native->device = gpu;
    native->pipeline = pipeline;
    native->shadow_pipeline = shadow_pipeline;
    native->blend_pipeline = blend_pipeline;
    native->sun_pipeline = sun_pipeline;
    native->sun_map = sun_map;
    native->shadow_mask = std::move(shadow_mask);
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
    native_->shadows.clear();
    native_->shadow_shapes.clear();
    native_->translucent.clear();
    native_->casts.clear();
    native_->sun.reset();
}

void WorldRenderer::set_sun_shadow(const SunShadow& shadow) {
    if (native_->target == nullptr) {
        return;
    }
    if (shadow.darkness > 0.0F && std::isfinite(shadow.darkness) &&
        std::all_of(shadow.view_projection.m.begin(), shadow.view_projection.m.end(),
                    [](float v) { return std::isfinite(v); })) {
        native_->sun = shadow;
    } else {
        native_->sun.reset();
    }
}

namespace {

/// A shadow's corner: its darkness travels in the colour's red (shadow.frag.hlsl).
WorldVertex shadow_corner(const Vec3& p, float u, float v, float darkness) {
    const float d = std::isfinite(darkness) ? std::clamp(darkness, 0.0F, 1.0F) : 0.0F;
    return WorldVertex{p.x,  p.y,  p.z,  u,    v,    d,    d,    d,
                       1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
}

} // namespace

void WorldRenderer::draw_shadow(const std::array<Vec3, 4>& corners, float darkness) {
    if (native_->target == nullptr || !(darkness > 0.0F)) {
        return;
    }
    const WorldVertex tl = shadow_corner(corners[0], 0.0F, 0.0F, darkness);
    const WorldVertex tr = shadow_corner(corners[1], 1.0F, 0.0F, darkness);
    const WorldVertex br = shadow_corner(corners[2], 1.0F, 1.0F, darkness);
    const WorldVertex bl = shadow_corner(corners[3], 0.0F, 1.0F, darkness);
    const WorldVertex quad[6] = {tl, tr, bl, tr, br, bl};
    native_->shadows.insert(native_->shadows.end(), std::begin(quad), std::end(quad));
    native_->shadow_shapes.push_back(nullptr);
}

void WorldRenderer::draw_shadow(const Texture& shape, const std::array<Vec3, 4>& corners,
                                Rect source, const std::array<float, 4>& darkness) {
    if (native_->target == nullptr ||
        std::none_of(darkness.begin(), darkness.end(), [](float d) { return d > 0.0F; })) {
        return;
    }
    // As draw() maps a source: a hundredth of a texel inside its edges.
    constexpr float inset = 0.01F;
    const auto tex_w = static_cast<float>(shape.width());
    const auto tex_h = static_cast<float>(shape.height());
    const float u0 = (source.x + inset) / tex_w;
    const float v0 = (source.y + inset) / tex_h;
    const float u1 = (source.x + source.w - inset) / tex_w;
    const float v1 = (source.y + source.h - inset) / tex_h;
    const WorldVertex tl = shadow_corner(corners[0], u0, v0, darkness[0]);
    const WorldVertex tr = shadow_corner(corners[1], u1, v0, darkness[1]);
    const WorldVertex br = shadow_corner(corners[2], u1, v1, darkness[2]);
    const WorldVertex bl = shadow_corner(corners[3], u0, v1, darkness[3]);
    const WorldVertex quad[6] = {tl, tr, bl, tr, br, bl};
    native_->shadows.insert(native_->shadows.end(), std::begin(quad), std::end(quad));
    native_->shadow_shapes.push_back(&shape);
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
    if (color[3] < 0.999F || texture.translucent()) {
        if (!(color[3] > 0.0F)) {
            return; // fully clear: nothing to draw
        }
        const Vec3 middle{(corners[0].x + corners[2].x) / 2.0F,
                          (corners[0].y + corners[2].y) / 2.0F,
                          (corners[0].z + corners[2].z) / 2.0F};
        const Vec3 eye = native_->eye;
        const float dx = middle.x - eye.x;
        const float dy = middle.y - eye.y;
        const float dz = middle.z - eye.z;
        native_->translucent.push_back(Native::Translucent{
            {tl, tr, bl, tr, br, bl}, &texture, (dx * dx) + (dy * dy) + (dz * dz)});
        return;
    }
    const WorldVertex quad[6] = {tl, tr, bl, tr, br, bl};
    native_->vertices.insert(native_->vertices.end(), std::begin(quad), std::end(quad));
    native_->textures.push_back(&texture);
    native_->casts.push_back(surface.casts_shadow);
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

    // Shadows go after the quads in the same buffer, then the see-through quads,
    // farthest first (in the order drawn where equally far).
    const auto opaque_count = static_cast<std::uint32_t>(n.vertices.size());
    n.vertices.insert(n.vertices.end(), n.shadows.begin(), n.shadows.end());
    const auto shadowed_count = static_cast<std::uint32_t>(n.vertices.size());
    std::stable_sort(n.translucent.begin(), n.translucent.end(),
                     [](const Native::Translucent& a, const Native::Translucent& b) {
                         return a.distance > b.distance;
                     });
    for (const Native::Translucent& quad : n.translucent) {
        n.vertices.insert(n.vertices.end(), quad.vertices.begin(), quad.vertices.end());
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

    // The sun's shadow map: every opaque quad that casts, seen from the sun.
    stats_.sun_casters = 0;
    if (n.sun && vertex_count > 0) {
        SDL_GPUDepthStencilTargetInfo map{};
        map.texture = n.sun_map;
        map.clear_depth = 1.0F;
        map.load_op = SDL_GPU_LOADOP_CLEAR;
        map.store_op = SDL_GPU_STOREOP_STORE;
        map.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        map.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* sun_pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &map);
        SDL_BindGPUGraphicsPipeline(sun_pass, n.sun_pipeline);
        SDL_GPUBufferBinding binding{};
        binding.buffer = n.vertex_buffer;
        SDL_BindGPUVertexBuffers(sun_pass, 0, &binding, 1);
        std::array<float, 20> sun_camera{};
        std::copy(n.sun->view_projection.m.begin(), n.sun->view_projection.m.end(),
                  sun_camera.begin());
        SDL_PushGPUVertexUniformData(commands, 0, sun_camera.data(), sizeof(sun_camera));
        std::size_t first = 0;
        while (first < n.textures.size()) {
            if (!n.casts[first]) {
                ++first;
                continue;
            }
            std::size_t last = first + 1;
            while (last < n.textures.size() && n.casts[last] &&
                   n.textures[last] == n.textures[first]) {
                ++last;
            }
            SDL_GPUTextureSamplerBinding sampler{};
            sampler.texture = n.textures[first]->native().texture;
            sampler.sampler = n.sampler;
            SDL_BindGPUFragmentSamplers(sun_pass, 0, &sampler, 1);
            SDL_DrawGPUPrimitives(sun_pass, static_cast<std::uint32_t>((last - first) * 6), 1,
                                  static_cast<std::uint32_t>(first * 6), 0);
            ++stats_.draw_calls;
            stats_.sun_casters += last - first;
            first = last;
        }
        SDL_EndGPURenderPass(sun_pass);
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
        // The pixel shader's uniforms: fog, the point lights, then the sun's shadows (see
        // world.frag.hlsl).
        const bool fog_on = n.fog.end > n.fog.start;
        std::array<float, 4 * (3 + (2 * max_point_lights) + 6)> frame{};
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
        if (n.sun) {
            const std::size_t at = 4 * (3 + (2 * max_point_lights));
            std::copy(n.sun->view_projection.m.begin(), n.sun->view_projection.m.end(),
                      frame.begin() + static_cast<std::ptrdiff_t>(at));
            frame.at(at + 16) = 1.0F;
            frame.at(at + 17) = std::min(n.sun->darkness, 1.0F);
            frame.at(at + 18) = 1.0F / static_cast<float>(sun_shadow_size);
            frame.at(at + 19) = n.sun->bias;
            frame.at(at + 20) = n.sun->towards_sun.x;
            frame.at(at + 21) = n.sun->towards_sun.y;
            frame.at(at + 22) = n.sun->towards_sun.z;
        }
        SDL_PushGPUFragmentUniformData(commands, 0, frame.data(), sizeof(frame));
        // Every world draw samples its texture and the sun's map.
        const auto bind = [&n, pass](const Texture& texture) {
            std::array<SDL_GPUTextureSamplerBinding, 2> samplers{};
            samplers[0].texture = texture.native().texture;
            samplers[0].sampler = n.sampler;
            samplers[1].texture = n.sun_map;
            samplers[1].sampler = n.sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, samplers.data(), 2);
        };
        std::size_t first = 0;
        while (first < n.textures.size()) {
            std::size_t last = first + 1;
            while (last < n.textures.size() && n.textures[last] == n.textures[first]) {
                ++last;
            }
            bind(*n.textures[first]);
            SDL_DrawGPUPrimitives(pass, static_cast<std::uint32_t>((last - first) * 6), 1,
                                  static_cast<std::uint32_t>(first * 6), 0);
            ++stats_.draw_calls;
            first = last;
        }
        if (shadowed_count > opaque_count) {
            SDL_BindGPUGraphicsPipeline(pass, n.shadow_pipeline);
            // One draw per run of shadows sharing a shape (the blob, or a sprite's picture).
            std::size_t from = 0;
            while (from < n.shadow_shapes.size()) {
                std::size_t to = from + 1;
                while (to < n.shadow_shapes.size() &&
                       n.shadow_shapes[to] == n.shadow_shapes[from]) {
                    ++to;
                }
                const Texture* shape = n.shadow_shapes[from];
                SDL_GPUTextureSamplerBinding mask{};
                mask.texture = (shape != nullptr ? shape : n.shadow_mask.get())->native().texture;
                mask.sampler = n.sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, &mask, 1);
                SDL_DrawGPUPrimitives(pass, static_cast<std::uint32_t>((to - from) * 6), 1,
                                      opaque_count + static_cast<std::uint32_t>(from * 6), 0);
                ++stats_.draw_calls;
                from = to;
            }
        }
        if (vertex_count > shadowed_count) {
            SDL_BindGPUGraphicsPipeline(pass, n.blend_pipeline);
            frame[7] = 1.0F; // the pixel shader's blended mode
            SDL_PushGPUFragmentUniformData(commands, 0, frame.data(), sizeof(frame));
            std::size_t from = 0;
            while (from < n.translucent.size()) {
                std::size_t to = from + 1;
                while (to < n.translucent.size() &&
                       n.translucent[to].texture == n.translucent[from].texture) {
                    ++to;
                }
                bind(*n.translucent[from].texture);
                SDL_DrawGPUPrimitives(pass, static_cast<std::uint32_t>((to - from) * 6), 1,
                                      shadowed_count + static_cast<std::uint32_t>(from * 6), 0);
                ++stats_.draw_calls;
                from = to;
            }
        }
    }
    SDL_EndGPURenderPass(pass);
    const bool submitted = submit_paced(device_.native(), commands);
    release(n.device, upload);
    if (!submitted) {
        device_.last_error_ = sdl_error("Submitting the frame failed");
        return false;
    }
    stats_.quads = n.textures.size() + n.translucent.size();
    stats_.translucent = n.translucent.size();
    stats_.shadows = n.shadows.size() / 6;
    return true;
}

// ---------------------------------------------------------------------------------------
// Post-processing
// ---------------------------------------------------------------------------------------

bool Look::neutral() const noexcept {
    const bool no_tilt = !tilt_shift || !(blur >= 0.5F);
    return no_tilt && !(bloom > 0.0F) && exposure == 1.0F && contrast == 1.0F &&
           saturation == 1.0F && !(vignette > 0.0F) && tint_r == 1.0F && tint_g == 1.0F &&
           tint_b == 1.0F;
}

struct PostProcessor::Native {
    SDL_GPUDevice* device{nullptr};
    SDL_GPUGraphicsPipeline* blur{nullptr};  ///< The first pass, into two targets.
    SDL_GPUGraphicsPipeline* final{nullptr}; ///< The second pass.
    SDL_GPUSampler* sampler{nullptr};
    // The first pass's outputs, the size of the frame.
    SDL_GPUTexture* blurred{nullptr};
    SDL_GPUTexture* bright{nullptr};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

PostProcessor::PostProcessor(Device& device, std::unique_ptr<Native> native)
    : device_(device), native_(std::move(native)) {}

PostProcessor::~PostProcessor() {
    const Native& n = *native_;
    for (SDL_GPUTexture* texture : {n.blurred, n.bright}) {
        if (texture != nullptr) {
            SDL_ReleaseGPUTexture(n.device, texture);
        }
    }
    SDL_ReleaseGPUSampler(n.device, n.sampler);
    SDL_ReleaseGPUGraphicsPipeline(n.device, n.blur);
    SDL_ReleaseGPUGraphicsPipeline(n.device, n.final);
}

std::unique_ptr<PostProcessor> PostProcessor::create(Device& device) {
    SDL_GPUDevice* gpu = device.native().device;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(gpu);
    const bool spirv = (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
    if (!spirv && (formats & SDL_GPU_SHADERFORMAT_DXIL) == 0) {
        device.last_error_ = "The GPU accepts neither SPIR-V nor DXIL shaders.";
        return nullptr;
    }
    const SDL_GPUShaderFormat format =
        spirv ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_DXIL;
    const auto shader = [&](const auto& spirv_code, const auto& dxil_code, SDL_GPUShaderStage stage,
                            std::uint32_t samplers, std::uint32_t uniforms) {
        SDL_GPUShaderCreateInfo info{};
        info.code = spirv ? spirv_code.data() : dxil_code.data();
        info.code_size = spirv ? spirv_code.size() : dxil_code.size();
        info.entrypoint = "main";
        info.format = format;
        info.stage = stage;
        info.num_samplers = samplers;
        info.num_uniform_buffers = uniforms;
        return SDL_CreateGPUShader(gpu, &info);
    };
    SDL_GPUShader* vertex =
        shader(post_vert_spirv, post_vert_dxil, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader* blur_pixels =
        shader(post_blur_frag_spirv, post_blur_frag_dxil, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader* final_pixels =
        shader(post_final_frag_spirv, post_final_frag_dxil, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);

    auto native = std::make_unique<Native>();
    native->device = gpu;
    if (vertex == nullptr || blur_pixels == nullptr || final_pixels == nullptr) {
        device.last_error_ = sdl_error("Creating the post-processing shaders failed");
    } else {
        const auto pipeline = [&](SDL_GPUShader* pixels, std::uint32_t targets) {
            std::array<SDL_GPUColorTargetDescription, 2> colors{};
            for (SDL_GPUColorTargetDescription& color : colors) {
                color.format = pixel_format;
            }
            SDL_GPUGraphicsPipelineCreateInfo info{};
            info.vertex_shader = vertex;
            info.fragment_shader = pixels;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.target_info.color_target_descriptions = colors.data();
            info.target_info.num_color_targets = targets;
            return SDL_CreateGPUGraphicsPipeline(gpu, &info);
        };
        native->blur = pipeline(blur_pixels, 2);
        native->final = pipeline(final_pixels, 1);
        if (native->blur == nullptr || native->final == nullptr) {
            device.last_error_ = sdl_error("Creating the post-processing pipelines failed");
        }
    }
    release(gpu, vertex);
    release(gpu, blur_pixels);
    release(gpu, final_pixels);
    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    native->sampler = SDL_CreateGPUSampler(gpu, &sampler_info);
    if (native->blur == nullptr || native->final == nullptr || native->sampler == nullptr) {
        if (native->sampler == nullptr && native->blur != nullptr && native->final != nullptr) {
            device.last_error_ = sdl_error("Creating the post-processing sampler failed");
        }
        if (native->sampler != nullptr) {
            SDL_ReleaseGPUSampler(gpu, native->sampler);
        }
        if (native->blur != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(gpu, native->blur);
        }
        if (native->final != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(gpu, native->final);
        }
        return nullptr;
    }
    return std::unique_ptr<PostProcessor>(new PostProcessor(device, std::move(native)));
}

bool PostProcessor::apply(const RenderTarget& source, RenderTarget& destination, const Look& look) {
    Native& n = *native_;
    if (&source == &destination || source.width() != destination.width() ||
        source.height() != destination.height()) {
        device_.last_error_ = "Post-processing needs another target of the same size.";
        return false;
    }
    // The intermediate targets follow the frame's size.
    if (n.blurred == nullptr || n.width != source.width() || n.height != source.height()) {
        for (SDL_GPUTexture** texture : {&n.blurred, &n.bright}) {
            if (*texture != nullptr) {
                SDL_ReleaseGPUTexture(n.device, *texture);
            }
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = pixel_format;
            info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = source.width();
            info.height = source.height();
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            *texture = SDL_CreateGPUTexture(n.device, &info);
        }
        n.width = source.width();
        n.height = source.height();
        if (n.blurred == nullptr || n.bright == nullptr) {
            n.width = 0;
            device_.last_error_ = sdl_error("Creating the post-processing targets failed");
            return false;
        }
    }

    // The uniforms both passes read (see post_blur.frag.hlsl), with every value clamped.
    const auto clamped = [](float value, float low, float high, float fallback) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    const float half_band = clamped(look.focus_height, 0.0F, 1.0F, 0.35F) / 2.0F;
    const std::array<float, 20> uniforms{static_cast<float>(source.width()),
                                         static_cast<float>(source.height()),
                                         0.0F,
                                         0.0F,
                                         clamped(look.focus_center, 0.0F, 1.0F, 0.55F),
                                         half_band,
                                         look.tilt_shift ? clamped(look.blur, 0.0F, 12.0F, 0.0F)
                                                         : 0.0F,
                                         clamped(look.blur_ramp, 0.0F, 1.0F, 0.25F),
                                         clamped(look.bloom_threshold, 0.0F, 1.0F, 0.75F),
                                         clamped(look.bloom, 0.0F, 2.0F, 0.0F),
                                         clamped(look.bloom_radius, 0.0F, 16.0F, 6.0F),
                                         0.0F,
                                         clamped(look.exposure, 0.0F, 4.0F, 1.0F),
                                         clamped(look.contrast, 0.0F, 2.0F, 1.0F),
                                         clamped(look.saturation, 0.0F, 2.0F, 1.0F),
                                         clamped(look.vignette, 0.0F, 1.0F, 0.0F),
                                         clamped(look.tint_r, 0.0F, 2.0F, 1.0F),
                                         clamped(look.tint_g, 0.0F, 2.0F, 1.0F),
                                         clamped(look.tint_b, 0.0F, 2.0F, 1.0F),
                                         0.0F};

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(n.device);
    if (commands == nullptr) {
        device_.last_error_ = sdl_error("Acquiring a command buffer failed");
        return false;
    }
    SDL_PushGPUFragmentUniformData(commands, 0, uniforms.data(), sizeof(uniforms));

    // Pass 1: the scene, blurred along rows, into the two intermediate targets.
    std::array<SDL_GPUColorTargetInfo, 2> outputs{};
    outputs[0].texture = n.blurred;
    outputs[1].texture = n.bright;
    for (SDL_GPUColorTargetInfo& output : outputs) {
        output.load_op = SDL_GPU_LOADOP_DONT_CARE;
        output.store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, outputs.data(), 2, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, n.blur);
    SDL_GPUTextureSamplerBinding scene{};
    scene.texture = source.native().texture;
    scene.sampler = n.sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &scene, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    // Pass 2: down the columns, then bloom, grading and the vignette, into the destination.
    SDL_GPUColorTargetInfo output{};
    output.texture = destination.native().texture;
    output.load_op = SDL_GPU_LOADOP_DONT_CARE;
    output.store_op = SDL_GPU_STOREOP_STORE;
    pass = SDL_BeginGPURenderPass(commands, &output, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, n.final);
    std::array<SDL_GPUTextureSamplerBinding, 2> inputs{};
    inputs[0].texture = n.blurred;
    inputs[0].sampler = n.sampler;
    inputs[1].texture = n.bright;
    inputs[1].sampler = n.sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, inputs.data(), 2);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    if (!submit_paced(device_.native(), commands)) {
        device_.last_error_ = sdl_error("Submitting the post-processing failed");
        return false;
    }
    return true;
}

} // namespace engine::render
