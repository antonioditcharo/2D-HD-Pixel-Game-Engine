#include "editor/core/logic_text.h"

#include "engine/foundation/text.h"
#include "engine/game/logic_world.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <utility>
#include <variant>

namespace editor {

namespace logic = engine::logic;
namespace reflect = engine::reflect;
namespace serialize = engine::serialize;
using logic::Expr;
using logic::ExprKind;
using logic::Stmt;
using logic::StmtKind;

namespace {

/// Finds what an object's member is: the game's objects by type name, entities through
/// their components.
class Members {
public:
    explicit Members(const Document& document) : document_(document) {}

    template <typename Find>
    [[nodiscard]] auto find(const std::string& object, Find find_in) const
        -> decltype(find_in(std::declval<const reflect::TypeInfo&>())) {
        if (const reflect::TypeInfo* global = document_.registry().find_type(object)) {
            if (auto* found = find_in(*global)) {
                return found;
            }
        }
        const engine::world::World& world = document_.world();
        for (const engine::world::Entity e : world.find_named(object)) {
            for (const reflect::TypeInfo* type : world.components(e)) {
                if (auto* found = find_in(*type)) {
                    return found;
                }
            }
        }
        // "who", the object an event names: any kind of part.
        if (object == "who") {
            for (const reflect::TypeInfo* type : document_.registry().types()) {
                if (auto* found = find_in(*type)) {
                    return found;
                }
            }
        }
        // A tag ("#enemy"), or "it" in a Rule about one: what the tagged objects have.
        if (object.starts_with('#') || object == "it") {
            for (const std::string& tag : document_.logic_tags()) {
                if (object != "it" && tag != object) {
                    continue;
                }
                for (const reflect::TypeInfo* type : engine::game::object_types(world)(tag)) {
                    if (auto* found = find_in(*type)) {
                        return found;
                    }
                }
                for (const reflect::TypeInfo* type : document_.types_elsewhere(tag)) {
                    if (auto* found = find_in(*type)) {
                        return found;
                    }
                }
            }
        }
        // Not in the open scene: an entity of another scene of the project.
        for (const reflect::TypeInfo* type : document_.types_elsewhere(object)) {
            if (auto* found = find_in(*type)) {
                return found;
            }
        }
        return nullptr;
    }

private:
    const Document& document_;
};

/// How an object is named in a line: as it is, or "any object tagged enemy" for "#enemy".
std::string who(const std::string& object) {
    return object.starts_with('#') ? "any object tagged " + object.substr(1) : object;
}

std::string value_text(const reflect::Value& value) {
    if (const bool* yes = std::get_if<bool>(&value)) {
        return *yes ? "yes" : "no";
    }
    if (const std::int64_t* whole = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*whole);
    }
    if (const double* number = std::get_if<double>(&value)) {
        return engine::text::format_number(*number);
    }
    return "“" + std::get<std::string>(value) + "”";
}

/// `text` with its first letter in lower case, to sit inside a sentence ("A message is
/// open" in "Wait until not a message is open").
std::string inside(std::string text) {
    if (!text.empty() && text.front() >= 'A' && text.front() <= 'Z') {
        text.front() = static_cast<char>(text.front() - 'A' + 'a');
    }
    return text;
}

/// `text` with each "{id}" replaced by `fill(id)`.
template <typename Fill>
std::string fill_in(const std::string& text, Fill fill) {
    std::string out;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t open = text.find('{', at);
        const std::size_t close = open == std::string::npos ? open : text.find('}', open);
        if (close == std::string::npos) {
            out += text.substr(at);
            break;
        }
        out += text.substr(at, open - at);
        out += fill(text.substr(open + 1, close - open - 1));
        at = close + 1;
    }
    return out;
}

/// Which of `f`'s parameters a block text's "{name}" is, by its position, so values go
/// where the text names them ("Give {count} {item}" for give(item, count)); the next one in
/// order if the name is not a parameter's.
std::size_t parameter_at(const reflect::FunctionInfo& f, const std::string& name,
                         std::size_t& next) {
    for (std::size_t i = 0; i < f.parameters.size(); ++i) {
        if (f.parameters[i].id == name) {
            return i;
        }
    }
    return next++;
}

class Writer {
public:
    Writer(const Document& document, std::vector<LogicLine>& lines)
        : members_(document), lines_(lines) {}

    [[nodiscard]] std::string expr(const Expr& e) const {
        switch (e.kind) {
        case ExprKind::Literal:
            return value_text(e.literal);
        case ExprKind::Property: {
            const reflect::PropertyInfo* p = members_.find(
                e.object, [&e](const reflect::TypeInfo& t) { return t.find_property(e.member); });
            return inside(p != nullptr ? p->meta.display_name : e.member) + " of " + e.object;
        }
        case ExprKind::Call: {
            const reflect::FunctionInfo* f = members_.find(
                e.object, [&e](const reflect::TypeInfo& t) { return t.find_function(e.member); });
            if (f == nullptr) {
                return e.member + " of " + e.object;
            }
            std::size_t next = 0;
            const std::string said = fill_in(reflect::block_text(*f), [&](const std::string& name) {
                const std::size_t i = parameter_at(*f, name, next);
                return i < e.operands.size() ? expr(e.operands[i]) : std::string("…");
            });
            return inside(said) + " (" + who(e.object) + ")";
        }
        case ExprKind::Variable:
            return e.member;
        case ExprKind::EventValue:
            return "[" + e.member + "]";
        case ExprKind::Not:
            return "not " + (e.operands.empty() ? std::string("…") : expr(e.operands[0]));
        case ExprKind::All:
        case ExprKind::Any: {
            std::string joined;
            for (std::size_t i = 0; i < e.operands.size(); ++i) {
                joined += (i == 0                    ? ""
                           : e.kind == ExprKind::All ? " and "
                                                     : " or ") +
                          expr(e.operands[i]);
            }
            return "(" + joined + ")";
        }
        case ExprKind::Compare: {
            static constexpr std::array<const char*, 6> compare{" = ", " ≠ ", " < ",
                                                                " ≤ ", " > ", " ≥ "};
            return (e.operands.size() > 0 ? expr(e.operands[0]) : std::string("…")) +
                   compare.at(static_cast<std::size_t>(e.compare)) +
                   (e.operands.size() > 1 ? expr(e.operands[1]) : std::string("…"));
        }
        case ExprKind::Arithmetic:
            return sum_start(e) + " " +
                   (e.operands.size() > 1 ? part(e.operands[1], e, false) : std::string("…"));
        }
        return "…";
    }

    void line(int depth, std::string text, logic::BlockId block, bool otherwise = false) {
        lines_.push_back(LogicLine{depth, std::move(text), block, otherwise});
    }

    /// A sum without its last part, with that part's sign: "talks +", or
    /// "(talks + [answer]) ×" when the parts so far must be worked out first.
    [[nodiscard]] std::string sum_start(const Expr& sum) const {
        static constexpr std::array<const char*, 4> signs{"+", "−", "×", "÷"};
        return (sum.operands.empty() ? std::string("…") : part(sum.operands[0], sum, true)) + " " +
               signs.at(static_cast<std::size_t>(sum.arithmetic));
    }

    /// sum_start() down to the innermost bracket's last part: "talks × (a +".
    [[nodiscard]] std::string open_start(const Expr& sum) const {
        if (sum.operands.size() != 2 || sum.operands[1].kind != ExprKind::Arithmetic) {
            return sum_start(sum);
        }
        const std::string outer = sum_start(sum);
        const std::string inner = open_start(sum.operands[1]);
        // Bracketed as part() would bracket the whole of it.
        const std::string whole = part(sum.operands[1], sum, false);
        return outer + " " + (whole.starts_with("(") ? "(" + inner : inner);
    }

    /// One part of `sum`, in brackets when it is a sum that would otherwise read as
    /// working differently: (a + b) × c, a − (b + c).
    [[nodiscard]] std::string part(const Expr& e, const Expr& sum, bool first) const {
        const auto rank = [](logic::ArithmeticOp op) {
            return op == logic::ArithmeticOp::Multiply || op == logic::ArithmeticOp::Divide ? 2 : 1;
        };
        const bool bracket =
            e.kind == ExprKind::Arithmetic && (first ? rank(e.arithmetic) < rank(sum.arithmetic)
                                                     : rank(e.arithmetic) <= rank(sum.arithmetic));
        return bracket ? "(" + expr(e) + ")" : expr(e);
    }

    /// True for a group of conditions made in the editor: it and each of its conditions
    /// have their own block, so each gets its own line.
    static bool grouped(const Expr& e) {
        return (e.kind == ExprKind::Any || e.kind == ExprKind::All) && e.id.is_valid() &&
               !e.operands.empty() &&
               std::all_of(e.operands.begin(), e.operands.end(),
                           [](const Expr& each) { return each.id.is_valid(); });
    }

    /// A condition of a Rule at `depth`: "If …", or a group's line and its conditions'
    /// lines below it, groups inside groups included. A condition without a block of its
    /// own shows as `fallback` (the Rule's).
    void condition(const Expr& e, int depth, logic::BlockId fallback, bool top) {
        if (!grouped(e)) {
            line(depth, (top ? "If " : "") + expr(e), e.id.is_valid() ? e.id : fallback);
            return;
        }
        const bool any = e.kind == ExprKind::Any;
        line(depth,
             top ? (any ? "If any of these:" : "If all of these:")
                 : (any ? "Any of these:" : "All of these:"),
             e.id);
        for (const Expr& each : e.operands) {
            condition(each, depth + 1, fallback, false);
        }
    }

    void steps(const std::vector<Stmt>& list, int depth) {
        for (const Stmt& s : list) {
            step(s, depth);
        }
    }

    void step(const Stmt& s, int depth) {
        const auto arg = [&s, this](std::size_t i) {
            return i < s.arguments.size() ? expr(s.arguments[i]) : std::string("…");
        };
        switch (s.kind) {
        case StmtKind::Do: {
            const reflect::FunctionInfo* f = members_.find(
                s.object, [&s](const reflect::TypeInfo& t) { return t.find_function(s.member); });
            std::size_t next = 0;
            const std::string said =
                f == nullptr ? s.member
                             : fill_in(reflect::block_text(*f), [&](const std::string& name) {
                                   return arg(parameter_at(*f, name, next));
                               });
            line(depth, said + " (" + who(s.object) + ")", s.id);
            return;
        }
        case StmtKind::Set: {
            const reflect::PropertyInfo* p = members_.find(
                s.object, [&s](const reflect::TypeInfo& t) { return t.find_property(s.member); });
            line(depth,
                 "Set " + inside(p != nullptr ? p->meta.display_name : s.member) + " of " +
                     s.object + " to " + arg(0),
                 s.id);
            return;
        }
        case StmtKind::SetVariable:
            line(depth, "Set " + s.member + " to " + arg(0), s.id);
            return;
        case StmtKind::Wait:
            line(depth, "Wait " + arg(0) + " seconds", s.id);
            return;
        case StmtKind::WaitUntil:
            line(depth, "Wait until " + arg(0), s.id);
            return;
        case StmtKind::If:
            line(depth, "If " + arg(0) + ":", s.id);
            steps(s.body, depth + 1);
            if (!s.otherwise.empty()) {
                line(depth, "Otherwise:", s.id, true);
                steps(s.otherwise, depth + 1);
            }
            return;
        case StmtKind::Repeat:
            line(depth, "Repeat " + arg(0) + " times:", s.id);
            steps(s.body, depth + 1);
            return;
        case StmtKind::ForEachTagged:
            line(depth, "For each object tagged " + s.member + " (as it):", s.id);
            steps(s.body, depth + 1);
            return;
        case StmtKind::Run:
            line(depth, "Run " + s.member + " and wait for it", s.id);
            return;
        case StmtKind::Log:
            line(depth, "Log " + arg(0), s.id);
            return;
        }
    }

    [[nodiscard]] std::string event_text(const std::string& object,
                                         const std::string& event) const {
        if (object.empty()) {
            return event == "game_start" ? std::string("When the game starts")
                   : event == "scene_start"
                       ? std::string("When a scene starts")
                       : "When " + engine::reflect::display_name_from_id(event);
        }
        const reflect::EventInfo* found = members_.find(
            object, [&event](const reflect::TypeInfo& t) { return t.find_event(event); });
        const std::string said =
            found == nullptr ? "When " + event
                             : fill_in(reflect::block_text(*found),
                                       [](const std::string& id) { return "[" + id + "]"; });
        return said + " (" + who(object) + ")";
    }

private:
    Members members_;
    std::vector<LogicLine>& lines_;
};

} // namespace

std::vector<LogicFile> logic_files(Document& document, std::vector<serialize::Issue>& issues) {
    issues.insert(issues.end(), document.logic_issues().begin(), document.logic_issues().end());
    return document.logic();
}

std::string describe_value(const Expr& value, const Document& document) {
    std::vector<LogicLine> unused;
    return Writer(document, unused).expr(value);
}

std::string describe_sum_start(const Expr& sum, const Document& document) {
    std::vector<LogicLine> unused;
    return Writer(document, unused).sum_start(sum);
}

std::string describe_open_sum(const Expr& sum, const Document& document) {
    std::vector<LogicLine> unused;
    return Writer(document, unused).open_start(sum);
}

std::vector<LogicLine> describe(const logic::LogicAsset& asset, const Document& document) {
    std::vector<LogicLine> lines;
    Writer w(document, lines);
    for (const logic::Rule& rule : asset.rules) {
        w.line(0, w.event_text(rule.object, rule.event), rule.id);
        for (const Expr& condition : rule.conditions) {
            w.condition(condition, 1, rule.id, true);
        }
        w.steps(rule.actions, 1);
        if (!rule.otherwise.empty()) {
            w.line(1, "Otherwise:", rule.id, true);
            w.steps(rule.otherwise, 2);
        }
    }
    for (const logic::Sequence& sequence : asset.sequences) {
        w.line(0, "Sequence " + sequence.name, sequence.id);
        w.steps(sequence.steps, 1);
    }
    for (const logic::Brain& brain : asset.brains) {
        w.line(0, "Brain " + brain.name, brain.id);
        for (const logic::BrainState& state : brain.states) {
            w.line(1, "State " + state.name, state.id);
            if (!state.on_enter.empty()) {
                w.line(2, "On enter:", state.id);
                w.steps(state.on_enter, 3);
            }
            if (!state.while_in.empty()) {
                w.line(2, "While in it:", state.id);
                w.steps(state.while_in, 3);
            }
            if (!state.on_exit.empty()) {
                w.line(2, "On exit:", state.id);
                w.steps(state.on_exit, 3);
            }
            for (const logic::Transition& go : state.transitions) {
                std::string when;
                if (go.min_time > 0.0) {
                    when = " after " + engine::text::format_number(go.min_time) + " seconds";
                }
                for (const Expr& condition : go.conditions) {
                    when += (when.empty() ? " if " : " and if ") + w.expr(condition);
                }
                w.line(2, "Go to " + go.to + when, go.id);
            }
        }
    }
    return lines;
}

} // namespace editor
