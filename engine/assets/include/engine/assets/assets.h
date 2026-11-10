#pragma once

// The asset database: every file in a project's assets folder, identified by a GUID kept
// in a ".meta" file next to it, so that references survive renames and moves (plan/03
// §4-5). It detects what changed since the last scan, which drives importing and hot
// reload, and imports images.
//
// See docs/dev/assets.md.

#include "engine/foundation/guid.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::assets {

/// One asset as last scanned.
struct AssetRecord {
    Guid guid;
    std::string path;     ///< Relative to the assets folder, with '/' separators.
    std::string importer; ///< "image" for PNG files, "file" for everything else.
    std::uint64_t size = 0;
    std::uint64_t content_hash = 0; ///< FNV-1a of the file's bytes.
};

/// A problem found while scanning. The asset concerned is skipped or repaired as the
/// message says; nothing a person wrote is deleted or overwritten.
struct AssetIssue {
    std::string path;
    std::string message;

    friend bool operator==(const AssetIssue&, const AssetIssue&) = default;
};

/// What changed since the previous refresh(). Each list is sorted by GUID.
struct AssetChanges {
    std::vector<Guid> added;
    std::vector<Guid> modified; ///< The content changed.
    std::vector<Guid> moved;    ///< The path changed; the GUID stayed.
    std::vector<Guid> removed;

    [[nodiscard]] bool empty() const noexcept {
        return added.empty() && modified.empty() && moved.empty() && removed.empty();
    }
};

class AssetDatabase {
public:
    /// Watches `root`, a project's assets folder. Nothing is read until refresh().
    explicit AssetDatabase(std::filesystem::path root);

    /// Scans the folder, writes ".meta" files for new assets, and returns what changed.
    ///
    /// - Files and folders whose names start with "." are ignored.
    /// - A file without a ".meta" gets one with a new GUID, unless it has the same
    ///   content as an asset that vanished in this scan: then it is that asset, moved
    ///   without its ".meta", and keeps its GUID (the stale ".meta" left behind is
    ///   removed, since it held only that GUID).
    /// - A ".meta" that cannot be read is reported and left untouched, and its asset is
    ///   skipped until it is fixed.
    /// - If two ".meta" files hold the same GUID (a file copied with its ".meta"), the
    ///   one already known, or else the first by path, keeps it; the other gets a new
    ///   GUID and the repair is reported.
    AssetChanges refresh();

    [[nodiscard]] const AssetRecord* find(Guid guid) const;
    [[nodiscard]] const AssetRecord* find(std::string_view path) const;
    /// Every asset, sorted by path.
    [[nodiscard]] std::vector<AssetRecord> assets() const;
    /// Problems found by the last refresh().
    [[nodiscard]] const std::vector<AssetIssue>& issues() const noexcept { return issues_; }

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    /// Reads an asset's bytes. Returns nothing if it is unknown or cannot be read.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read(Guid guid) const;

private:
    struct Known {
        AssetRecord record;
        std::filesystem::file_time_type modified;
    };

    std::filesystem::path root_;
    std::unordered_map<Guid, Known, GuidHash> known_;
    std::vector<AssetIssue> issues_;
};

/// The ".meta" format name and version.
inline constexpr std::string_view meta_format = "sparxel.meta";
inline constexpr int meta_version = 1;

/// An image as RGBA8 pixels, rows top to bottom.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

/// Decodes a PNG file's bytes. Returns nothing and sets `error` if they are not a valid
/// PNG.
[[nodiscard]] std::optional<Image> decode_png(std::span<const std::uint8_t> bytes,
                                              std::string& error);

/// Encodes RGBA8 pixels as PNG, for tools and tests. Empty on failure.
[[nodiscard]] std::vector<std::uint8_t> encode_png(const Image& image);

/// FNV-1a, 64-bit: the content hash used for change and move detection.
[[nodiscard]] std::uint64_t content_hash(std::span<const std::uint8_t> bytes) noexcept;

} // namespace engine::assets
