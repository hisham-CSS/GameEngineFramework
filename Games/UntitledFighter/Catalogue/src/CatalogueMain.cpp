// UntitledFighterCatalogue -- the showcase cooker's CLI (ROADMAP M1.6,
// ADR-011 section 4). All the work is CookCatalogue in CseGame; this file is
// argument handling and an exit code, in AssetCooker's own line-oriented
// idiom so a script or an editor can parse either tool the same way.
//
//   UntitledFighterCatalogue <charactersDir> <outDir>
//
//   OK <entry>: model=<verdict> game=<verdict> replay=<file|none>
//   ERR <entry>: <reason>
//   DONE entries=<n> errors=<n>
//
// Exit 0 when every entry cooked, 1 when any refused, 2 on bad usage.
#include "cse/game/Catalogue.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr,
                     "usage: UntitledFighterCatalogue <charactersDir> <outDir>\n"
                     "  cooks the catalogue manifest at\n"
                     "  <charactersDir>/fighter_a/variants/catalogue.json\n");
        return 2;
    }

    cse::game::CatalogueReport report{};
    cse::game::CookCatalogue(argv[1], "fighter_a/variants/catalogue.json",
                             argv[2], report);

    if (!report.error.empty()) {
        std::fprintf(stderr, "ERR manifest: %s\n", report.error.c_str());
        return 1;
    }

    int errors = 0;
    for (const cse::game::CookedEntry& e : report.entries) {
        if (e.error.empty()) {
            std::printf("OK %s: model=%s game=%s replay=%s\n", e.name.c_str(),
                        e.proverStatus.c_str(), e.searchVerdict.c_str(),
                        e.replayRel.empty() ? "none" : e.replayRel.c_str());
        } else {
            ++errors;
            std::printf("ERR %s: %s\n", e.name.c_str(), e.error.c_str());
        }
    }
    std::printf("DONE entries=%zu errors=%d\n", report.entries.size(), errors);
    return errors == 0 ? 0 : 1;
}
