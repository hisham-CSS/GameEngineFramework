#include "SceneSerializer.h"
#include "Scene.h"
#include "Components.h"
#include "AssetManager.h"
#include "Model.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace {

    json vec3ToJson(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }

    glm::vec3 vec3FromJson(const json& j, const glm::vec3& fallback = glm::vec3(0.f)) {
        if (!j.is_array() || j.size() != 3) return fallback;
        return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    }

} // namespace

namespace MyCoreEngine {

    bool SceneSerializer::Save(const std::string& path) const {
        auto& reg = scene_.registry;

        json root;
        root["version"] = kVersion;

        // --- scene-level lighting / shading settings ---
        json settings;
        settings["lightDir"] = vec3ToJson(scene_.LightDir());
        settings["lightColor"] = vec3ToJson(scene_.LightColor());
        settings["lightIntensity"] = scene_.LightIntensity();
        settings["pbrEnabled"] = scene_.GetPBREnabled();
        settings["normalMapEnabled"] = scene_.GetNormalMapEnabled();
        settings["instancingEnabled"] = scene_.GetInstancingEnabled();
        settings["metallic"] = scene_.GetMetallic();
        settings["roughness"] = scene_.GetRoughness();
        settings["ao"] = scene_.GetAO();
        settings["metallicMapEnabled"] = scene_.GetMetallicMapEnabled();
        settings["roughnessMapEnabled"] = scene_.GetRoughnessMapEnabled();
        settings["aoMapEnabled"] = scene_.GetAOMapEnabled();
        settings["iblEnabled"] = scene_.GetIBLEnabled();
        settings["iblIntensity"] = scene_.GetIBLIntensity();
        settings["lodEnabled"] = scene_.GetLODEnabled();
        settings["lodDistanceScale"] = scene_.GetLODDistanceScale();
        settings["depthPrepass"] = scene_.GetDepthPrepassEnabled();
        root["settings"] = std::move(settings);

        // --- entities ---
        // Entities have no stable ids in the file: parent links serialize as
        // the INDEX of the parent within this array (same iteration order).
        std::unordered_map<entt::entity, size_t> entityIndex;
        {
            size_t i = 0;
            for (auto e : reg.view<entt::entity>()) entityIndex[e] = i++;
        }

        json entities = json::array();
        for (auto e : reg.view<entt::entity>()) {
            // object even when component-less: every entity must occupy an
            // OBJECT slot so parent indices stay aligned on load
            json je = json::object();

            if (auto* name = reg.try_get<Name>(e)) {
                je["name"] = name->value;
            }
            if (auto* p = reg.try_get<Parent>(e)) {
                if (auto it = entityIndex.find(p->value); it != entityIndex.end()) {
                    je["parent"] = it->second;
                }
            }
            if (auto* t = reg.try_get<Transform>(e)) {
                je["transform"] = {
                    { "position", vec3ToJson(t->position) },
                    { "rotation", vec3ToJson(t->rotation) },
                    { "scale",    vec3ToJson(t->scale) },
                };
            }
            if (auto* mc = reg.try_get<ModelComponent>(e)) {
                // empty string = component present but no model loaded (a
                // legitimate authoring state — must survive save/load like
                // it survives undo/play-stop restores)
                je["model"] = (mc->model && !mc->model->SourcePath().empty())
                    ? mc->model->SourcePath() : std::string();
            }
            if (reg.any_of<NoShadow>(e)) {
                je["noShadow"] = true;
            }
            if (auto* cam = reg.try_get<CameraComponent>(e)) {
                je["camera"] = {
                    { "fovDeg",  cam->fovDeg },
                    { "primary", cam->primary },
                };
            }
            if (auto* ov = reg.try_get<MaterialOverrides>(e)) {
                json jov = json::array();
                for (const auto& [slot, mat] : ov->byIndex) {
                    if (!mat) continue;
                    jov.push_back({
                        { "slot",      slot },
                        { "baseColor", vec3ToJson(mat->baseColor) },
                        { "emissive",  vec3ToJson(mat->emissive) },
                        { "metallic",  mat->metallic },
                        { "roughness", mat->roughness },
                        { "ao",        mat->ao },
                    });
                }
                if (!jov.empty()) je["materialOverrides"] = std::move(jov);
            }

            entities.push_back(std::move(je));
        }
        root["entities"] = std::move(entities);

        std::ofstream out(path);
        if (!out) {
            std::cerr << "ERROR::SCENE::SAVE_FAILED cannot open '" << path << "'" << std::endl;
            return false;
        }
        out << root.dump(2) << "\n";
        return static_cast<bool>(out);
    }

    bool SceneSerializer::Load(const std::string& path) {
        std::ifstream in(path);
        if (!in) {
            std::cerr << "ERROR::SCENE::LOAD_FAILED cannot open '" << path << "'" << std::endl;
            return false;
        }

        json root;
        try {
            in >> root;
        }
        catch (const json::exception& e) {
            std::cerr << "ERROR::SCENE::LOAD_FAILED parse error in '" << path << "': "
                      << e.what() << std::endl;
            return false;
        }

        if (!root.is_object() || !root.contains("entities") || !root["entities"].is_array()) {
            std::cerr << "ERROR::SCENE::LOAD_FAILED '" << path << "' is not a scene file" << std::endl;
            return false;
        }
        const int version = root.value("version", 0);
        if (version <= 0 || version > kVersion) {
            std::cerr << "ERROR::SCENE::LOAD_FAILED unsupported scene version " << version
                      << " in '" << path << "'" << std::endl;
            return false;
        }

        // File is valid — replace scene contents from here on.
        auto& reg = scene_.registry;
        reg.clear();

        if (root.contains("settings")) {
            const json& s = root["settings"];
            scene_.LightDir() = vec3FromJson(s.value("lightDir", json()), scene_.LightDir());
            scene_.LightColor() = vec3FromJson(s.value("lightColor", json()), scene_.LightColor());
            scene_.LightIntensity() = s.value("lightIntensity", scene_.LightIntensity());
            scene_.SetPBREnabled(s.value("pbrEnabled", scene_.GetPBREnabled()));
            scene_.SetNormalMapEnabled(s.value("normalMapEnabled", scene_.GetNormalMapEnabled()));
            scene_.SetInstancingEnabled(s.value("instancingEnabled", scene_.GetInstancingEnabled()));
            scene_.SetMetallic(s.value("metallic", scene_.GetMetallic()));
            scene_.SetRoughness(s.value("roughness", scene_.GetRoughness()));
            scene_.SetAO(s.value("ao", scene_.GetAO()));
            scene_.SetMetallicMapEnabled(s.value("metallicMapEnabled", scene_.GetMetallicMapEnabled()));
            scene_.SetRoughnessMapEnabled(s.value("roughnessMapEnabled", scene_.GetRoughnessMapEnabled()));
            scene_.SetAOMapEnabled(s.value("aoMapEnabled", scene_.GetAOMapEnabled()));
            scene_.SetIBLEnabled(s.value("iblEnabled", scene_.GetIBLEnabled()));
            scene_.SetIBLIntensity(s.value("iblIntensity", scene_.GetIBLIntensity()));
            scene_.SetLODEnabled(s.value("lodEnabled", scene_.GetLODEnabled()));
            scene_.SetLODDistanceScale(s.value("lodDistanceScale", scene_.GetLODDistanceScale()));
            scene_.SetDepthPrepassEnabled(s.value("depthPrepass", scene_.GetDepthPrepassEnabled()));
        }

        // parent links resolve in a second pass: a child can precede its
        // parent in the array
        std::vector<entt::entity> created;
        std::vector<std::pair<entt::entity, size_t>> pendingParents;

        for (const json& je : root["entities"]) {
            if (!je.is_object()) {
                // keep index alignment: parent refs are FILE-array indices,
                // so skipped entries must still occupy a created[] slot
                created.push_back(entt::null);
                continue;
            }
            Entity entity = scene_.createEntity();
            created.push_back(entity);
            if (je.contains("parent") && je["parent"].is_number_unsigned()) {
                pendingParents.emplace_back(entity, je["parent"].get<size_t>());
            }

            if (je.contains("name")) {
                entity.addComponent<Name>(Name{ je["name"].get<std::string>() });
            }
            if (je.contains("transform")) {
                const json& jt = je["transform"];
                Transform t{};
                t.position = vec3FromJson(jt.value("position", json()), t.position);
                t.rotation = vec3FromJson(jt.value("rotation", json()), t.rotation);
                t.scale = vec3FromJson(jt.value("scale", json()), t.scale);
                t.dirty = true;
                entity.addComponent<Transform>(t);
            }

            std::shared_ptr<Model> model;
            if (je.contains("model")) {
                const std::string modelPath = je["model"].get<std::string>();
                if (modelPath.empty()) {
                    // component present, no model loaded yet
                    entity.addComponent<ModelComponent>(ModelComponent{});
                }
                else {
                    model = assets_.GetModel(modelPath);
                    if (model) {
                        entity.addComponent<ModelComponent>(ModelComponent{ model });
                        // AABB is derived data: regenerate rather than trust the file.
                        // Skip empty models (failed loads) — their bounds would be garbage.
                        if (!model->Meshes().empty()) {
                            entity.addComponent<AABB>(generateAABB(*model));
                        }
                    }
                }
            }
            if (je.value("noShadow", false)) {
                reg.emplace<NoShadow>(entity);
            }
            if (je.contains("camera") && je["camera"].is_object()) {
                const json& jc = je["camera"];
                CameraComponent cam;
                // clamp: an out-of-range fov (hand-edited file) makes the
                // projection degenerate and renders silent garbage
                cam.fovDeg = glm::clamp(jc.value("fovDeg", cam.fovDeg), 1.0f, 179.0f);
                cam.primary = jc.value("primary", cam.primary);
                reg.emplace<CameraComponent>(entity, cam);
            }
            if (je.contains("materialOverrides") && model) {
                const auto& shared = model->Materials();
                MaterialOverrides ov;
                for (const json& jo : je["materialOverrides"]) {
                    const size_t slot = jo.value("slot", size_t{ 0 });
                    // Clone the shared material so texture ids carry over, then
                    // apply the serialized scalar edits on top.
                    auto mat = (slot < shared.size() && shared[slot])
                        ? std::make_shared<Material>(*shared[slot])
                        : std::make_shared<Material>();
                    mat->baseColor = vec3FromJson(jo.value("baseColor", json()), mat->baseColor);
                    mat->emissive = vec3FromJson(jo.value("emissive", json()), mat->emissive);
                    mat->metallic = jo.value("metallic", mat->metallic);
                    mat->roughness = jo.value("roughness", mat->roughness);
                    mat->ao = jo.value("ao", mat->ao);
                    ov.byIndex[slot] = std::move(mat);
                }
                if (!ov.byIndex.empty()) {
                    entity.addComponent<MaterialOverrides>(std::move(ov));
                }
            }
        }

        for (const auto& [child, parentIdx] : pendingParents) {
            if (parentIdx >= created.size()) continue;
            const entt::entity parent = created[parentIdx];
            if (parent == entt::null || parent == child) continue;
            // refuse links that would close a cycle (corrupt/hand-edited
            // file): cycle members would be unreachable from any root and
            // vanish from the hierarchy panel entirely
            if (IsSameOrDescendantOf(reg, parent, child)) {
                std::cerr << "WARN::SCENE::LOAD parent link skipped (cycle) in '"
                          << path << "'" << std::endl;
                continue;
            }
            reg.emplace<Parent>(child, Parent{ parent });
        }

        return true;
    }

} // namespace MyCoreEngine
