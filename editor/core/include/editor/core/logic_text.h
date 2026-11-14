#pragma once

// No-code logic as plain language (plan/05 §3): every Rule, Sequence and Brain of a logic
// asset as indented lines, such as "When elder: when it is talked to" and "Say “Hi.”".
// Each line reads from the blocks' own reflected texts, so a new action reads well as
// soon as it is registered. The editor's Logic view shows these, and edits them through
// the Document.
//
// See docs/dev/editor.md.

#include "engine/logic/ir.h"

#include <string>
#include <vector>

#include "editor/core/document.h"

namespace editor {

struct LogicLine {
    int depth = 0;                ///< Indentation: 0 for a Rule, Sequence or Brain.
    std::string text;             ///< Plain language.
    engine::logic::BlockId block; ///< The block it shows, to highlight and explain it.
    /// An "Otherwise:" line: `block` is the If or Rule whose Otherwise follows.
    bool otherwise = false;
};

/// Every logic asset of `document`'s project (.drules and .dsequence files), sorted by
/// path, as the document holds them now (with any edits). Files that could not be read
/// when it was opened are reported in `issues` and left out.
[[nodiscard]] std::vector<LogicFile> logic_files(Document& document,
                                                 std::vector<engine::serialize::Issue>& issues);

/// One value as it reads in a line: "talks", "speed of hero", "[answer]", "talks + 1".
[[nodiscard]] std::string describe_value(const engine::logic::Expr& value,
                                         const Document& document);

/// A sum without its last part, with that part's sign: "talks +", or
/// "(talks + [answer]) ×" when the parts so far are worked out first.
[[nodiscard]] std::string describe_sum_start(const engine::logic::Expr& sum,
                                             const Document& document);
/// Everything of a sum before the part a slot's field edits, which is inside its innermost
/// bracket (Document::bracket_logic_sum()): "talks × (a +" for talks × (a + 1). For a sum
/// with no bracket, describe_sum_start().
[[nodiscard]] std::string describe_open_sum(const engine::logic::Expr& sum,
                                            const Document& document);

/// `asset` as lines: its Rules, then Sequences, then Brains, each with its steps below
/// it. Objects are looked up in `document`: the game's own (audio, dialogue, game, input,
/// lighting) by name, and entities through their components.
[[nodiscard]] std::vector<LogicLine> describe(const engine::logic::LogicAsset& asset,
                                              const Document& document);

} // namespace editor
