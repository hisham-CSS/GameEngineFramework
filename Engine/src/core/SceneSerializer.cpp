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
        root["settings"] = std::move(settings);

        // --- entities ---
        json entities = json::array();
        for (auto e : reg.view<entt::entity>()) {
            json je;

            if (auto* name = reg.try_get<Name>(e)) {
                je["name"] = name->value;
            }
            if (auto* t = reg.try_get<Transform>(e)) {
                je["transform"] = {
                    { "position", vec3ToJson(t->position) },
                    { "rotation", vec3ToJson(t->rotation) },
                    { "scale",    vec3ToJson(t->scale) },
                };
            }
            if (auto* mc = reg.try_get<ModelComponent>(e)) {
                if (mc->model && !mc->model->SourcePath().empty()) {
                    je["model"] = mc->model->SourcePath();
                }
            }
            if (reg.any_of<NoShadow>(e)) {
                je["noShadow"] = true;
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
        }

        for (const json& je : root["entities"]) {
            if (!je.is_object()) continue;
            Entity entity = scene_.createEntity();

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
                model = assets_.GetModel(je["model"].get<std::string>());
                if (model) {
                    entity.addComponent<ModelComponent>(ModelComponent{ model });
                    // AABB is derived data: regenerate rather than trust the file.
                    // Skip empty models (failed loads) — their bounds would be garbage.
                    if (!model->Meshes().empty()) {
                        entity.addComponent<AABB>(generateAABB(*model));
                    }
                }
            }
            if (je.value("noShadow", false)) {
                reg.emplace<NoShadow>(entity);
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

        return true;
    }

} // namespace MyCoreEngine
