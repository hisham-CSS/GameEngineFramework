// comboprover.hpp - deciding whether a fighting game character can loop forever.
//
// A single header C++17 port of the decision procedure in docs/02-formal-model.md,
// written to drop into a game engine's editor. Standard library only, no build
// system changes, no allocation once the search is running beyond what the
// containers do themselves.
//
// The point of having this in the engine rather than only as an offline script is
// that it is fast enough to run while a designer is typing. A cancel table of the
// size a real character has resolves in well under a millisecond, so the editor
// can re-run it on every edit and show the verdict live. Being told that the
// cancel you just authored can never connect, at the moment you author it, is
// feedback no engine currently gives.
//
// Scope. This header analyses a defender pinned in the corner, which is where
// infinites live and is the case a designer most needs checked. It therefore
// does not model the distance between the players, and consequently does not
// model walking forward during a combo to stay in range, because against the
// wall there is no distance to recover. Away from the wall those two things
// decide the answer and the Python reference implementation handles them; see
// sections 6 and 7 of docs/02-formal-model.md. A caller who wants the
// away-from-wall verdict in engine can supply distance as an ordinary resource
// and pre-expand each cancel into one edge per amount of walking the slack
// allows, which is exactly what the reference implementation does internally.
//
// Usage:
//
//     comboprover::Character c;
//     c.resourceNames = {"juggle"};
//     c.initial       = {4};
//     int launcher = c.addMove({"launcher", 9, 3, 20, 30, 60.0f, {-1}, {1}});
//     int slash    = c.addMove({"slash",    6, 3, 12, 22, 40.0f, {-1}, {1}});
//     c.addCancel({launcher, slash, 0});
//     c.addCancel({slash, slash, 0});
//     c.starters = {launcher};
//
//     comboprover::Result r = comboprover::analyse(c);
//     if (r.status == comboprover::Status::Infinite) { /* r.loop is the combo */ }
//
// Licence: same as the rest of the project.

#ifndef COMBOPROVER_HPP
#define COMBOPROVER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace comboprover {

using ResourceVec = std::vector<int>;

// ---------------------------------------------------------------------------
// Hitstun decay
// ---------------------------------------------------------------------------

// How hitstun shrinks as a combo goes on. The two things the theory needs are
// that it never goes up and that it stops changing after a computable number of
// hits. Every scheme below has both properties, and settlingIndex() reports the
// point past which the hit count stops mattering.
struct Decay {
    enum class Kind { None, Linear, Multiplicative, Table };

    Kind kind = Kind::None;
    int step = 0;              // Linear: frames removed per hit already landed
    float ratio = 1.0f;        // Multiplicative: factor per hit already landed
    int floorFrames = 0;       // never decay below this
    std::vector<float> table;  // Table: multiplier by hit index, last repeats

    int hitstun(int base, int hitsSoFar) const {
        const int n = hitsSoFar < 0 ? 0 : hitsSoFar;
        switch (kind) {
            case Kind::None:
                return base;
            case Kind::Linear:
                return std::max(floorFrames, base - step * n);
            case Kind::Multiplicative: {
                float value = static_cast<float>(base);
                for (int i = 0; i < n && value > static_cast<float>(floorFrames); ++i) {
                    value *= ratio;
                }
                return std::max(floorFrames, static_cast<int>(value));
            }
            case Kind::Table: {
                if (table.empty()) return base;
                const std::size_t index =
                    std::min(static_cast<std::size_t>(n), table.size() - 1);
                return std::max(floorFrames,
                                static_cast<int>(static_cast<float>(base) * table[index]));
            }
        }
        return base;
    }

    // The smallest hit count past which hitstun no longer changes for any move.
    int settlingIndex(const std::vector<int>& bases) const {
        if (kind == Kind::None) return 0;
        if (kind == Kind::Table) {
            return table.empty() ? 0 : static_cast<int>(table.size()) - 1;
        }
        int settled = 0;
        for (int base : bases) {
            int n = 0;
            // Halts because the sequence never increases and is bounded below.
            while (hitstun(base, n) != hitstun(base, n + 1) && n < 4096) ++n;
            settled = std::max(settled, n);
        }
        return settled;
    }
};

// ---------------------------------------------------------------------------
// Moves and cancels
// ---------------------------------------------------------------------------

struct Move {
    std::string id;
    int startup = 1;    // frames before it can first connect
    int active = 1;     // frames during which it can connect
    int recovery = 0;   // frames after it stops being dangerous
    int hitstun = 0;    // frames the defender is frozen on hit, before decay
    float damage = 0.0f;
    ResourceVec effect;  // how performing it changes each resource; negative spends
    ResourceVec guard;   // minimum each resource must be for it to be allowed

    int duration() const { return startup + active + recovery; }
};

// An authored edge from one move to a follow-up.
//
// `delay` is the actionable delay: frames between the source connecting and the
// attacker being allowed to start the follow-up along this edge. Zero is a true
// cancel. active + recovery - 1 is a link, meaning the source is waited out.
struct Cancel {
    int from = 0;
    int to = 0;
    int delay = 0;
    bool onHit = true;  // edges available only on block or whiff cannot continue a combo
    ResourceVec effect;
    ResourceVec guard;
};

struct Character {
    std::string name;
    std::vector<Move> moves;
    std::vector<Cancel> cancels;
    std::vector<std::string> resourceNames;
    ResourceVec initial;
    Decay decay;
    std::vector<int> starters;   // empty means any move may open a combo
    std::vector<float> scaling;  // damage multiplier by hit index, last repeats

    int addMove(Move m) {
        moves.push_back(std::move(m));
        return static_cast<int>(moves.size()) - 1;
    }
    int addCancel(Cancel c) {
        cancels.push_back(std::move(c));
        return static_cast<int>(cancels.size()) - 1;
    }
    std::size_t resourceCount() const { return resourceNames.size(); }

    float damageMultiplier(int hitsSoFar) const {
        if (scaling.empty()) return 1.0f;
        const std::size_t index = std::min(static_cast<std::size_t>(std::max(0, hitsSoFar)),
                                           scaling.size() - 1);
        return scaling[index];
    }
};

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

enum class Status { Terminating, Infinite, Unknown };

// A cancel the designer authored that can never connect: by the time the
// follow-up becomes dangerous, the defender is already free.
struct DeadCancel {
    int cancelIndex = 0;
    int advantage = 0;
    int startup = 0;
    int shortfall() const { return startup - advantage; }
};

struct Result {
    Status status = Status::Unknown;
    std::string reason;

    // Set when status is Infinite. `prefix` is how to reach the loop from the
    // opening move and `loop` is the loop itself, both as move indices.
    std::vector<int> prefix;
    std::vector<int> loop;

    // Set when status is Terminating. Resource indices, read in order: if the
    // first has not changed look at the second, and so on. Each one runs down
    // and none can go below zero, which is why no combo goes on forever.
    std::vector<int> rankingOrder;
    bool hasRanking = false;

    // Set when status is Terminating.
    int maxHits = 0;
    int maxFrames = 0;
    float maxDamage = 0.0f;

    // Reported whatever the verdict.
    std::vector<DeadCancel> deadCancels;
    std::vector<int> unreachableMoves;
    int settlingIndex = 0;
    int usableCancels = 0;

    int explored = 0;
    bool capped = false;
    bool spendOnly = true;
};

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

namespace detail {

struct Config {
    int move = 0;
    int hits = 0;
    ResourceVec res;

    bool operator==(const Config& other) const {
        return move == other.move && hits == other.hits && res == other.res;
    }
};

struct ConfigHash {
    std::size_t operator()(const Config& c) const noexcept {
        std::size_t h = static_cast<std::size_t>(c.move) * 2654435761u;
        h ^= static_cast<std::size_t>(c.hits) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        for (int value : c.res) {
            h ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        }
        return h;
    }
};

inline ResourceVec applyEffect(const ResourceVec& base, const ResourceVec& effect) {
    ResourceVec out = base;
    const std::size_t n = std::min(out.size(), effect.size());
    for (std::size_t i = 0; i < n; ++i) out[i] += effect[i];
    return out;
}

inline bool meetsGuard(const ResourceVec& have, const ResourceVec& guard) {
    const std::size_t n = std::min(have.size(), guard.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (have[i] < guard[i]) return false;
    }
    return true;
}

inline bool nonNegative(const ResourceVec& v) {
    for (int value : v) {
        if (value < 0) return false;
    }
    return true;
}

// The self-covering test. Coming back to the same move having spent nothing is
// an infinite combo; coming back having gained is just as fatal, because each
// repeat then starts from a better position than the last.
inline bool covers(const ResourceVec& later, const ResourceVec& earlier) {
    const std::size_t n = std::min(later.size(), earlier.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (later[i] < earlier[i]) return false;
    }
    return true;
}

inline ResourceVec totalEffect(const Character& c, const Cancel& edge) {
    ResourceVec out(c.resourceCount(), 0);
    const std::size_t n = out.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i < edge.effect.size()) out[i] += edge.effect[i];
        if (i < c.moves[edge.to].effect.size()) out[i] += c.moves[edge.to].effect[i];
    }
    return out;
}

inline ResourceVec totalGuard(const Character& c, const Cancel& edge) {
    ResourceVec out(c.resourceCount(), 0);
    const std::size_t n = out.size();
    for (std::size_t i = 0; i < n; ++i) {
        const int a = i < edge.guard.size() ? edge.guard[i] : 0;
        const int b = i < c.moves[edge.to].guard.size() ? c.moves[edge.to].guard[i] : 0;
        out[i] = std::max(a, b);
    }
    return out;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// The decision procedure
// ---------------------------------------------------------------------------

// Answer the infinite combo question for one character.
//
// `limit` bounds how many positions the search will visit. Reaching it produces
// Status::Unknown rather than a guess, because an unfinished search dressed up
// as a clean bill of health is worse than no answer at all.
inline Result analyse(const Character& character, int limit = 200000) {
    using namespace detail;

    Result result;
    const std::size_t moveCount = character.moves.size();
    const std::size_t resCount = character.resourceCount();

    // --- settling point, and the graph of cancels that can actually connect ---

    std::vector<int> bases;
    bases.reserve(moveCount);
    for (const Move& m : character.moves) bases.push_back(m.hitstun);
    const int settling = character.decay.settlingIndex(bases);
    result.settlingIndex = settling;

    int hitCap = settling + 1;
    if (!character.scaling.empty()) {
        hitCap = std::max(hitCap, static_cast<int>(character.scaling.size()));
    }

    std::vector<std::vector<int>> outgoing(moveCount);
    for (std::size_t i = 0; i < character.cancels.size(); ++i) {
        const Cancel& edge = character.cancels[i];
        if (!edge.onHit) continue;
        const int settledStun =
            character.decay.hitstun(character.moves[edge.from].hitstun, settling);
        const int advantage = settledStun - edge.delay;
        const int startup = character.moves[edge.to].startup;
        if (startup <= advantage) {
            outgoing[edge.from].push_back(static_cast<int>(i));
            ++result.usableCancels;
        } else {
            result.deadCancels.push_back({static_cast<int>(i), advantage, startup});
        }
    }

    std::vector<int> starters = character.starters;
    if (starters.empty()) {
        starters.resize(moveCount);
        for (std::size_t i = 0; i < moveCount; ++i) starters[i] = static_cast<int>(i);
    }

    // --- which moves any combo can produce at all ---

    std::vector<bool> reachable(moveCount, false);
    {
        std::vector<int> stack = starters;
        while (!stack.empty()) {
            const int move = stack.back();
            stack.pop_back();
            if (reachable[move]) continue;
            reachable[move] = true;
            for (int edgeIndex : outgoing[move]) {
                const int target = character.cancels[edgeIndex].to;
                if (!reachable[target]) stack.push_back(target);
            }
        }
    }
    for (std::size_t i = 0; i < moveCount; ++i) {
        if (!reachable[i]) result.unreachableMoves.push_back(static_cast<int>(i));
    }

    for (std::size_t m = 0; m < moveCount && result.spendOnly; ++m) {
        for (int edgeIndex : outgoing[m]) {
            const ResourceVec effect = totalEffect(character, character.cancels[edgeIndex]);
            for (int value : effect) {
                if (value > 0) {
                    result.spendOnly = false;
                    break;
                }
            }
            if (!result.spendOnly) break;
        }
    }

    // --- walk the reachable positions, watching for one that covers an ancestor ---

    std::vector<Config> configs;
    std::vector<std::vector<std::pair<int, int>>> edgesOut;  // (cancel index, config index)
    std::unordered_map<Config, int, ConfigHash> seen;

    auto intern = [&](const Config& c) -> int {
        auto found = seen.find(c);
        if (found != seen.end()) return found->second;
        const int index = static_cast<int>(configs.size());
        configs.push_back(c);
        edgesOut.emplace_back();
        seen.emplace(c, index);
        return index;
    };

    ResourceVec startResources = character.initial;
    startResources.resize(resCount, 0);

    std::vector<int> startConfigs;
    for (int starter : starters) {
        const Move& move = character.moves[starter];
        if (!meetsGuard(startResources, move.guard)) continue;
        ResourceVec after = applyEffect(startResources, move.effect);
        if (!nonNegative(after)) continue;
        Config c;
        c.move = starter;
        c.hits = std::min(1, hitCap);
        c.res = std::move(after);
        startConfigs.push_back(intern(c));
    }

    std::vector<bool> expanded;
    auto expand = [&](int configIndex) {
        if (configIndex < static_cast<int>(expanded.size()) && expanded[configIndex]) return;
        if (static_cast<int>(expanded.size()) <= configIndex) expanded.resize(configIndex + 1, false);
        expanded[configIndex] = true;

        const Config source = configs[configIndex];
        const int stun =
            character.decay.hitstun(character.moves[source.move].hitstun, std::max(0, source.hits - 1));

        for (int edgeIndex : outgoing[source.move]) {
            const Cancel& edge = character.cancels[edgeIndex];
            if (character.moves[edge.to].startup > stun - edge.delay) continue;
            if (!meetsGuard(source.res, totalGuard(character, edge))) continue;
            ResourceVec after = applyEffect(source.res, totalEffect(character, edge));
            if (!nonNegative(after)) continue;
            Config next;
            next.move = edge.to;
            next.hits = std::min(source.hits + 1, hitCap);
            next.res = std::move(after);
            // Intern first, into a local. Writing `edgesOut[i].emplace_back(e,
            // intern(next))` would be a bug: the subscript is evaluated before
            // the argument, and intern can grow edgesOut, so the reference
            // would dangle by the time it is written through.
            const int target = intern(next);
            edgesOut[configIndex].emplace_back(edgeIndex, target);
        }
    };

    enum { White = 0, Grey = 1, Black = 2 };
    std::vector<std::uint8_t> colour;
    auto colourOf = [&](int index) -> std::uint8_t {
        return index < static_cast<int>(colour.size()) ? colour[index] : White;
    };
    auto setColour = [&](int index, std::uint8_t value) {
        if (static_cast<int>(colour.size()) <= index) colour.resize(index + 1, White);
        colour[index] = value;
    };

    struct Frame {
        int config;
        std::size_t position;
        int arrivedBy;  // cancel index, or -1 for an opening move
    };

    bool found = false;
    std::vector<int> witnessPrefix, witnessLoop;

    for (int start : startConfigs) {
        if (found) break;
        if (colourOf(start) != White) continue;

        std::vector<Frame> path{{start, 0, -1}};
        setColour(start, Grey);
        // Ancestors on the current path, keyed by move and hit count so the
        // covering test only looks at positions that could possibly match.
        std::unordered_map<std::int64_t, std::vector<int>> ancestors;
        auto key = [](int move, int hits) -> std::int64_t {
            return (static_cast<std::int64_t>(move) << 32) | static_cast<std::uint32_t>(hits);
        };
        ancestors[key(configs[start].move, configs[start].hits)].push_back(0);

        while (!path.empty()) {
            if (static_cast<int>(configs.size()) > limit) {
                result.capped = true;
                break;
            }

            Frame& frame = path.back();
            expand(frame.config);
            const auto& steps = edgesOut[frame.config];

            if (frame.position < steps.size()) {
                const auto step = steps[frame.position];
                ++frame.position;
                const int nextIndex = step.second;
                const Config& next = configs[nextIndex];

                int match = -1;
                auto candidates = ancestors.find(key(next.move, next.hits));
                if (candidates != ancestors.end()) {
                    for (int position : candidates->second) {
                        if (covers(next.res, configs[path[position].config].res)) {
                            match = position;
                            break;
                        }
                    }
                }

                if (match >= 0) {
                    for (int i = 1; i <= match; ++i) {
                        witnessPrefix.push_back(configs[path[i].config].move);
                    }
                    for (std::size_t i = static_cast<std::size_t>(match) + 1; i < path.size(); ++i) {
                        witnessLoop.push_back(configs[path[i].config].move);
                    }
                    witnessLoop.push_back(next.move);
                    found = true;
                    break;
                }

                if (colourOf(nextIndex) == White) {
                    setColour(nextIndex, Grey);
                    ancestors[key(next.move, next.hits)].push_back(static_cast<int>(path.size()));
                    path.push_back({nextIndex, 0, step.first});
                }
            } else {
                setColour(frame.config, Black);
                const std::int64_t k = key(configs[frame.config].move, configs[frame.config].hits);
                auto entry = ancestors.find(k);
                if (entry != ancestors.end() && !entry->second.empty() &&
                    entry->second.back() == static_cast<int>(path.size()) - 1) {
                    entry->second.pop_back();
                }
                path.pop_back();
            }
        }
        if (result.capped) break;
    }

    result.explored = static_cast<int>(configs.size());

    if (found) {
        result.status = Status::Infinite;
        result.prefix = std::move(witnessPrefix);
        result.loop = std::move(witnessLoop);
        result.reason = "a loop returns to the same move with no resource lower "
                        "than it started, so it can be repeated indefinitely";
        return result;
    }

    if (result.capped) {
        result.status = Status::Unknown;
        result.reason = "the search hit its limit before finishing; raise it to get an answer";
        return result;
    }

    result.status = Status::Terminating;
    result.reason = "every reachable position leads eventually to a dead end";

    // --- the certificate: which resources run down, and in what order ---

    if (result.spendOnly && resCount > 0) {
        std::vector<char> live(character.cancels.size(), 0);
        for (std::size_t m = 0; m < moveCount; ++m) {
            if (!reachable[m]) continue;
            for (int edgeIndex : outgoing[m]) {
                if (reachable[character.cancels[edgeIndex].to]) live[edgeIndex] = 1;
            }
        }

        std::vector<char> used(resCount, 0);
        bool progressing = true;
        while (progressing) {
            progressing = false;
            // Does anything still loop using only the edges left?
            std::vector<int> degree(moveCount, 0);
            bool anyEdge = false;
            for (std::size_t i = 0; i < character.cancels.size(); ++i) {
                if (live[i]) {
                    anyEdge = true;
                    break;
                }
            }
            if (!anyEdge) break;
            (void)degree;

            for (std::size_t r = 0; r < resCount; ++r) {
                if (used[r]) continue;
                bool anyDown = false, anyUp = false;
                for (std::size_t i = 0; i < character.cancels.size(); ++i) {
                    if (!live[i]) continue;
                    const ResourceVec effect = totalEffect(character, character.cancels[i]);
                    if (r < effect.size()) {
                        if (effect[r] < 0) anyDown = true;
                        if (effect[r] > 0) anyUp = true;
                    }
                }
                if (anyDown && !anyUp) {
                    used[r] = 1;
                    result.rankingOrder.push_back(static_cast<int>(r));
                    for (std::size_t i = 0; i < character.cancels.size(); ++i) {
                        if (!live[i]) continue;
                        const ResourceVec effect = totalEffect(character, character.cancels[i]);
                        if (r < effect.size() && effect[r] < 0) live[i] = 0;
                    }
                    progressing = true;
                    break;
                }
            }
        }
        result.hasRanking = !result.rankingOrder.empty();
    }

    // --- worst case, by longest path through a graph that has no cycles ---

    {
        const std::size_t n = configs.size();
        for (std::size_t i = 0; i < n; ++i) expand(static_cast<int>(i));

        std::vector<int> incoming(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            for (const auto& step : edgesOut[i]) ++incoming[step.second];
        }
        std::vector<int> ready;
        for (std::size_t i = 0; i < n; ++i) {
            if (incoming[i] == 0) ready.push_back(static_cast<int>(i));
        }

        std::vector<int> order;
        order.reserve(n);
        while (!ready.empty()) {
            const int index = ready.back();
            ready.pop_back();
            order.push_back(index);
            for (const auto& step : edgesOut[index]) {
                if (--incoming[step.second] == 0) ready.push_back(step.second);
            }
        }

        if (order.size() == n) {
            const float negative = -std::numeric_limits<float>::max();
            std::vector<float> bestHits(n, negative), bestFrames(n, negative),
                bestDamage(n, negative);
            for (int index : startConfigs) {
                const Config& c = configs[index];
                bestHits[index] = std::max(bestHits[index], 1.0f);
                bestFrames[index] =
                    std::max(bestFrames[index], static_cast<float>(character.moves[c.move].startup));
                bestDamage[index] = std::max(
                    bestDamage[index], character.moves[c.move].damage * character.damageMultiplier(0));
            }
            for (int index : order) {
                if (bestHits[index] == negative) continue;
                for (const auto& step : edgesOut[index]) {
                    const Cancel& edge = character.cancels[step.first];
                    const Move& target = character.moves[edge.to];
                    const int to = step.second;
                    bestHits[to] = std::max(bestHits[to], bestHits[index] + 1.0f);
                    bestFrames[to] = std::max(
                        bestFrames[to],
                        bestFrames[index] + static_cast<float>(edge.delay + target.startup));
                    bestDamage[to] = std::max(
                        bestDamage[to],
                        bestDamage[index] +
                            target.damage * character.damageMultiplier(configs[index].hits));
                }
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (bestHits[i] == negative) continue;
                result.maxHits = std::max(result.maxHits, static_cast<int>(bestHits[i]));
                result.maxFrames = std::max(result.maxFrames, static_cast<int>(bestFrames[i]));
                result.maxDamage = std::max(result.maxDamage, bestDamage[i]);
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

inline std::string describe(const Character& character, const Result& result) {
    std::string out;
    out += character.name.empty() ? "character" : character.name;
    out += "\n  verdict   ";
    switch (result.status) {
        case Status::Terminating: out += "TERMINATING"; break;
        case Status::Infinite:    out += "INFINITE COMBO"; break;
        case Status::Unknown:     out += "UNRESOLVED"; break;
    }
    out += "\n  because   " + result.reason;
    out += "\n  model     " + std::to_string(character.moves.size()) + " moves, " +
           std::to_string(result.usableCancels) + " usable cancels, " +
           std::string(result.spendOnly ? "spend-only" : "general") + " fragment";

    if (result.status == Status::Infinite) {
        out += "\n  loop      ";
        for (std::size_t i = 0; i < result.loop.size(); ++i) {
            if (i) out += " > ";
            out += character.moves[result.loop[i]].id;
        }
    }

    if (result.status == Status::Terminating) {
        if (result.hasRanking) {
            out += "\n  ends because these run down, in order: ";
            for (std::size_t i = 0; i < result.rankingOrder.size(); ++i) {
                if (i) out += ", ";
                out += character.resourceNames[result.rankingOrder[i]];
            }
        }
        out += "\n  worst case " + std::to_string(result.maxHits) + " hits, " +
               std::to_string(result.maxFrames) + " frames, " +
               std::to_string(static_cast<int>(result.maxDamage)) + " damage";
    }

    for (const DeadCancel& dead : result.deadCancels) {
        const Cancel& edge = character.cancels[dead.cancelIndex];
        out += "\n  dead cancel: " + character.moves[edge.from].id + " -> " +
               character.moves[edge.to].id + " leaves " + std::to_string(dead.advantage) +
               " but needs " + std::to_string(dead.startup) + ", short by " +
               std::to_string(dead.shortfall());
    }
    for (int move : result.unreachableMoves) {
        out += "\n  unreachable: no combo can produce " + character.moves[move].id;
    }

    out += "\n";
    return out;
}

}  // namespace comboprover

#endif  // COMBOPROVER_HPP
