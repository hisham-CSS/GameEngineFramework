#include "SceneSerializer.h"
#include "Scene.h"
#include "Components.h"
#include "AssetManager.h"
#include "Model.h"
#include "PathSandbox.h"
#include "../physics/PhysicsComponents.h"
#include "../script/ScriptComponent.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
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
        settings["aaEnabled"] = scene_.GetAAEnabled();

        // --- environment (skybox + image-based lighting) ---
        // How strongly the environment LIGHTS the scene stays in
        // settings["iblIntensity"] above; this block is only about WHICH
        // environment and how the sky is drawn.
        {
            const EnvironmentSettings& e = scene_.Environment();
            json env;
            env["source"]       = static_cast<int>(e.source);
            env["hdriPath"]     = e.hdriPath;
            env["skyIntensity"] = e.skyIntensity;
            env["drawSkybox"]   = e.drawSkybox;
            env["zenith"]       = vec3ToJson(e.zenith);
            env["horizon"]      = vec3ToJson(e.horizon);
            env["ground"]       = vec3ToJson(e.ground);
            env["sunIntensity"] = e.sunIntensity;
            settings["environment"] = std::move(env);
        }
        root["settings"] = std::move(settings);

        // --- entities ---
        // Entities have no stable ids in the file: parent links serialize as
        // the INDEX of the parent within this array (same iteration order).
        // Write in CREATION order (entt views iterate newest-first): Load
        // recreates entities in file order, so this keeps relative entity
        // indices stable across save/load cycles — priority ties in camera
        // selection (lowest index wins) must not flip on every save.
        std::vector<entt::entity> ordered;
        for (auto e : reg.view<entt::entity>()) ordered.push_back(e);
        std::reverse(ordered.begin(), ordered.end());

        std::unordered_map<entt::entity, size_t> entityIndex;
        {
            size_t i = 0;
            for (auto e : ordered) entityIndex[e] = i++;
        }

        json entities = json::array();
        for (auto e : ordered) {
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
                    { "fovDeg",   cam->fovDeg },
                    { "nearClip", cam->nearClip },
                    { "farClip",  cam->farClip },
                    { "priority", cam->priority },
                    { "enabled",  cam->enabled },
                };
            }
            if (auto* lc = reg.try_get<LightComponent>(e)) {
                je["light"] = {
                    { "type",      static_cast<int>(lc->type) },
                    { "color",     vec3ToJson(lc->color) },
                    { "intensity", lc->intensity },
                    { "range",     lc->range },
                    { "innerAngleDeg", lc->innerAngleDeg },
                    { "outerAngleDeg", lc->outerAngleDeg },
                    { "enabled",   lc->enabled },
                };
            }

            // --- scripting. Only the path and the enable flag are saved:
            // script STATE lives in the language runtime and is deliberately
            // not persisted -- it would not survive a backend swap, and a
            // half-restored VM is worse than a clean re-run of OnStart.
            if (auto* sc = reg.try_get<ScriptComponent>(e)) {
                je["script"] = {
                    { "path",    sc->path },
                    { "enabled", sc->enabled },
                };
            }

            // --- physics: body + whichever collider shape the entity has.
            // Backend-agnostic by construction (these are engine enums and
            // plain floats), so a scene authored against Jolt loads under
            // PhysX unchanged.
            if (auto* rb = reg.try_get<RigidBody>(e)) {
                je["rigidBody"] = {
                    { "type",            static_cast<int>(rb->type) },
                    { "mass",            rb->mass },
                    { "friction",        rb->friction },
                    { "restitution",     rb->restitution },
                    { "linearDamping",   rb->linearDamping },
                    { "angularDamping",  rb->angularDamping },
                    { "isTrigger",       rb->isTrigger },
                    { "initialLinearVelocity", vec3ToJson(rb->initialLinearVelocity) },
                };
            }
            if (auto* c = reg.try_get<BoxCollider>(e)) {
                je["boxCollider"] = {
                    { "halfExtents", vec3ToJson(c->halfExtents) },
                    { "offset",      vec3ToJson(c->offset) },
                };
            }
            if (auto* c = reg.try_get<SphereCollider>(e)) {
                je["sphereCollider"] = {
                    { "radius", c->radius },
                    { "offset", vec3ToJson(c->offset) },
                };
            }
            if (auto* c = reg.try_get<CapsuleCollider>(e)) {
                je["capsuleCollider"] = {
                    { "radius",     c->radius },
                    { "halfHeight", c->halfHeight },
                    { "offset",     vec3ToJson(c->offset) },
                };
            }
            if (auto* c = reg.try_get<PlaneCollider>(e)) {
                je["planeCollider"] = { { "offset", vec3ToJson(c->offset) } };
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
                        { "alphaMode",   static_cast<int>(mat->alphaMode) },
                        { "opacity",     mat->opacity },
                        { "alphaCutoff", mat->alphaCutoff },
                        { "doubleSided", mat->doubleSided },
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

      try {
        // A wrong-TYPED field ("version": {}, "position": "north") throws
        // json::type_error, NOT a parse error, so the parse-only guard above
        // does not cover it. Every .value()/get<> below is on untrusted data;
        // one catch here turns a hostile or hand-corrupted file into a clean
        // load failure instead of a crash that takes the editor down.
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
            scene_.SetAAEnabled(s.value("aaEnabled", scene_.GetAAEnabled()));

            // --- environment (see the save side). Absent block => the
            // defaults, which is the procedural sky: an older scene file
            // gains environment lighting rather than loading unlit.
            if (s.contains("environment") && s["environment"].is_object()) {
                const json& je = s["environment"];
                EnvironmentSettings e;
                const int src = je.value("source", static_cast<int>(e.source));
                e.source = (src == static_cast<int>(EnvironmentSettings::Source::HDRi))
                    ? EnvironmentSettings::Source::HDRi
                    : EnvironmentSettings::Source::ProceduralSky;
                e.hdriPath     = je.value("hdriPath", e.hdriPath);
                e.skyIntensity = std::max(0.f, je.value("skyIntensity", e.skyIntensity));
                e.drawSkybox   = je.value("drawSkybox", e.drawSkybox);
                e.zenith       = vec3FromJson(je.value("zenith", json()), e.zenith);
                e.horizon      = vec3FromJson(je.value("horizon", json()), e.horizon);
                e.ground       = vec3FromJson(je.value("ground", json()), e.ground);
                e.sunIntensity = std::max(0.f, je.value("sunIntensity", e.sunIntensity));
                scene_.SetEnvironment(e);
            }
        }

        // parent links resolve in a second pass: a child can precede its
        // parent in the array
        std::vector<entt::entity> created;
        std::vector<std::pair<entt::entity, size_t>> pendingParents;
        int legacyPrimaries = 0; // pre-director files: count of "primary" cameras seen

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
                std::filesystem::path containedPath;
                if (modelPath.empty()) {
                    // component present, no model loaded yet
                    entity.addComponent<ModelComponent>(ModelComponent{});
                }
                else if (!PathIsContained(/*baseDir=*/"", modelPath, containedPath)) {
                    // The model path is untrusted (authored scene content) and
                    // flows straight into Assimp, whose mesh importers have a
                    // history of heap-overflow CVEs on malformed files. Refuse
                    // absolute/root/".." paths (see PathSandbox.h) BEFORE the
                    // loader ever opens them: a "../../evil.obj" or "C:/..."
                    // could otherwise point the importer at an arbitrary file.
                    // Base is empty => the working directory the loader itself
                    // resolves against; model paths are stored project-relative
                    // (e.g. "Exported/Model/backpack.obj"). Keep the entity with
                    // an empty ModelComponent, exactly like the empty-path case,
                    // so a rejected asset degrades gracefully instead of
                    // dropping the whole entity (and its parent-index slot).
                    std::cerr << "[SceneSerializer] rejected model path outside the project: '"
                              << modelPath << "'\n";
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
                // clamp: out-of-range lens values (hand-edited file) make
                // the projection degenerate and render silent garbage.
                // MinFarClipFor keeps the separation RELATIVE — an absolute
                // epsilon rounds away above ~32k and near == far would
                // reach the projection as a division by zero.
                cam.fovDeg = glm::clamp(jc.value("fovDeg", cam.fovDeg), 1.0f, 179.0f);
                cam.nearClip = std::max(jc.value("nearClip", cam.nearClip), 1e-3f);
                cam.farClip = std::max(jc.value("farClip", cam.farClip),
                                       MinFarClipFor(cam.nearClip));
                cam.priority = jc.value("priority", cam.priority);
                cam.enabled = jc.value("enabled", cam.enabled);
                // legacy (pre camera-director): "primary": true marked the
                // rendered camera — map it to a priority bump. Later
                // primaries get HIGHER priority: the old selection iterated
                // newest-first, so with several primaries (the old default
                // — the flag started true and nothing cleared it on other
                // cameras) the LAST one in the file used to render.
                if (!jc.contains("priority") && jc.value("primary", false)) {
                    cam.priority = ++legacyPrimaries;
                }
                reg.emplace<CameraComponent>(entity, cam);
            }
            if (je.contains("light") && je["light"].is_object()) {
                const json& jl = je["light"];
                LightComponent lc;
                const int t = jl.value("type", static_cast<int>(lc.type));
                lc.type = (t == static_cast<int>(LightType::Spot)) ? LightType::Spot
                                                                   : LightType::Point;
                lc.color = vec3FromJson(jl.value("color", json()), lc.color);
                lc.intensity = std::max(0.f, jl.value("intensity", lc.intensity));
                lc.range = std::max(0.f, jl.value("range", lc.range));
                // clamp the cone: an outer smaller than the inner would make
                // the spot falloff run backwards and light everything outside
                lc.innerAngleDeg = glm::clamp(jl.value("innerAngleDeg", lc.innerAngleDeg), 0.f, 89.9f);
                lc.outerAngleDeg = glm::clamp(jl.value("outerAngleDeg", lc.outerAngleDeg),
                                              lc.innerAngleDeg, 89.9f);
                lc.enabled = jl.value("enabled", lc.enabled);
                reg.emplace<LightComponent>(entity, lc);
            }

            // --- scripting (see the save side). An entity is given the
            // component even when the path is empty, so a half-authored entity
            // round-trips instead of silently losing its slot on reload.
            if (je.contains("script") && je["script"].is_object()) {
                const json& js = je["script"];
                ScriptComponent sc;
                sc.path    = js.value("path", sc.path);
                sc.enabled = js.value("enabled", sc.enabled);
                reg.emplace<ScriptComponent>(entity, sc);
            }

            // --- physics (see the save side). Every field falls back to the
            // component default, and the enum is range-checked: a hand-edited
            // or newer-build file must not index BodyType out of range.
            if (je.contains("rigidBody") && je["rigidBody"].is_object()) {
                const json& jr = je["rigidBody"];
                RigidBody rb;
                const int t = jr.value("type", static_cast<int>(rb.type));
                rb.type = (t >= 0 && t <= static_cast<int>(BodyType::Dynamic))
                    ? static_cast<BodyType>(t) : BodyType::Dynamic;
                rb.mass = jr.value("mass", rb.mass);
                rb.friction = std::max(0.f, jr.value("friction", rb.friction));
                rb.restitution = glm::clamp(jr.value("restitution", rb.restitution), 0.f, 1.f);
                rb.linearDamping = std::max(0.f, jr.value("linearDamping", rb.linearDamping));
                rb.angularDamping = std::max(0.f, jr.value("angularDamping", rb.angularDamping));
                rb.isTrigger = jr.value("isTrigger", rb.isTrigger);
                rb.initialLinearVelocity = vec3FromJson(
                    jr.value("initialLinearVelocity", json()), rb.initialLinearVelocity);
                reg.emplace<RigidBody>(entity, rb);
            }
            if (je.contains("boxCollider") && je["boxCollider"].is_object()) {
                const json& jc = je["boxCollider"];
                BoxCollider c;
                c.halfExtents = glm::max(
                    vec3FromJson(jc.value("halfExtents", json()), c.halfExtents),
                    glm::vec3(1e-3f)); // zero extents degenerate every backend
                c.offset = vec3FromJson(jc.value("offset", json()), c.offset);
                reg.emplace<BoxCollider>(entity, c);
            }
            if (je.contains("sphereCollider") && je["sphereCollider"].is_object()) {
                const json& jc = je["sphereCollider"];
                SphereCollider c;
                c.radius = std::max(jc.value("radius", c.radius), 1e-3f);
                c.offset = vec3FromJson(jc.value("offset", json()), c.offset);
                reg.emplace<SphereCollider>(entity, c);
            }
            if (je.contains("capsuleCollider") && je["capsuleCollider"].is_object()) {
                const json& jc = je["capsuleCollider"];
                CapsuleCollider c;
                c.radius = std::max(jc.value("radius", c.radius), 1e-3f);
                c.halfHeight = std::max(jc.value("halfHeight", c.halfHeight), 1e-4f);
                c.offset = vec3FromJson(jc.value("offset", json()), c.offset);
                reg.emplace<CapsuleCollider>(entity, c);
            }
            if (je.contains("planeCollider") && je["planeCollider"].is_object()) {
                PlaneCollider c;
                c.offset = vec3FromJson(je["planeCollider"].value("offset", json()), c.offset);
                reg.emplace<PlaneCollider>(entity, c);
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
                    // Transparency. Absent (older files) => the material's own
                    // default, i.e. Opaque -- so pre-transparency scenes load
                    // exactly as before.
                    const int am = jo.value("alphaMode", static_cast<int>(mat->alphaMode));
                    mat->alphaMode = (am >= 0 && am <= static_cast<int>(AlphaMode::Blend))
                        ? static_cast<AlphaMode>(am) : AlphaMode::Opaque;
                    mat->opacity = glm::clamp(jo.value("opacity", mat->opacity), 0.f, 1.f);
                    mat->alphaCutoff = glm::clamp(jo.value("alphaCutoff", mat->alphaCutoff), 0.f, 1.f);
                    mat->doubleSided = jo.value("doubleSided", mat->doubleSided);
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
      catch (const json::exception& e) {
        std::cerr << "ERROR::SCENE::LOAD_FAILED malformed field in '" << path
                  << "': " << e.what() << std::endl;
        return false;
      }
    }

} // namespace MyCoreEngine
