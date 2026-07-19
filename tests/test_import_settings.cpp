// ImportSettings sidecars (P4-3 phase 4): headless — plain file IO.
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>

#include "Engine.h"

using namespace MyCoreEngine;

TEST(ImportSettings, SidecarPathAppendsImportSuffix) {
    EXPECT_EQ(ImportSettingsPathFor("Exported/tex.png"), "Exported/tex.png.import");
}

TEST(ImportSettings, MissingSidecarYieldsDefaults) {
    const ImportSettings s = LoadImportSettings("no_such_asset.png");
    EXPECT_EQ(s.maxDimension, 0);
}

TEST(ImportSettings, RoundTrips) {
    const char* asset = "import_rt.png"; // asset itself need not exist
    ImportSettings s;
    s.maxDimension = 2048;
    ASSERT_TRUE(SaveImportSettings(asset, s));
    EXPECT_EQ(LoadImportSettings(asset).maxDimension, 2048);

    s.maxDimension = 0; // explicit reset still round-trips
    ASSERT_TRUE(SaveImportSettings(asset, s));
    EXPECT_EQ(LoadImportSettings(asset).maxDimension, 0);
    std::remove(ImportSettingsPathFor(asset).c_str());
}

TEST(ImportSettings, MalformedSidecarYieldsDefaultsNotCrash) {
    const char* asset = "import_bad.png";
    {
        std::ofstream f(ImportSettingsPathFor(asset));
        f << "{ this is not json";
    }
    EXPECT_EQ(LoadImportSettings(asset).maxDimension, 0);
    {
        std::ofstream f(ImportSettingsPathFor(asset));
        f << "[1,2,3]"; // valid json, wrong shape
    }
    EXPECT_EQ(LoadImportSettings(asset).maxDimension, 0);
    {
        std::ofstream f(ImportSettingsPathFor(asset));
        f << "{\"maxDimension\": -512}"; // negative: clamped to 0
    }
    EXPECT_EQ(LoadImportSettings(asset).maxDimension, 0);
    std::remove(ImportSettingsPathFor(asset).c_str());
}
