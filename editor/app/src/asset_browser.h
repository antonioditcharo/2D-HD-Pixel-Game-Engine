#pragma once

// The Assets panel's looks (plan/02 A4): pictures shown by small thumbnails, and the name
// filters of its search box.
//
// See docs/dev/editor.md (The Assets panel).

#include <QDateTime>
#include <QHash>
#include <QIcon>
#include <QStringList>
#include <QStyledItemDelegate>
#include <utility>

namespace editor {

/// The kinds of file the Assets panel lists.
[[nodiscard]] QStringList asset_kinds();
/// The name filters for a search: every kind, with `search` somewhere in the name (each
/// word of it, in order). An empty search lists everything.
[[nodiscard]] QStringList asset_filters(const QString& search);

/// Draws each picture in the Assets panel by a thumbnail of itself, scaled by whole pixels
/// so pixel art stays sharp. Thumbnails are made once, and again when the file changes.
class AssetThumbnails final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /// The thumbnail of the picture at `path`, `size` pixels square; null if it is not one.
    [[nodiscard]] QIcon thumbnail(const QString& path, int size) const;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    mutable QHash<QString, std::pair<QDateTime, QIcon>> cache_;
};

} // namespace editor
