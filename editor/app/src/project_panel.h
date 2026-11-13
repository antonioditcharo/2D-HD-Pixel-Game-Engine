#pragma once

// The Project view: the project's settings as fields, grouped as a creator thinks of them
// (the game, the picture, the Diorama camera, light and shadows, the look). Changes apply
// as you make them, so the scene shows them at once, and are steps of the Document's
// undo; the game's own checks refuse values out of range.
//
// See docs/dev/editor.md.

#include "engine/render/render.h"

#include <QWidget>
#include <optional>

#include "editor/core/document.h"

class QFormLayout;

namespace editor {

class ProjectPanel : public QWidget {
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget* parent = nullptr);

    /// Shows `document`'s settings (null shows nothing).
    void set_document(Document* document);
    /// Puts the document's settings back into the fields (after undo, for example).
    void refresh();

Q_SIGNALS:
    /// The settings were changed here.
    void edited();
    /// A change was refused, with why.
    void refused(const QString& message);
    /// A Look preset is hovered (shown in the scene without changing anything), or no longer.
    void previewing(std::optional<engine::render::Look> look);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void build();
    /// Changes the settings with `change`, or reports why not.
    template <typename Change>
    void apply(const Change& change);

    Document* document_ = nullptr;
    QWidget* content_ = nullptr;
    bool filling_ = false; ///< While the fields are being filled: their signals are not edits.
};

} // namespace editor
