// Can the bundle the Build action just produced actually start?
//
// WHY THIS FILE EXISTS, and it is not a hypothetical. A build from the editor
// produced a bundle containing Engine.dll, lua-c++.dll and none of the other
// fifteen runtime libraries. The install reported success, the pipeline's
// validate phase reported success, the Build panel reported success, and
// double-clicking the result produced a Windows loader dialog naming glfw3.dll.
//
// The cause was vcpkg's install-time applocal hook shelling out to `dumpbin`,
// which is on PATH only inside a Visual Studio developer environment -- and the
// editor spawns `cmake --install` from a GUI process that has none. applocal
// reported the problem with a PowerShell Write-Error, which does not fail an
// install, so every layer above it saw success.
//
// The install rule was fixed. This tests the BACKSTOP, and the backstop is the
// part that matters, because the failure above was not that a copy went wrong --
// it was that NOTHING ASKED whether the copy had worked.
//
// MissingRuntimeLibraries is a free function precisely so this file can exist.
// The check it replaced lived inside a private method of a class that owns a
// thread and spawns child processes, where it could be compiled and never
// proven. A check written to catch a silent failure has to be provable, or it is
// one more thing nobody is asking.
//
// NOTHING HERE RUNS A BUILD. No toolchain, no child process, no build tree --
// two directories of empty files and a pure function over them, which is the
// same discipline test_build_settings.cpp states for itself one file over.

#include <gtest/gtest.h>

#include "Engine.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A unique scratch directory per test, removed on the way out. Named after the
// test so a leftover directory after a crash says which one left it.
class Scratch {
public:
    explicit Scratch(const std::string& name) {
        root_ = fs::temp_directory_path() / ("cse_bundle_" + name);
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "bundle", ec);
        fs::create_directories(root_ / "staged", ec);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    std::string bundle() const { return (root_ / "bundle").string(); }
    std::string staged() const { return (root_ / "staged").string(); }

    // The CONTENT is irrelevant and deliberately so: this function compares
    // names, because the question it answers is "did the copy happen", not "is
    // this the same binary". Anything stronger would need a hash of a file the
    // install is allowed to have relinked.
    void put(const char* where, const std::string& name) const {
        std::ofstream f(root_ / where / name, std::ios::binary);
        f << "not a real dll";
    }

private:
    fs::path root_;
};

std::vector<std::string> missingIn(const Scratch& s) {
    return MyCoreEngine::MissingRuntimeLibraries(s.bundle(), s.staged());
}

}  // namespace

TEST(BundleValidate, ACompleteBundleIsMissingNothing) {
    Scratch s("complete");
    for (const char* n : { "Engine.dll", "glfw3.dll", "assimp-vc143-mt.dll" }) {
        s.put("staged", n);
        s.put("bundle", n);
    }

    EXPECT_TRUE(missingIn(s).empty())
        << "a bundle holding every staged DLL reported something missing, so "
           "this check would fail every correct build -- which is how a check "
           "gets deleted.";
}

TEST(BundleValidate, TheOneThatDidNotMakeItIsNamed) {
    // The shipped failure in miniature: Engine.dll got there because it has an
    // explicit install rule, glfw3.dll did not because it relied on applocal.
    Scratch s("named");
    s.put("staged", "Engine.dll");
    s.put("staged", "glfw3.dll");
    s.put("bundle", "Engine.dll");

    const std::vector<std::string> missing = missingIn(s);

    ASSERT_EQ(missing.size(), 1u)
        << "expected exactly one missing library.";
    EXPECT_EQ(missing[0], "glfw3.dll")
        << "the check noticed something was wrong but named the wrong file. A "
           "loader error already fails to name a file; a diagnostic that names "
           "the wrong one is worse than none.";
}

TEST(BundleValidate, TheRealFailureIsCaughtWhole) {
    // Two of seventeen, which is what actually shipped.
    Scratch s("whole");
    const char* all[] = {
        "Engine.dll", "lua-c++.dll", "glfw3.dll", "assimp-vc143-mt.dll",
        "draco.dll", "lua.dll", "meshoptimizer.dll", "minizip.dll",
        "PhysXCommon_64.dll", "PhysXCooking_64.dll", "PhysXDevice64.dll",
        "PhysXFoundation_64.dll", "PhysXGpu_64.dll", "PhysX_64.dll",
        "poly2tri.dll", "pugixml.dll", "zlib1.dll",
    };
    for (const char* n : all) s.put("staged", n);
    s.put("bundle", "Engine.dll");
    s.put("bundle", "lua-c++.dll");

    const std::vector<std::string> missing = missingIn(s);
    EXPECT_EQ(missing.size(), 15u)
        << "the bundle that shipped had 2 of 17 libraries and this check must "
           "account for all 15 that were absent.";
}

TEST(BundleValidate, TheOrderIsStableSoTwoMachinesPrintTheSameMessage) {
    Scratch s("order");
    for (const char* n : { "zlib1.dll", "assimp.dll", "glfw3.dll" }) {
        s.put("staged", n);
    }

    const std::vector<std::string> missing = missingIn(s);
    ASSERT_EQ(missing.size(), 3u);
    EXPECT_EQ(missing[0], "assimp.dll");
    EXPECT_EQ(missing[1], "glfw3.dll");
    EXPECT_EQ(missing[2], "zlib1.dll")
        << "directory iteration order is unspecified, so a message built from it "
           "cannot be diffed against a colleague's. Sorting is what makes the "
           "failure text a fact rather than a coincidence.";
}

TEST(BundleValidate, OnlyLibrariesCount) {
    // The staged directory also holds the executables, their PDBs and whatever
    // else the build put there. A bundle is not expected to carry those, and a
    // check that demanded them would fire on every correct build.
    Scratch s("onlylibs");
    s.put("staged", "PlayerDebug.exe");
    s.put("staged", "Engine.pdb");
    s.put("staged", "Engine.lib");
    s.put("staged", "Engine.dll");
    s.put("bundle", "Engine.dll");

    EXPECT_TRUE(missingIn(s).empty())
        << "something other than a .dll was reported missing. The exe is "
           "installed by its own rule and checked by its own step; PDBs and "
           "import libraries are not shipped at all.";
}

TEST(BundleValidate, ExtensionCaseDoesNotHideAFile) {
    Scratch s("case");
    s.put("staged", "SHOUTY.DLL");

    const std::vector<std::string> missing = missingIn(s);
    ASSERT_EQ(missing.size(), 1u)
        << "a .DLL was skipped because of its case. Windows ships both "
           "spellings, and a case-sensitive test quietly stops checking half of "
           "what is there.";
    EXPECT_EQ(missing[0], "SHOUTY.DLL");
}

TEST(BundleValidate, NoStagedDirectoryClaimsNothing) {
    // With no source of truth there is nothing this function can honestly say.
    // Reporting "everything is missing" here would turn a misconfigured path
    // into a build failure that names the wrong problem.
    Scratch s("nostaged");
    s.put("bundle", "Engine.dll");

    const std::vector<std::string> missing =
        MyCoreEngine::MissingRuntimeLibraries(s.bundle(),
                                              s.staged() + "_does_not_exist");
    EXPECT_TRUE(missing.empty())
        << "a missing staged directory produced a list of missing libraries. "
           "That is a guess, not a measurement.";
}

TEST(BundleValidate, NoBundleAtAllIsEverythingMissing) {
    Scratch s("nobundle");
    s.put("staged", "Engine.dll");
    s.put("staged", "glfw3.dll");

    const std::vector<std::string> missing =
        MyCoreEngine::MissingRuntimeLibraries(s.bundle() + "_does_not_exist",
                                              s.staged());
    EXPECT_EQ(missing.size(), 2u)
        << "an absent bundle directory must report everything as missing -- that "
           "one IS the truth, and it is the rollback case: a build cancelled "
           "part way through leaves no bundle and must not validate.";
}
