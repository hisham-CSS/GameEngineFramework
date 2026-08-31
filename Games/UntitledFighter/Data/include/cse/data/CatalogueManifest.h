// THE SHOWCASE CATALOGUE'S MANIFEST (ROADMAP M1.6's cooker slice).
//
// The catalogue was eight variant files and one unpatched base, enumerated by
// nothing: every exhibit test hard-coded its own path, and the two per-entry
// binding deviations that make an exhibit measurable at all -- meter_loop's
// super_beam chord, microwalk's solo stand_lp binding -- lived only as C++
// literals inside individual tests. A cooker cannot iterate C++ literals, so
// this file is the machine-readable row list: `catalogue.json` beside the
// variants, one entry per exhibit, each carrying the ONE thing the variant
// file itself cannot say -- how the exhibit is bound.
//
// It lives in CseData because it is authored content read from the character
// tree: same sandboxed file read, same nlohmann parse, same refuse-don't-guess
// rules as everything else this library loads. The base row is the entry with
// no `variant`, which is also how the catalogue finally gets its `base` line.
#pragma once

#include "cse/data/CharacterData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cse::data {

struct CatalogueBinding {
    std::string   moveId;
    std::uint16_t buttons = 0;   // kernel input bits, mapped from button names
};

struct CatalogueEntry {
    std::string name;

    // Relative path of the variant patch under the characters directory;
    // EMPTY means the base file itself -- the row every patch is a diff
    // against.
    std::string variantRel;

    // EMPTY means the arcade normals binding (stand_/crouch_/air_ prefixes
    // crossed with the six buttons); a move id here means THAT move bound to
    // LP alone and nothing else -- the microwalk exhibit's shape, where a
    // full-roster search drowns in unrelated strings.
    std::string soloBindingMove;

    // Bindings appended ON TOP of the normals -- meter_loop's super_beam
    // chord. Ignored when soloBindingMove is set.
    std::vector<CatalogueBinding> extraBindings;
};

struct CatalogueManifest {
    std::string                 baseFile;   // e.g. "fighter_a.json"
    std::vector<CatalogueEntry> entries;
};

// False with `error` naming the entry and the key. Unknown button names,
// missing names, an empty entry list and an unreadable file are refusals --
// a manifest that silently cooked fewer rows than it lists is the worst kind
// of green.
bool LoadCatalogueManifest(const std::string& charactersDir,
                           const std::string& relPath,
                           CatalogueManifest& out,
                           std::string& error);

} // namespace cse::data
