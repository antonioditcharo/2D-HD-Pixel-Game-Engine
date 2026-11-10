#pragma once

// The logic compiler: checks a LogicAsset against the reflection registry and turns it into
// Luau source for the Vm, with a debug map from source lines back to blocks.
//
// See docs/dev/logic-runtime.md §7.

#include "engine/foundation/reflect.h"
#include "engine/logic/ir.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::logic {

/// Tells the compiler what each object a logic asset names is made of: the types of all its
/// parts, for example every component of an entity in the map being edited, or
/// Vm::object_types(). Empty for unknown objects. Members are looked up across the parts in
/// the order given.
using ObjectTypes = std::function<std::vector<const reflect::TypeInfo*>(std::string_view object)>;

struct CompileOptions {
    /// Emit a call to `sx.trace()` before every block, so the editor can highlight blocks
    /// as they run (Vm::set_trace_handler). Costs one host call per block.
    bool trace = false;
};

/// A problem that stops an asset from compiling, attached to the block to show it on.
struct Diagnostic {
    BlockId block; ///< Invalid (0) for problems of the asset itself, such as a bad name.
    std::string message;
};

struct CompiledLogic {
    /// Luau source for Vm::load(). Empty if there are errors.
    std::string source;
    /// The block each line of `source` belongs to: `line_blocks[line - 1]`. Lines that
    /// belong to no block hold an invalid id.
    std::vector<BlockId> line_blocks;
    std::vector<Diagnostic> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
    /// The block for a line reported by the Vm (1-based). Invalid if there is none.
    [[nodiscard]] BlockId block_at_line(int line) const noexcept;
};

/// Checks and compiles `asset`. Never throws for bad input: every problem becomes a
/// Diagnostic with a plain-language message.
[[nodiscard]] CompiledLogic compile(const LogicAsset& asset, const reflect::Registry& registry,
                                    const ObjectTypes& objects, CompileOptions options = {});

/// The global function name a compiled sequence is started by, for Vm::start(). For
/// example "seq_elder_intro" for the sequence "elder_intro".
[[nodiscard]] std::string sequence_entry(std::string_view sequence_name);

/// The event name rules listen to, for Vm::emit(): "chest.opened", or "game_start" for a
/// game-wide event.
[[nodiscard]] std::string event_key(std::string_view object, std::string_view event);

} // namespace engine::logic
