#include "SceneSerializer.h"
#include "Scene.h"
#include "Components.h"
#include "AssetManager.h"
#include "Model.h"
#include "PathSandbox.h"
#include "../physics/PhysicsComponents.h"
#include "../script/ScriptComponent.h"
#include "../audio/AudioComponents.h"
#include "../audio/AudioTypes.h"   // kMinAudibleDistance (shared clamp floor)
#include "../ui/UIComponent.h"

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
        // Projected-size cull. These were the only Scene render settings the
        // block omitted, so ticking "Cull tiny objects" survived only until the
        // next load and never reached the shipped player (the quality tier sets
        // them too, but the default tier is Custom, which leaves them alone).
        settings["smallCullEnabled"] = scene_.GetSmallCullEnabled();
        settings["smallCullPixels"] = scene_.GetSmallCullPixels();
        settings["depthPrepass"] = scene_.GetDepthPrepassEnabled();
        settings["aaEnabled"] = scene_.GetAAEnabled();

        // --- post-process stack ---
        {
            const auto& p = scene_.PostFX();
            json post;
            post["vignetteEnabled"]    = p.vignette.enabled;
            post["vignetteIntensity"]  = p.vignette.intensity;
            post["vignetteRoundness"]  = p.vignette.roundness;
            post["vignetteSmoothness"] = p.vignette.smoothness;
            post["outlineEnabled"]   = p.outline.enabled;
            post["outlineThickness"] = p.outline.thickness;
            post["outlineThreshold"] = p.outline.threshold;
            post["outlineStrength"]  = p.outline.strength;
            post["outlineColor"]     = { p.outline.color.x, p.outline.color.y, p.outline.color.z };
            post["gradeEnabled"]     = p.colorGrade.enabled;
            post["gradeContrast"]    = p.colorGrade.contrast;
            post["gradeSaturation"]  = p.colorGrade.saturation;
            post["gradeTemperature"] = p.colorGrade.temperature;
            post["gradeTint"]        = p.colorGrade.tint;
            post["gradeLift"]        = p.colorGrade.lift;
            post["gradeGain"]        = p.colorGrade.gain;
            post["bloomEnabled"]   = p.bloom.enabled;
            post["bloomThreshold"] = p.bloom.threshold;
            post["bloomIntensity"] = p.bloom.intensity;
            settings["postFX"] = post;
        }
        settings["qualityLevel"] = static_cast<int>(scene_.GetQualityLevel());

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

            if (auto* as = reg.try_get<AudioSourceComponent>(e)) {
                je["audioSource"] = {
                    { "clip",        as->clip },
                    { "volume",      as->volume },
                    { "pitch",       as->pitch },
                    { "loop",        as->loop },
                    { "spatial",     as->spatial },
                    { "playOnStart", as->playOnStart },
                    { "minDistance", as->minDistance },
                    { "maxDistance", as->maxDistance },
                };
            }
            // Empty tag: its presence is the whole state (mirrors noShadow).
            if (reg.any_of<AudioListenerComponent>(e)) {
                je["audioListener"] = true;
            }

            if (auto* ud = reg.try_get<UIDocumentComponent>(e)) {
                je["uiDocument"] = {
                    { "markup",      ud->markup },
                    { "stylesheet",  ud->stylesheet },
                    { "sortOrder",   ud->sortOrder },
                    { "uiScaleMode", int(ud->scale.mode) },
                    { "uiScaleReference", json::array({ ud->scale.reference.x,
                                                        ud->scale.reference.y }) },
                    { "uiScaleMatch", ud->scale.match },
                    { "enabled",     ud->enabled },
                    { "interactive", ud->interactive },
                    { "region",      json::array({ ud->regionX, ud->regionY,
                                                   ud->regionW, ud->regionH }) },
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
                        { "alphaMode",   static_cast<int>(mat->alphaMode) },
                        { "opacity",     mat->opacity },
                        { "alphaCutoff", mat->alphaCutoff },
                        { "doubleSided", mat->doubleSided },
                        { "shadingModel", static_cast<int>(mat->shadingModel) },
                        { "toonBands",        mat->toonBands },
                        { "toonSpecStrength", mat->toonSpecStrength },
                        { "toonSpecSize",     mat->toonSpecSize },
                        { "toonRimStrength",  mat->toonRimStrength },
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
        // Honour the documented guarantee that a bad file leaves the current
        // scene intact. Only a JSON *syntax* error and a bad version were
        // checked before the registry was cleared; every .value()/get<> after
        // that point runs on untrusted data and throws json::type_error the
        // moment it reaches the bad field -- by which time the user's unsaved
        // scene was already gone, with no undo.
        //
        // So run the WHOLE load against a throwaway Scene first. If that
        // survives, the real pass cannot fail on a type error. Re-using Load
        // itself rather than a parallel validator means the two can never
        // disagree about the schema; the probe skips asset loading, so it costs
        // a JSON walk, not model I/O.
        if (!dryRun_) {
            Scene scratch;
            SceneSerializer probe(scratch, assets_);
            probe.dryRun_ = true;
            if (!probe.Load(path)) return false; // scene_ untouched
        }

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
        // ResetToDefaults, not just registry.clear(): every setting below is
        // applied with the CURRENT scene value as its .value() fallback, so a
        // file whose settings block omits a key used to silently inherit
        // whatever the PREVIOUSLY loaded scene had — and the next Save then
        // wrote those strangers into the file. Resetting first makes an absent
        // key mean "default", which is what the fallbacks now read.
        scene_.ResetToDefaults();
        auto& reg = scene_.registry;

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
            scene_.SetSmallCullEnabled(s.value("smallCullEnabled", scene_.GetSmallCullEnabled()));
            scene_.SetSmallCullPixels(s.value("smallCullPixels", scene_.GetSmallCullPixels()));
            scene_.SetDepthPrepassEnabled(s.value("depthPrepass", scene_.GetDepthPrepassEnabled()));
            scene_.SetAAEnabled(s.value("aaEnabled", scene_.GetAAEnabled()));

            // --- post-process stack (see the save side). Absent => defaults
            // (all effects off), so an older scene loads unchanged.
            if (s.contains("postFX") && s["postFX"].is_object()) {
                const json& jp = s["postFX"];
                auto& p = scene_.PostFX();
                p.vignette.enabled    = jp.value("vignetteEnabled",    p.vignette.enabled);
                p.vignette.intensity  = jp.value("vignetteIntensity",  p.vignette.intensity);
                p.vignette.roundness  = jp.value("vignetteRoundness",  p.vignette.roundness);
                p.vignette.smoothness = jp.value("vignetteSmoothness", p.vignette.smoothness);
                p.outline.enabled   = jp.value("outlineEnabled",   p.outline.enabled);
                p.outline.thickness = jp.value("outlineThickness", p.outline.thickness);
                p.outline.threshold = jp.value("outlineThreshold", p.outline.threshold);
                p.outline.strength  = jp.value("outlineStrength",  p.outline.strength);
                if (jp.contains("outlineColor") && jp["outlineColor"].is_array() &&
                    jp["outlineColor"].size() == 3) {
                    const auto& c = jp["outlineColor"];
                    p.outline.color = { c[0].get<float>(), c[1].get<float>(), c[2].get<float>() };
                }
                p.colorGrade.enabled     = jp.value("gradeEnabled",     p.colorGrade.enabled);
                p.colorGrade.contrast    = jp.value("gradeContrast",    p.colorGrade.contrast);
                p.colorGrade.saturation  = jp.value("gradeSaturation",  p.colorGrade.saturation);
                p.colorGrade.temperature = jp.value("gradeTemperature", p.colorGrade.temperature);
                p.colorGrade.tint        = jp.value("gradeTint",        p.colorGrade.tint);
                p.colorGrade.lift        = jp.value("gradeLift",        p.colorGrade.lift);
                p.colorGrade.gain        = jp.value("gradeGain",        p.colorGrade.gain);
                p.bloom.enabled   = jp.value("bloomEnabled",   p.bloom.enabled);
                p.bloom.threshold = jp.value("bloomThreshold", p.bloom.threshold);
                p.bloom.intensity = jp.value("bloomIntensity", p.bloom.intensity);
            }

            const int ql = s.value("qualityLevel",
                                   static_cast<int>(scene_.GetQualityLevel()));
            if (ql >= 0 && ql <= static_cast<int>(Scene::QualityLevel::Custom))
                scene_.SetQualityLevel(static_cast<Scene::QualityLevel>(ql));

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

            // No is_string() guard on purpose: a wrong-typed "name" throws and
            // fails the whole load, which is the house rule for a malformed
            // field (see WrongTypedFieldLeavesTheOpenSceneIntact). Load is
            // non-destructive, so the user keeps the scene they had and gets an
            // error naming the file — strictly better than silently dropping a
            // name they wrote and only noticing on the next save.
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
                else if (dryRun_) {
                    // Validation probe: the path has been read and sandboxed,
                    // which is all this pass needs. Skip the actual import so
                    // validating a scene costs a JSON walk rather than loading
                    // every model twice.
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

            if (je.contains("audioSource") && je["audioSource"].is_object()) {
                const json& ja = je["audioSource"];
                AudioSourceComponent as;
                as.clip        = ja.value("clip", as.clip);
                // Same containment gate as model/script/HDRi paths. A clip path
                // is authored (i.e. untrusted) content that flows straight into
                // miniaudio's WAV/MP3/FLAC/OGG decoders, which parse
                // attacker-controlled binary — so "../../etc/passwd" or an
                // absolute path must be refused BEFORE the decoder opens it.
                // Drop just the clip, keeping the component, so a rejected asset
                // degrades gracefully exactly like a rejected model.
                if (!as.clip.empty()) {
                    std::filesystem::path containedClip;
                    if (!PathIsContained(/*baseDir=*/"", as.clip, containedClip)) {
                        std::cerr << "[SceneSerializer] rejected audio clip path outside the project: '"
                                  << as.clip << "'\n";
                        as.clip.clear();
                    }
                }
                as.volume      = glm::clamp(ja.value("volume", as.volume), 0.f, 1.f);
                as.pitch       = std::max(1e-3f, ja.value("pitch", as.pitch));
                as.loop        = ja.value("loop", as.loop);
                as.spatial     = ja.value("spatial", as.spatial);
                as.playOnStart = ja.value("playOnStart", as.playOnStart);
                // min must stay strictly POSITIVE (every attenuation model
                // divides by it; 0 silences the source at every distance), and
                // max strictly above min or the falloff span collapses.
                as.minDistance = std::max(kMinAudibleDistance,
                                          ja.value("minDistance", as.minDistance));
                as.maxDistance = std::max(ja.value("maxDistance", as.maxDistance),
                                          as.minDistance + kMinAudibleDistance);
                reg.emplace<AudioSourceComponent>(entity, as);
            }
            if (je.value("audioListener", false)) {
                reg.emplace<AudioListenerComponent>(entity);
            }

            if (je.contains("uiDocument") && je["uiDocument"].is_object()) {
                const json& ju = je["uiDocument"];
                UIDocumentComponent ud;
                ud.markup      = ju.value("markup", ud.markup);
                ud.stylesheet  = ju.value("stylesheet", ud.stylesheet);
                ud.sortOrder   = ju.value("sortOrder", ud.sortOrder);
                // Read with defaults, so a scene written before scaling existed
                // loads as Constant rather than as a zero-scale invisible UI.
                ud.scale.mode = ui::UIScaleMode(
                    ju.value("uiScaleMode", int(ud.scale.mode)));
                if (auto it = ju.find("uiScaleReference");
                    it != ju.end() && it->is_array() && it->size() == 2) {
                    ud.scale.reference.x = (*it)[0].get<float>();
                    ud.scale.reference.y = (*it)[1].get<float>();
                }
                ud.scale.match = ju.value("uiScaleMatch", ud.scale.match);
                ud.enabled     = ju.value("enabled", ud.enabled);
                ud.interactive = ju.value("interactive", ud.interactive);
                // Omitted means "the whole surface", so an older scene loads
                // unchanged rather than to a zero-area document.
                if (ju.contains("region") && ju["region"].is_array() &&
                    ju["region"].size() == 4) {
                    const json& r = ju["region"];
                    if (r[0].is_number() && r[1].is_number() &&
                        r[2].is_number() && r[3].is_number()) {
                        ud.regionX = r[0].get<float>();
                        ud.regionY = r[1].get<float>();
                        ud.regionW = r[2].get<float>();
                        ud.regionH = r[3].get<float>();
                    }
                }
                // Same containment gate as model, script, clip and HDRi paths,
                // and for the same reason: authored markup and stylesheets flow
                // straight into parsers. Drop just the path, keeping the
                // component, so a rejected asset degrades exactly like a
                // rejected model rather than deleting the entity's UI.
                for (auto* p : { &ud.markup, &ud.stylesheet }) {
                    if (p->empty()) continue;
                    std::filesystem::path contained;
                    if (!PathIsContained(/*baseDir=*/"", *p, contained)) {
                        std::cerr << "[SceneSerializer] rejected UI path outside the project: '"
                                  << *p << "'\n";
                        p->clear();
                    }
                }
                reg.emplace<UIDocumentComponent>(entity, ud);
            }

            // The probe never loads models, so `model` is always null there and
            // this block would be skipped — leaving every field inside
            // materialOverrides unvalidated, i.e. still able to throw during the
            // real load after the scene had been reset. Walk it in the probe
            // too, purely to force the same reads.
            // NO is_array() guard, deliberately. The real pass below does not
            // have one either: it range-fors straight into the value, so a
            // `"materialOverrides": 7` throws there — AFTER the registry has
            // been cleared and half-repopulated, which is exactly the
            // scene-destroying failure this probe exists to prevent. Guarding
            // here and not there made the probe pass the one shape it most
            // needed to catch.
            if (dryRun_ && je.contains("materialOverrides")) {
                for (const json& jo : je["materialOverrides"]) {
                    (void)jo.value("slot", size_t{ 0 });
                    Material probe;
                    (void)vec3FromJson(jo.value("baseColor", json()), probe.baseColor);
                    (void)vec3FromJson(jo.value("emissive", json()), probe.emissive);
                    (void)jo.value("metallic", probe.metallic);
                    (void)jo.value("roughness", probe.roughness);
                    (void)jo.value("ao", probe.ao);
                    (void)jo.value("alphaMode", static_cast<int>(probe.alphaMode));
                    (void)jo.value("opacity", probe.opacity);
                    (void)jo.value("alphaCutoff", probe.alphaCutoff);
                    (void)jo.value("doubleSided", probe.doubleSided);
                    (void)jo.value("shadingModel", static_cast<int>(probe.shadingModel));
                    (void)jo.value("toonBands", probe.toonBands);
                    (void)jo.value("toonSpecStrength", probe.toonSpecStrength);
                    (void)jo.value("toonSpecSize", probe.toonSpecSize);
                    (void)jo.value("toonRimStrength", probe.toonRimStrength);
                }
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
                    const int sm = jo.value("shadingModel", static_cast<int>(mat->shadingModel));
                    mat->shadingModel = (sm == static_cast<int>(ShadingModel::Toon))
                        ? ShadingModel::Toon : ShadingModel::PBR;
                    mat->toonBands        = jo.value("toonBands",        mat->toonBands);
                    mat->toonSpecStrength = jo.value("toonSpecStrength", mat->toonSpecStrength);
                    mat->toonSpecSize     = jo.value("toonSpecSize",     mat->toonSpecSize);
                    mat->toonRimStrength  = jo.value("toonRimStrength",  mat->toonRimStrength);
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
      catch (const std::exception& e) {
        // GetModel is the ONE call the probe cannot rehearse — it skips asset
        // loading by design — so a full import's bad_alloc or length_error is
        // not a json::exception and used to escape this function entirely,
        // killing the process mid-swap with the registry half-built.
        //
        // A mitigation, not a cure: the scene is still incomplete on this path.
        // It needs an out-of-memory to reach, and a reported failure beats a
        // crash.
        std::cerr << "ERROR::SCENE::LOAD_FAILED while importing assets for '"
                  << path << "': " << e.what() << std::endl;
        return false;
      }
      catch (...) {
        std::cerr << "ERROR::SCENE::LOAD_FAILED unknown error loading '"
                  << path << "'" << std::endl;
        return false;
      }
    }

} // namespace MyCoreEngine
