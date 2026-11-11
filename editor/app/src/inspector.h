#pragma once

// The inspector (plan/06 §4): the selected entity's name and components, with a field for
// every property, generated from the reflection registry. Nothing here knows any
// component: a new component type appears with its fields as soon as it is registered.
//
// See docs/dev/editor.md.

#include <QWidget>

#include "editor/core/document.h"

class QVBoxLayout;

namespace editor {

class Inspector : public QWidget {
    Q_OBJECT

public:
    explicit Inspector(QWidget* parent = nullptr);

    /// Shows `entity` of `document` (or nothing, for an empty GUID or no document).
    void show_entity(Document* document, engine::Guid entity);
    /// Shows fields marked Advanced too.
    void set_show_advanced(bool show);

Q_SIGNALS:
    /// A field changed the document; `structure` if components were added or removed or
    /// the entity renamed, so other views rebuild.
    void edited(bool structure);
    /// A change was refused, with why, in plain language.
    void refused(const QString& message);

private:
    void rebuild();
    QWidget* field(const engine::reflect::TypeInfo& component,
                   const engine::reflect::PropertyInfo& property);

    Document* document_ = nullptr;
    engine::Guid entity_;
    bool show_advanced_ = false;
    QVBoxLayout* layout_ = nullptr;
    QWidget* content_ = nullptr;
};

} // namespace editor
