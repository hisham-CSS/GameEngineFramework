#include "MenuUIContent.h"

#include "UITextureCache.h"
#include "UIWorld.h"

#include "../audio/AudioWorld.h"
#include "../core/Application.h"
#include "../core/ProjectSettings.h"
#include "../core/Renderer.h"
#include "../core/Scene.h"
#include "../core/SceneLoader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace MyCoreEngine {

namespace {

    // The most recent swaps, newest LAST so the log reads top-to-bottom like a
    // console. Bounded: this is a demo instrument, not a diagnostic record, and
    // an unbounded vector behind a six-slot window is just a leak with a view.
    constexpr std::size_t kSwapLogMax = 24;

    struct SwapRow {
        std::string path;
        std::string status;
        bool ok = true;
    };

    // Keyed on the UIWorld so this file owns no singleton: a process has one
    // host, but a test can stand up several and they must not share a log.
    // Main thread only, like everything else in the UI.
    std::unordered_map<const UIWorld*, std::vector<SwapRow>>& swapLogs() {
        static std::unordered_map<const UIWorld*, std::vector<SwapRow>> m;
        return m;
    }

    bool allowed(const MenuUIHooks& h) {
        return !h.allowHostMutation || h.allowHostMutation();
    }

    std::string baseName(const std::string& p) {
        const std::size_t i = p.find_last_of("/\\");
        return i == std::string::npos ? p : p.substr(i + 1);
    }

    void setStatus(ui::UIDataSource& src, std::string msg, bool ok) {
        // Truncated rather than wrapped: the status line is one row in a fixed
        // panel, and a long asset path would otherwise reflow the whole card.
        if (msg.size() > 52) msg = msg.substr(0, 49) + "...";
        src.SetString("menuStatus", std::move(msg));
        src.SetBool("menuStatusOk", ok);
    }

    const char* qualityName(Scene::QualityLevel q) {
        switch (q) {
        case Scene::QualityLevel::Low:    return "LOW";
        case Scene::QualityLevel::Medium: return "MEDIUM";
        case Scene::QualityLevel::High:   return "HIGH";
        default:                          return "CUSTOM";
        }
    }

    // The tier drives three `classes=` toggles rather than a comparison in the
    // markup, because a hole is a PATH and there is deliberately no `==`.
    void publishQuality(ui::UIDataSource& src, Scene::QualityLevel q) {
        src.SetString("menuQualityName", qualityName(q));
        src.SetBool("menuQLow",  q == Scene::QualityLevel::Low);
        src.SetBool("menuQMed",  q == Scene::QualityLevel::Medium);
        src.SetBool("menuQHigh", q == Scene::QualityLevel::High);
    }

    void publishVolume(ui::UIDataSource& src, float v) {
        src.SetNumber("menuVolume", v);                       // 0..1, for a fill bar
        src.SetInt("menuVolumePct", (long long)std::lround(v * 100.0f));
    }

    // Rebuilt whenever the log changes. Same reasoning as the sample HUD's
    // inventory: SetList is equality-gated, so an identical list bumps no
    // version and wakes no binding, and building it eagerly is cheaper than
    // tracking which row moved.
    void publishSwapLog(ui::UIDataSource& src, const std::vector<SwapRow>& rows) {
        ui::UIList l;
        for (const SwapRow& r : rows) {
            ui::UIRecord& rec = l.Add();
            rec.SetString("path", r.path);
            rec.SetString("status", r.status);
            rec.SetBool("ok", r.ok);
        }
        src.SetList("swapLog", std::move(l));
    }

    void applyVolume(const MenuUIHooks& h, ui::UIDataSource& src, float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        if (h.audio) h.audio->SetMasterVolume(v);
        publishVolume(src, v);
        if (h.onMasterVolume) {
            // The host owns persistence. The editor does, because it keeps its
            // own mirror of a value AudioWorld cannot be asked for.
            h.onMasterVolume(v);
        } else {
            // Load-modify-save, so the fields ProjectSettings does not model
            // are at least preserved across this write.
            ProjectSettings s;
            s.Load();
            s.masterVolume = v;
            s.Save();
        }
    }

    void applyQuality(const MenuUIHooks& h, ui::UIDataSource& src,
                      Scene::QualityLevel q) {
        if (!h.scene || !h.renderer) return;
        h.scene->SetQualityLevel(q);
        h.renderer->ApplyQualityTier(q, *h.scene);
        publishQuality(src, q);
        // The CSM half of a tier lives on the Renderer and is not part of the
        // scene, so a host with two renderers has to rebuild both.
        if (h.onQualityChanged) h.onQualityChanged();
        setStatus(src, std::string("Quality: ") + qualityName(q), true);
    }

} // namespace

void InstallMenuUIContent(UIWorld& world, const MenuUIHooks& hooks) {
    ui::UIDataSource& src = world.shared();
    const MenuUIHooks h = hooks;   // by VALUE: the actions outlive the caller's

    swapLogs()[&world].clear();

    // ---- seed everything the markup binds to, BEFORE any document loads ----
    // A binding pass against a missing property is a diagnostic, not a crash,
    // but it is still noise the author has to read past.
    publishVolume(src, std::clamp(h.initialVolume, 0.0f, 1.0f));
    publishQuality(src, h.scene ? h.scene->GetQualityLevel()
                                : Scene::QualityLevel::High);
    src.SetBool("menuVSync", true);
    src.SetInt("menuSwaps", 0);
    src.SetInt("menuLiveDocs", 0);
    src.SetInt("menuObservers", 0);
    src.SetInt("menuTextures", 0);
    src.SetInt("swapLogCursor", 0);
    setStatus(src, "Ready.", true);
    publishSwapLog(src, {});

    // ---- the verbs ----

    // The whole point of the demo. DEFERRED by the loader: this runs inside
    // UIWorld::Update, inside the UI render pass, so the registry cannot be
    // replaced underneath it — Application drains the request at the next frame
    // boundary. LoadScene returns false when the file will not load, in which
    // case nothing whatsoever has been touched.
    src.AddAction("menuNewGame", [&src, h] {
        if (!h.app) return;
        if (!h.app->LoadScene(h.playScenePath)) {
            setStatus(src, "Cannot load " + baseName(h.playScenePath), false);
        }
    });

    // The return leg, called from the sample HUD. Same call, other direction:
    // there is nothing special about "the menu" to the loader.
    src.AddAction("menuBackToMenu", [&src, h] {
        if (!h.app) return;
        if (!h.app->LoadScene(h.menuScenePath)) {
            setStatus(src, "Cannot load " + baseName(h.menuScenePath), false);
        }
    });

    src.AddAction("menuQuit", [h] {
        if (h.onQuit) { h.onQuit(); return; }
        // Default: ask the host window to close, which unwinds RunLoop
        // normally rather than calling exit() out from under the GL context.
        if (h.app) {
            if (GLFWwindow* w = h.app->GetNativeWindow()) {
                glfwSetWindowShouldClose(w, GLFW_TRUE);
            }
        }
    });

    // Stepped buttons rather than a slider, because there is no slider widget
    // and faking one out of a drag would be a worse lie than a pair of buttons.
    // The bar next to them is a real bound width, so the value is still visible
    // as a quantity rather than only as a number.
    src.AddAction("menuVolumeUp", [&src, h] {
        if (!allowed(h)) return;
        applyVolume(h, src, float(src.GetNumber("menuVolume")) + h.volumeStep);
    });
    src.AddAction("menuVolumeDown", [&src, h] {
        if (!allowed(h)) return;
        applyVolume(h, src, float(src.GetNumber("menuVolume")) - h.volumeStep);
    });

    src.AddAction("menuQualityLow", [&src, h] {
        if (allowed(h)) applyQuality(h, src, Scene::QualityLevel::Low);
    });
    src.AddAction("menuQualityMedium", [&src, h] {
        if (allowed(h)) applyQuality(h, src, Scene::QualityLevel::Medium);
    });
    src.AddAction("menuQualityHigh", [&src, h] {
        if (allowed(h)) applyQuality(h, src, Scene::QualityLevel::High);
    });

    src.AddAction("menuVSyncToggle", [&src, h] {
        if (!allowed(h) || !h.app) return;
        const bool on = !src.GetBool("menuVSync");
        h.app->setVSync(on);
        src.SetBool("menuVSync", on);
        setStatus(src, on ? "VSync on" : "VSync off", true);
    });

    // The swap log's window, exactly like the sample HUD's inventory cursor:
    // a CURSOR fed to repeat-offset, clamped by the pool to the last full page
    // so the panel never shrinks to a partial window at either end.
    const auto moveCursor = [&src, &world](long long by) {
        const auto& rows = swapLogs()[&world];
        const long long last = rows.empty() ? 0 : (long long)rows.size() - 1;
        src.SetInt("swapLogCursor",
                   std::clamp(src.GetInt("swapLogCursor") + by, 0LL, last));
    };
    src.AddAction("menuLogPrev", [moveCursor] { moveCursor(-1); });
    src.AddAction("menuLogNext", [moveCursor] { moveCursor(+1); });
}

void MenuUIReportSwap(UIWorld& world, const SceneSwapResult& r) {
    // May touch shared() and NOTHING else — see the header. This can be
    // re-entered from inside a UI action via RequestSwap's synchronous failure
    // path, while UIWorld is mid-iteration over its documents.
    ui::UIDataSource& src = world.shared();
    std::vector<SwapRow>& rows = swapLogs()[&world];

    SwapRow row;
    row.path = baseName(r.path);
    switch (r.status) {
    case SceneSwapStatus::Ok:
        row.ok = r.report.complete();
        row.status = row.ok
            ? "loaded " + std::to_string(r.report.entitiesCreated)
            : "loaded, models missing";
        break;
    case SceneSwapStatus::Invalid:    row.ok = false; row.status = "invalid";    break;
    case SceneSwapStatus::Refused:    row.ok = false; row.status = "refused";    break;
    case SceneSwapStatus::LoadFailed: row.ok = false; row.status = "load failed"; break;
    case SceneSwapStatus::Superseded: row.ok = false; row.status = "superseded"; break;
    }

    setStatus(src, row.path + ": " + row.status, row.ok);

    rows.push_back(std::move(row));
    if (rows.size() > kSwapLogMax) rows.erase(rows.begin());
    publishSwapLog(src, rows);
    src.SetInt("menuSwaps", (long long)rows.size());
    // Follow the newest entry, so a swap you just caused is the one on screen.
    src.SetInt("swapLogCursor", rows.empty() ? 0 : (long long)rows.size() - 1);
}

void MenuUIPublishCounters(UIWorld& world, const MenuUIHooks& hooks) {
    ui::UIDataSource& src = world.shared();
    src.SetInt("menuLiveDocs", (long long)world.liveCount());
    if (const ui::UITextureCache* tc = world.textureCache()) {
        src.SetInt("menuTextures", (long long)tc->size());
    }
    // The number that makes "the tenth swap costs what the first did" something
    // you can look at. A rebuild implemented by re-running the Install helpers
    // would show up here as a count that climbs by three per swap.
    if (hooks.app) {
        if (const SceneLoader* l = hooks.app->sceneLoader()) {
            src.SetInt("menuObservers", (long long)l->observerCount());
        }
    }
}

} // namespace MyCoreEngine
