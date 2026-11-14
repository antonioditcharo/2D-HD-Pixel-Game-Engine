#include "editor/core/snapshots.h"

#include "engine/serialize/json.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace editor {

namespace fs = std::filesystem;
namespace serialize = engine::serialize;

namespace {

constexpr const char* info_file = "snapshot.json";
constexpr const char* picture_file = "picture.png";

/// The local time now, as "2026-11-14 05-12-03" (for folders) and "2026-11-14 05:12".
std::pair<std::string, std::string> now_text() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    std::array<char, 32> folder{};
    std::array<char, 32> shown{};
    std::strftime(folder.data(), folder.size(), "%Y-%m-%d %H-%M-%S", &local);
    std::strftime(shown.data(), shown.size(), "%Y-%m-%d %H:%M", &local);
    return {folder.data(), shown.data()};
}

/// A name safe as part of a folder's name.
std::string folder_safe(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
        out += plain ? c : '_';
        if (out.size() >= 40) {
            break;
        }
    }
    return out;
}

/// Copies everything in `from` but its .sparxel folder into `to`.
bool copy_project(const fs::path& from, const fs::path& to, std::error_code& failed) {
    fs::create_directories(to, failed);
    if (failed) {
        return false;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(from, failed)) {
        if (entry.path().filename() == ".sparxel") {
            continue;
        }
        fs::copy(entry.path(), to / entry.path().filename(),
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing, failed);
        if (failed) {
            return false;
        }
    }
    return !failed;
}

} // namespace

fs::path snapshots_folder(const fs::path& project) {
    return project / ".sparxel" / "snapshots";
}

Snapshot take_snapshot(const fs::path& project, std::string_view name,
                       const std::vector<std::uint8_t>& picture, std::string& error) {
    const auto [stamp, when] = now_text();
    Snapshot snapshot;
    snapshot.name = name.empty() ? std::string("Snapshot") : std::string(name);
    snapshot.when = when;
    std::string id = stamp + " " + folder_safe(snapshot.name);
    // Two in one second: numbered.
    for (int n = 2; fs::exists(snapshots_folder(project) / id); ++n) {
        id = stamp + " " + folder_safe(snapshot.name) + " " + std::to_string(n);
    }
    snapshot.folder = snapshots_folder(project) / id;
    std::error_code failed;
    if (!copy_project(project, snapshot.folder / "project", failed)) {
        error = "The snapshot could not be written: " + failed.message();
        fs::remove_all(snapshot.folder, failed);
        return {};
    }
    std::ofstream(snapshot.folder / info_file, std::ios::binary)
        << serialize::to_text(serialize::Json{{"name", snapshot.name}, {"when", snapshot.when}});
    if (!picture.empty()) {
        snapshot.picture = snapshot.folder / picture_file;
        std::ofstream(snapshot.picture, std::ios::binary)
            .write(reinterpret_cast<const char*>(picture.data()), // NOLINT
                   static_cast<std::streamsize>(picture.size()));
    }
    snapshot.id = id;
    return snapshot;
}

std::vector<Snapshot> list_snapshots(const fs::path& project) {
    std::vector<Snapshot> out;
    std::error_code failed;
    const fs::path folder = snapshots_folder(project);
    if (!fs::is_directory(folder, failed)) {
        return out;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, failed)) {
        if (!entry.is_directory(failed) || !fs::is_directory(entry.path() / "project", failed)) {
            continue;
        }
        Snapshot s;
        s.id = entry.path().filename().string();
        s.folder = entry.path();
        s.name = s.id;
        std::ifstream in(entry.path() / info_file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        std::vector<serialize::Issue> ignored;
        if (const std::optional<serialize::Json> info = serialize::parse(text, ignored)) {
            if (info->contains("name") && (*info)["name"].is_string()) {
                s.name = (*info)["name"].get<std::string>();
            }
            if (info->contains("when") && (*info)["when"].is_string()) {
                s.when = (*info)["when"].get<std::string>();
            }
        }
        if (fs::exists(entry.path() / picture_file, failed)) {
            s.picture = entry.path() / picture_file;
        }
        out.push_back(std::move(s));
    }
    // Ids begin with the time: newest first.
    std::sort(out.begin(), out.end(),
              [](const Snapshot& a, const Snapshot& b) { return a.id > b.id; });
    return out;
}

bool restore_snapshot(const fs::path& project, std::string_view id, std::string& error) {
    const fs::path from = snapshots_folder(project) / std::string(id) / "project";
    std::error_code failed;
    if (id.empty() || id.find("..") != std::string_view::npos || !fs::is_directory(from, failed)) {
        error = "There is no such snapshot.";
        return false;
    }
    // The project as it is now, first, so a restore can be undone.
    const Snapshot before = take_snapshot(project, "Before restoring", {}, error);
    if (before.id.empty()) {
        return false;
    }
    // Everything but the snapshots goes, then the snapshot's copy comes back.
    for (const fs::directory_entry& entry : fs::directory_iterator(project, failed)) {
        if (entry.path().filename() != ".sparxel") {
            fs::remove_all(entry.path(), failed);
        }
    }
    if (failed || !copy_project(from, project, failed)) {
        error = "The snapshot could not be put back (" + failed.message() +
                "); the project as it was is in the snapshot \"" + before.id + "\".";
        return false;
    }
    return true;
}

bool delete_snapshot(const fs::path& project, std::string_view id, std::string& error) {
    const fs::path folder = snapshots_folder(project) / std::string(id);
    std::error_code failed;
    if (id.empty() || id.find("..") != std::string_view::npos ||
        !fs::is_directory(folder, failed)) {
        error = "There is no such snapshot.";
        return false;
    }
    fs::remove_all(folder, failed);
    if (failed) {
        error = "The snapshot could not be deleted: " + failed.message();
        return false;
    }
    return true;
}

} // namespace editor
