#include "asset_browser.h"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <algorithm>

namespace editor {

QStringList asset_kinds() {
    return {QStringLiteral("png"),     QStringLiteral("wav"),     QStringLiteral("ogg"),
            QStringLiteral("mp3"),     QStringLiteral("flac"),    QStringLiteral("dscene"),
            QStringLiteral("drules"),  QStringLiteral("dsprite"), QStringLiteral("dtalk"),
            QStringLiteral("dprefab"), QStringLiteral("dtable"),  QStringLiteral("aseprite"),
            QStringLiteral("ase")};
}

QStringList asset_filters(const QString& search) {
    const QStringList words = search.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString middle = QStringLiteral("*");
    for (const QString& word : words) {
        // Wildcard characters typed are taken as themselves.
        QString plain = word;
        plain.replace(QLatin1Char('['), QStringLiteral("[[]"));
        plain.replace(QLatin1Char('*'), QStringLiteral("[*]"));
        plain.replace(QLatin1Char('?'), QStringLiteral("[?]"));
        middle += plain + QLatin1Char('*');
    }
    QStringList filters;
    for (const QString& kind : asset_kinds()) {
        filters << middle + QLatin1Char('.') + kind;
    }
    return filters;
}

QIcon AssetThumbnails::thumbnail(const QString& path, int size) const {
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        return {};
    }
    const QFileInfo file(path);
    const QDateTime changed = file.lastModified();
    if (const auto found = cache_.constFind(path);
        found != cache_.constEnd() && found->first == changed) {
        return found->second;
    }
    const QImage image(path);
    QIcon icon;
    if (!image.isNull()) {
        // Whole-pixel steps when it is smaller, smooth when it must shrink.
        const int longest = std::max(image.width(), image.height());
        const QImage fitted =
            longest <= size
                ? image.scaled(image.width() * (size / longest), image.height() * (size / longest),
                               Qt::KeepAspectRatio, Qt::FastTransformation)
                : image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap square(size, size);
        square.fill(Qt::transparent);
        QPainter painter(&square);
        painter.drawImage((size - fitted.width()) / 2, (size - fitted.height()) / 2, fitted);
        painter.end();
        icon = QIcon(square);
    }
    cache_.insert(path, {changed, icon});
    return icon;
}

void AssetThumbnails::initStyleOption(QStyleOptionViewItem* option,
                                      const QModelIndex& index) const {
    QStyledItemDelegate::initStyleOption(option, index);
    const QString path = index.data(QFileSystemModel::FilePathRole).toString();
    const int size = std::max(option->decorationSize.width(), option->decorationSize.height());
    if (const QIcon icon = thumbnail(path, size); !icon.isNull()) {
        option->icon = icon;
        option->features |= QStyleOptionViewItem::HasDecoration;
    }
}

} // namespace editor
