// THE CANCEL GRAPH, DRAWN (ROADMAP M1.6's cooker slice, ADR-011 section 4:
// "a `graph.dot` of the cancel graph with the loop highlighted").
//
// One pure function: the loaded character plus the prover's verdict in, a
// Graphviz `digraph` out as a string. A STRING and not a file, so the emitter
// is testable byte-for-byte with no filesystem and the cooker owns the one
// write path. It lives in CseData beside ProverAdapter because the things it
// draws -- moves, cancels, the dead list, the loop -- are this library's
// vocabulary; ADR-011 decision 8 pins comboprover.hpp as published-unmodified,
// so the emitter could never live there.
//
// WHAT THE PICTURE SAYS, and the three styles that say it:
//   - every move is a node, labelled `id\nstartup/active/recovery`;
//   - every authored cancel is an edge, labelled with its delay and its `on`
//     value when not the default `hit`;
//   - a DEAD edge (the prover's own list: the follow-up cannot connect at the
//     settled hitstun) is dashed and grey -- authored, named, unusable;
//   - a LOOP edge (consecutive moves of the verdict's loop, wrap included)
//     is red and bold -- the infinite, drawn onto the graph that contains it.
//
// Deterministic by construction: nodes and edges are emitted in FILE ORDER,
// the same fixed order every other reader of this data uses, so the same
// character and verdict produce the same bytes on every machine and a golden
// diff of two catalogue runs means something.
#pragma once

#include "cse/data/CharacterData.h"
#include "cse/data/ProverAdapter.h"

#include <string>

namespace cse::data {

std::string WriteCancelGraphDot(const CharacterData& character,
                                const ProverResult& verdict);

} // namespace cse::data
