#pragma once

// Project Doctor (plan/02 A10): checks a whole project for what would go wrong in the game
// and says so in plain language, each finding pointing at where to fix it: pictures and
// sounds that are gone, logic that does not run, a start scene that is missing, scenes no
// logic leads to, and files nothing uses. No user interface: tested on its own.
//
// See docs/dev/editor.md.

#include "engine/foundation/guid.h"
#include "engine/logic/ir.h"

#include <cstdint>
#include <string>
#include <vector>

#include "editor/core/document.h"

namespace editor {

struct Finding {
    /// Problem: the game will not work as meant. Warning: probably a mistake. Note: tidy-up.
    enum class Severity : std::uint8_t { Problem, Warning, Note };
    Severity severity = Severity::Problem;
    std::string message; ///< Plain language, saying where and what.
    /// Where to fix it, as much as is known: a scene (its path in assets/) and an entity of
    /// it, a logic file and a block of it, or an asset.
    std::string scene;
    engine::Guid entity;
    std::string logic;
    engine::logic::BlockId block;
    std::string asset;
};

/// Checks the project open in `document`, every scene of it (the open one as it is now,
/// with its unsaved changes; the others as saved), its logic and its assets. Problems
/// first, then warnings, then notes, each in the order found.
[[nodiscard]] std::vector<Finding> examine(const Document& document);

} // namespace editor
