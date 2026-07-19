#include "AssetValidator.h"
#include "AssetIndex.h"
#include "ImportSettings.h"
#include "../core/JobSystem.h"
#include "../core/Model.h"

#include "stb_image.h" // stbi_info: dimensions without a full decode

#include <memory>
#include <string>
#include <vector>

namespace MyCoreEngine {

    namespace {

        void collectByKind(const AssetIndex::Node& node,
                           std::vector<const AssetIndex::Node*>& models,
                           std::vector<const AssetIndex::Node*>& textures)
        {
            for (const auto& c : node.children) {
                switch (c.kind) {
                case AssetIndex::Kind::Directory: collectByKind(c, models, textures); break;
                case AssetIndex::Kind::Model:     models.push_back(&c); break;
                case AssetIndex::Kind::Texture:   textures.push_back(&c); break;
                default: break;
                }
            }
        }

    } // namespace

    AssetValidationReport ValidateAssetTree(const std::string& root, JobSystem& jobs)
    {
        AssetValidationReport report;

        // one synchronous walk on the calling thread: the validator is a
        // batch tool, not a frame loop
        AssetIndex index(root);
        index.tick(1e9f); // immediate scan

        std::vector<const AssetIndex::Node*> models, textures;
        collectByKind(index.root(), models, textures);

        // --- textures: dimensions vs their import settings (cheap, inline)
        for (const auto* t : textures) {
            ++report.texturesChecked;
            int w = 0, h = 0, comp = 0;
            if (!stbi_info(t->relPath.c_str(), &w, &h, &comp)) {
                report.issues.push_back({ AssetValidationIssue::Level::Err,
                    t->relPath, "texture does not decode: " +
                    std::string(stbi_failure_reason() ? stbi_failure_reason() : "unknown") });
                continue;
            }
            const ImportSettings s = LoadImportSettings(t->relPath);
            if (s.maxDimension > 0 && (w > s.maxDimension || h > s.maxDimension)) {
                report.issues.push_back({ AssetValidationIssue::Level::Warn,
                    t->relPath,
                    "exceeds import maxDimension " + std::to_string(s.maxDimension) +
                    " (" + std::to_string(w) + "x" + std::to_string(h) + ")" });
            }
        }

        // --- models: decode in parallel on the workers ---------------------
        struct DecodeSlot {
            std::string path;
            ModelCPUData cpu;
            bool done = false;
        };
        std::vector<std::shared_ptr<DecodeSlot>> slots;
        slots.reserve(models.size());
        for (const auto* m : models) {
            auto slot = std::make_shared<DecodeSlot>();
            slot->path = m->relPath;
            slots.push_back(slot);
            jobs.submit(
                [slot] { slot->cpu = Model::Decode(slot->path); },
                [slot] { slot->done = true; });
        }
        // dedicated pool (documented contract): pump until every decode's
        // completion ran — the loop converges because the job count is fixed
        do {
            jobs.waitIdle();
        } while (jobs.pumpCompletions(1e6f) > 0);

        for (const auto& slot : slots) {
            ++report.modelsChecked;
            if (!slot->done) {
                report.issues.push_back({ AssetValidationIssue::Level::Err,
                    slot->path, "decode never completed (job system)" });
                continue;
            }
            const ModelCPUData& cpu = slot->cpu;
            if (!cpu.valid) {
                report.issues.push_back({ AssetValidationIssue::Level::Err,
                    slot->path, "model failed to import" });
                continue;
            }
            if (cpu.meshes.empty()) {
                // lenient importers (OBJ especially) accept garbage as an
                // empty scene — an invisible model at runtime is an error
                report.issues.push_back({ AssetValidationIssue::Level::Err,
                    slot->path, "model imports but contains no meshes" });
                continue;
            }
            for (const auto& t : cpu.textures) {
                if (!t.decoded) {
                    report.issues.push_back({ AssetValidationIssue::Level::Warn,
                        slot->path, "references missing/undecodable texture: " + t.file });
                }
            }
            // meshes big enough to qualify for simplification where NO level
            // was accepted pay full vertex cost at every distance — usually
            // the OBJ per-face-vertex (disconnected soup) issue. Rolled up
            // to ONE warning per model: a 70-mesh asset must not flood the
            // report with 70 identical lines.
            size_t lodEligible = 0, lodRejected = 0, rejectedTris = 0;
            for (const auto& mesh : cpu.meshes) {
                if (mesh.indices.size() < 3u * 64u) continue; // under the simplify floor
                ++lodEligible;
                bool anyAccepted = false;
                for (int l = 1; l < Mesh::kLodCount; ++l) {
                    if (!mesh.lodIndices[l].empty()) { anyAccepted = true; break; }
                }
                if (!anyAccepted) {
                    ++lodRejected;
                    rejectedTris += mesh.indices.size() / 3;
                }
            }
            if (lodRejected > 0) {
                report.issues.push_back({ AssetValidationIssue::Level::Warn,
                    slot->path, std::to_string(lodRejected) + " of " +
                    std::to_string(lodEligible) + " LOD-eligible meshes accepted no "
                    "level (" + std::to_string(rejectedTris) + " tris at full cost "
                    "at every distance; disconnected OBJ vertices?)" });
            }
        }

        return report;
    }

} // namespace MyCoreEngine
