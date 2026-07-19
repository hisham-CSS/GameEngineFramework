// AssetValidator (P4-3 phase 4, the cooker's validate command): headless —
// Model::Decode is CPU-only and texture checks use stbi_info.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Engine.h"

using namespace MyCoreEngine;
namespace fs = std::filesystem;

namespace {

const char* kRoot = "validator_test_root";

void writeTinyBMP(const fs::path& path) {
    // 2x2 24bpp BMP (bottom-up, rows padded to 4 bytes)
    std::ofstream bmp(path, std::ios::binary);
    unsigned char hdr[54] = {};
    hdr[0]='B'; hdr[1]='M';
    auto put32 = [&](int off, unsigned int v) {
        hdr[off]=(unsigned char)v; hdr[off+1]=(unsigned char)(v>>8);
        hdr[off+2]=(unsigned char)(v>>16); hdr[off+3]=(unsigned char)(v>>24);
    };
    put32(2, 54 + 16); put32(10, 54); put32(14, 40);
    put32(18, 2); put32(22, 2); hdr[26]=1; hdr[28]=24; put32(34, 16);
    bmp.write((const char*)hdr, 54);
    const unsigned char rows[16] = { 255,0,0, 0,255,0, 0,0, 0,0,255, 255,255,255, 0,0 };
    bmp.write((const char*)rows, 16);
}

// count issues matching a level + path substring
int countIssues(const AssetValidationReport& r, AssetValidationIssue::Level level,
                const std::string& pathPart) {
    int n = 0;
    for (const auto& i : r.issues) {
        if (i.level == level && i.path.find(pathPart) != std::string::npos) ++n;
    }
    return n;
}

class AssetValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::error_code ec;
        fs::remove_all(kRoot, ec);
        fs::create_directories(kRoot);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(kRoot, ec);
    }
};

} // namespace

TEST_F(AssetValidatorTest, CleanTreeReportsNoIssues) {
    writeTinyBMP(fs::path(kRoot) / "ok.bmp");
    {
        std::ofstream obj(fs::path(kRoot) / "ok.obj");
        obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"; // tiny: under LOD floor
    }
    JobSystem jobs(2);
    const auto report = ValidateAssetTree(kRoot, jobs);
    EXPECT_EQ(report.modelsChecked, 1);
    EXPECT_EQ(report.texturesChecked, 1);
    EXPECT_TRUE(report.issues.empty());
    EXPECT_EQ(report.errorCount(), 0);
}

TEST_F(AssetValidatorTest, BrokenModelIsAnError) {
    {
        std::ofstream obj(fs::path(kRoot) / "broken.obj");
        obj << "this is not a wavefront file \x01\x02\x03\n";
    }
    JobSystem jobs(2);
    const auto report = ValidateAssetTree(kRoot, jobs);
    EXPECT_EQ(countIssues(report, AssetValidationIssue::Level::Err, "broken.obj"), 1);
    EXPECT_GE(report.errorCount(), 1);
}

TEST_F(AssetValidatorTest, MissingTextureReferenceIsAWarning) {
    {
        std::ofstream mtl(fs::path(kRoot) / "m.mtl");
        mtl << "newmtl m\nmap_Kd missing_texture.bmp\n";
    }
    {
        std::ofstream obj(fs::path(kRoot) / "m.obj");
        obj << "mtllib m.mtl\nusemtl m\n"
            << "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
            << "vt 0 0\nvt 1 0\nvt 0 1\n"
            << "f 1/1 2/2 3/3\n";
    }
    JobSystem jobs(2);
    const auto report = ValidateAssetTree(kRoot, jobs);
    EXPECT_EQ(countIssues(report, AssetValidationIssue::Level::Warn, "m.obj"), 1);
    EXPECT_EQ(report.errorCount(), 0) << "missing texture is a WARN, not an ERR";
}

TEST_F(AssetValidatorTest, OversizedTextureAgainstImportSettingsWarns) {
    writeTinyBMP(fs::path(kRoot) / "big.bmp"); // 2x2
    ImportSettings s;
    s.maxDimension = 1; // anything over 1px violates
    ASSERT_TRUE(SaveImportSettings(std::string(kRoot) + "/big.bmp", s));

    JobSystem jobs(2);
    const auto report = ValidateAssetTree(kRoot, jobs);
    EXPECT_EQ(countIssues(report, AssetValidationIssue::Level::Warn, "big.bmp"), 1);

    // raise the limit: warning disappears
    s.maxDimension = 4096;
    ASSERT_TRUE(SaveImportSettings(std::string(kRoot) + "/big.bmp", s));
    const auto report2 = ValidateAssetTree(kRoot, jobs);
    EXPECT_EQ(countIssues(report2, AssetValidationIssue::Level::Warn, "big.bmp"), 0);
}

TEST_F(AssetValidatorTest, NoAcceptedLodWarnsOnDenseObj) {
    // a dense OBJ grid imports as disconnected per-face vertices (no
    // JoinIdenticalVertices), so simplification accepts nothing — exactly
    // what this check exists to surface
    {
        std::ofstream obj(fs::path(kRoot) / "grid.obj");
        constexpr int N = 24;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x)
                obj << "v " << x << " 0 " << y << "\n";
        for (int y = 0; y + 1 < N; ++y) {
            for (int x = 0; x + 1 < N; ++x) {
                const int a = y * N + x + 1, b = a + 1, c = a + N, d = c + 1;
                obj << "f " << a << " " << b << " " << c << "\n";
                obj << "f " << b << " " << d << " " << c << "\n";
            }
        }
    }
    JobSystem jobs(2);
    const auto report = ValidateAssetTree(kRoot, jobs);
    EXPECT_GE(countIssues(report, AssetValidationIssue::Level::Warn, "grid.obj"), 1);
    EXPECT_EQ(report.errorCount(), 0);
}
