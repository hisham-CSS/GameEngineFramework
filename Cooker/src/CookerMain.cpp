// AssetCooker — the engine's headless asset-work process (P4-3 phase 4).
//
// The out-of-process half of the asset pipeline, ShaderCompileWorker-style:
// the editor (or CI) spawns it for batch work, so a hostile asset takes
// down the cooker, not the editor, and long jobs use the whole machine
// without touching the editor's frame loop. It links Engine.dll but never
// initializes GL — every operation is CPU-only by design.
//
// Protocol (stdout, line-oriented — the editor parses this):
//   OK   <path>            per file with no findings (verbose mode only)
//   WARN <path>: <reason>
//   ERR  <path>: <reason>
//   DONE models=<n> textures=<n> warnings=<n> errors=<n>
// Exit code: 0 = clean (warnings allowed), 1 = errors found, 2 = bad usage.
//
// Commands:
//   validate <root>   decode every model (parallel), check every texture
//                     against its .import settings; report what would load
//                     wrong or wastefully at runtime.
// Planned (NOT implemented): cook-textures (BC7/mips to ImportSettings,
// lands with P4-2 alongside engine DDS loading — atomic tmp+rename output
// arrives with it), cook-meshes, pack.
#include "Engine.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace MyCoreEngine;

namespace {

int usage()
{
    std::fprintf(stderr,
        "AssetCooker - headless asset validation/cooking\n"
        "usage:\n"
        "  AssetCooker validate <assetRoot>\n");
    return 2;
}

int runValidate(const char* root)
{
    // a validation gate must FAIL CLOSED: a typo'd root (or wrong CWD)
    // would otherwise scan zero assets and report a clean exit 0
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        std::printf("ERR  %s: asset root not found (cwd-relative?)\n", root);
        return 2;
    }

    JobSystem jobs; // dedicated pool; ValidateAssetTree pumps it itself
    const AssetValidationReport report = ValidateAssetTree(root, jobs);

    int warnings = 0;
    for (const auto& issue : report.issues) {
        const bool err = issue.level == AssetValidationIssue::Level::Err;
        if (!err) ++warnings;
        std::printf("%s %s: %s\n", err ? "ERR " : "WARN",
                    issue.path.c_str(), issue.message.c_str());
    }
    std::printf("DONE models=%d textures=%d warnings=%d errors=%d\n",
                report.modelsChecked, report.texturesChecked,
                warnings, report.errorCount());
    return report.errorCount() > 0 ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "validate") == 0) {
        return runValidate(argv[2]);
    }
    return usage();
}
