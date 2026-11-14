#pragma once

// Draws a Document's scene as the game would show it, for the editor's scene view: the
// project's resolution, render style, lighting and look, with the camera where the editor
// puts it. Rendering runs on a headless GPU device and returns pixels, so the Qt view and
// tests alike can use it.
//
// See docs/dev/editor.md.

#include "engine/assets/assets.h"
#include "engine/render/render.h"

#include <memory>
#include <optional>
#include <string>

#include "editor/core/document.h"

namespace editor {

class Preview {
public:
    /// Returns null, with `error`, if there is no GPU to draw with.
    [[nodiscard]] static std::unique_ptr<Preview> create(std::string& error);

    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;
    Preview(Preview&&) = delete;
    Preview& operator=(Preview&&) = delete;
    ~Preview();

    /// The scene of `document` with the camera looking at map point (x, y): an image of the
    /// project's width and height. Nothing, with `error`, if drawing failed.
    [[nodiscard]] std::optional<engine::assets::Image> render(Document& document, double x,
                                                              double y, std::string& error);
    /// Draws with `look` in place of the project's (a preset being hovered), until reset.
    void set_look_override(std::optional<engine::render::Look> look) { look_override_ = look; }

    /// Forgets the pictures that `changes` (from the document's AssetDatabase::refresh())
    /// modified or removed, so the next render() draws them as they are now: a sprite
    /// saved in Sprite Studio, or a picture changed in another program.
    void assets_changed(const engine::assets::AssetChanges& changes);

private:
    std::optional<engine::render::Look> look_override_;
    struct Impl;
    explicit Preview(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace editor
