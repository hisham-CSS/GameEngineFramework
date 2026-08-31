#include "cse/data/CancelGraphDot.h"

#include <set>
#include <string>
#include <utility>

namespace cse::data {
namespace {

// Move ids come from authored JSON and land inside double quotes in the dot
// output; the two characters that could break the quoting are escaped and
// everything else passes through -- a move id is already loader-vetted text.
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

const char* contactName(Contact on) {
    switch (on) {
        case Contact::Hit:    return "hit";
        case Contact::Block:  return "block";
        case Contact::Whiff:  return "whiff";
        case Contact::Always: return "always";
    }
    return "?";
}

} // namespace

std::string WriteCancelGraphDot(const CharacterData& character,
                                const ProverResult& verdict) {
    // The loop's edges as (from,to) move-index pairs, wrap included -- the
    // verdict's own sequence, so the highlight cannot drift from the claim.
    std::set<std::pair<MoveIndex, MoveIndex>> loopEdges;
    std::set<MoveIndex>                       loopMoves;
    if (verdict.status == ProverStatus::Infinite && !verdict.loop.empty()) {
        for (std::size_t i = 0; i < verdict.loop.size(); ++i) {
            const MoveIndex from = verdict.loop[i];
            const MoveIndex to =
                verdict.loop[(i + 1) % verdict.loop.size()];
            loopEdges.insert({ from, to });
            loopMoves.insert(from);
        }
    }

    std::set<CancelIndex> dead;
    for (const ProverDeadCancel& d : verdict.deadCancels) dead.insert(d.cancel);

    std::string out;
    out += "digraph cancels {\n";
    out += "  rankdir=LR;\n";
    out += "  node [shape=box, fontname=\"monospace\"];\n";

    for (const Move& m : character.moves) {
        out += "  " + quoted(m.id) + " [label=" +
               quoted(m.id + "\\n" + std::to_string(m.startup) + "/" +
                      std::to_string(m.active) + "/" +
                      std::to_string(m.recovery));
        const MoveIndex mi = character.FindMove(m.id);
        if (loopMoves.count(mi) != 0)
            out += ", color=red, penwidth=2";
        out += "];\n";
    }

    for (std::size_t i = 0; i < character.cancels.size(); ++i) {
        const Cancel& e = character.cancels[i];
        if (e.from == kInvalidMove || e.to == kInvalidMove) continue;
        if (static_cast<std::size_t>(e.from) >= character.moves.size()) continue;
        if (static_cast<std::size_t>(e.to) >= character.moves.size()) continue;

        std::string label = "+" + std::to_string(e.delay);
        if (e.on != Contact::Hit)
            label += std::string(" ") + contactName(e.on);

        out += "  " + quoted(character.moves[e.from].id) + " -> " +
               quoted(character.moves[e.to].id) + " [label=" + quoted(label);
        if (loopEdges.count({ e.from, e.to }) != 0) {
            out += ", color=red, penwidth=2";
        } else if (dead.count(static_cast<CancelIndex>(i)) != 0) {
            // Dead by the prover's own list: authored, named, unusable at the
            // settled hitstun. Dashed grey so the eye reads "here but off".
            out += ", style=dashed, color=gray50";
        }
        out += "];\n";
    }

    out += "}\n";
    return out;
}

} // namespace cse::data
