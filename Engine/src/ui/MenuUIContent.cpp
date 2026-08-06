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

    // Per-world volume reconciliation state. See MenuUIPublishCounters.
    struct VolumeWatch {
        float last = -1.0f;      // -1 => not primed
        float pending = 0.0f;    // what to write when it settles
        int   stillFor = 0;      // frames since it last moved
        bool  dirty = false;     // moved since the last disk write
    };

    // ~0.25s at 60Hz. Long enough that a drag never touches the disk mid-gesture,
    // short enough that letting go and immediately quitting still saves.
    constexpr int kVolumeSettleFrames = 15;

    struct Log {
        std::vector<SwapRow> rows;
        // MONOTONIC, and deliberately not rows.size(): the log is a console
        // that caps and can be cleared, while "how many swaps has this session
        // done" is the number the demo actually asks you to watch. Tying them
        // together meant the counter silently stopped climbing at the cap.
        long long lifetimeSwaps = 0;
    };

    // Keyed on the UIWorld so this file owns no singleton: a process has one
    // host, but a test can stand up several and they must not share a log.
    // Main thread only, like everything else in the UI.
    std::unordered_map<const UIWorld*, Log>& swapLogs() {
        static std::unordered_map<const UIWorld*, Log> m;
        return m;
    }

    std::unordered_map<const UIWorld*, VolumeWatch>& volumeWatches() {
        static std::unordered_map<const UIWorld*, VolumeWatch> m;
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

    // The LIVE half: what you hear, every frame it moves.
    void applyVolumeLive(const MenuUIHooks& h, ui::UIDataSource& src, float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        if (h.audio) h.audio->SetMasterVolume(v);
        publishVolume(src, v);
    }

    // The PERSISTED half, which touches the disk and so must not run per frame.
    void commitVolume(const MenuUIHooks& h, float v) {
        v = std::clamp(v, 0.0f, 1.0f);
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

    // The stepped buttons still exist for a mouse user who wants an exact
    // notch. They commit immediately: a click is already a settled gesture.
    //
    // ...and then tell the watch so, or it would see menuVolume move, mark
    // itself dirty, and commit the SAME value again a quarter of a second
    // later. Every writer of this value has to leave the watch agreeing with
    // the source, which is the price of having two paths to one setting.
    void applyVolume(const UIWorld& world, const MenuUIHooks& h,
                     ui::UIDataSource& src, float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        applyVolumeLive(h, src, v);
        commitVolume(h, v);
        VolumeWatch& vw = volumeWatches()[&world];
        vw.last = v;
        vw.pending = v;
        vw.stillFor = 0;
        vw.dirty = false;
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

    swapLogs()[&world] = Log{};
    volumeWatches()[&world] = VolumeWatch{};

    // ---- seed everything the markup binds to, BEFORE any document loads ----
    // A binding pass against a missing property is a diagnostic, not a crash,
    // but it is still noise the author has to read past.
    publishVolume(src, std::clamp(h.initialVolume, 0.0f, 1.0f));
    publishQuality(src, h.scene ? h.scene->GetQualityLevel()
                                : Scene::QualityLevel::High);
    src.SetBool("menuVSync", true);
    // A bool has no text form a hole can render -- `{flag | int}` reads as 1/0 --
    // so the LABEL is published next to the flag, the same way the quality tier
    // publishes a name beside its three booleans.
    src.SetString("menuVSyncName", "ON");
    src.SetInt("menuSwaps", 0);
    src.SetInt("menuLiveDocs", 0);
    src.SetInt("menuObservers", 0);
    src.SetInt("menuTextures", 0);
    src.SetInt("swapLogCursor", 0);
    // SEEDED, not merely published each frame. MenuUIPublishCounters only runs
    // when a host with an Application is driving, and a document binds at LOAD
    // -- so without these the menu reports three unresolved holes on any host
    // that has not started one yet, which includes every test.
    src.SetBool("menuLoading", false);
    src.SetInt("menuLoadDone", 0);
    src.SetInt("menuLoadTotal", 0);
    src.SetInt("menuLoadPct", 100);
    // WHICH PANEL IS OPEN. Three bools rather than a string, because a hole is
    // a PATH and there is deliberately no `==` in the markup -- the same reason
    // the quality tier publishes three bools beside its name.
    //
    // These drive `if=`, which drives DISPLAY, which is what the focus scope
    // stack follows. One direction of causality: the app owns "open", the
    // markup owns "visible", and the stack owns "where navigation goes".
    src.SetBool("panelSettings", false);
    src.SetBool("panelSystem", false);
    // Derived, and published rather than expressed in markup: a hole is a PATH,
    // so there is no `||` to write `if="!panelSettings && !panelSystem"` with.
    // One bool the verb column hides on, however many panels there end up being.
    src.SetBool("panelOpen", false);
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
        // ASYNC: the game scene has models, and this is the one swap a player
        // actually waits on. The menu stays up and responsive while they warm.
        if (!h.app->LoadSceneAsync(h.playScenePath)) {
            setStatus(src, "Cannot load " + baseName(h.playScenePath), false);
        }
    });

    // The return leg, called from the sample HUD. Same call, other direction:
    // there is nothing special about "the menu" to the loader.
    src.AddAction("menuBackToMenu", [&src, h] {
        if (!h.app) return;
        // Async here too, for one rule rather than two. A scene with no models
        // warms nothing and drains on the very next frame, so this is exactly
        // the synchronous call until the day the menu grows some.
        if (!h.app->LoadSceneAsync(h.menuScenePath)) {
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
    src.AddAction("menuVolumeUp", [&src, &world, h] {
        if (!allowed(h)) return;
        applyVolume(world, h, src, float(src.GetNumber("menuVolume")) + h.volumeStep);
    });
    src.AddAction("menuVolumeDown", [&src, &world, h] {
        if (!allowed(h)) return;
        applyVolume(world, h, src, float(src.GetNumber("menuVolume")) - h.volumeStep);
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
        src.SetString("menuVSyncName", on ? "ON" : "OFF");
        setStatus(src, on ? "VSync on" : "VSync off", true);
    });

    // The swap log's window, exactly like the sample HUD's inventory cursor:
    // a CURSOR fed to repeat-offset, clamped by the pool to the last full page
    // so the panel never shrinks to a partial window at either end.
    const auto moveCursor = [&src, &world](long long by) {
        const auto& rows = swapLogs()[&world].rows;
        const long long last = rows.empty() ? 0 : (long long)rows.size() - 1;
        src.SetInt("swapLogCursor",
                   std::clamp(src.GetInt("swapLogCursor") + by, 0LL, last));
    };
    // Clears the CONSOLE, not the counter. Clearing a log has never reset an
    // uptime, and the swap count is the thing the SYSTEM tab asks you to watch
    // across a session.
    src.AddAction("menuLogClear", [&src, &world] {
        Log& log = swapLogs()[&world];
        if (log.rows.empty()) return;   // equality-gated anyway, but say it
        log.rows.clear();
        publishSwapLog(src, log.rows);
        src.SetInt("swapLogCursor", 0);
    // SEEDED, not merely published each frame. MenuUIPublishCounters only runs
    // when a host with an Application is driving, and a document binds at LOAD
    // -- so without these the menu reports three unresolved holes on any host
    // that has not started one yet, which includes every test.
    src.SetBool("menuLoading", false);
    src.SetInt("menuLoadDone", 0);
    src.SetInt("menuLoadTotal", 0);
    src.SetInt("menuLoadPct", 100);
        setStatus(src, "Log cleared.", true);
    });

    // Opening a panel is one bool. Closing it is the same bool, and `on-back`
    // on the panel's focus-scope root is what B and Escape invoke -- so a pad,
    // a keyboard and a mouse all close it through one path.
    const auto openPanel = [&src](const char* which) {
        src.SetBool("panelSettings", which && std::string(which) == "settings");
        src.SetBool("panelSystem",   which && std::string(which) == "system");
        src.SetBool("panelOpen",     which != nullptr);
    };
    src.AddAction("menuOpenSettings", [openPanel] { openPanel("settings"); });
    src.AddAction("menuOpenSystem",   [openPanel] { openPanel("system"); });
    src.AddAction("menuClosePanel",   [openPanel] { openPanel(nullptr); });

    src.AddAction("menuLogPrev", [moveCursor] { moveCursor(-1); });
    src.AddAction("menuLogNext", [moveCursor] { moveCursor(+1); });
}

void MenuUIReportSwap(UIWorld& world, const SceneSwapResult& r) {
    // May touch shared() and NOTHING else — see the header. This can be
    // re-entered from inside a UI action via RequestSwap's synchronous failure
    // path, while UIWorld is mid-iteration over its documents.
    ui::UIDataSource& src = world.shared();
    Log& log = swapLogs()[&world];
    std::vector<SwapRow>& rows = log.rows;

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
    src.SetInt("menuSwaps", ++log.lifetimeSwaps);
    // Follow the newest entry, so a swap you just caused is the one on screen.
    src.SetInt("swapLogCursor", rows.empty() ? 0 : (long long)rows.size() - 1);
}

void MenuUIPublishCounters(UIWorld& world, const MenuUIHooks& hooks) {
    ui::UIDataSource& src = world.shared();

    // ---- loading progress, for a screen that has to say something ----------
    // Published unconditionally: the setters are equality-gated, so the frames
    // where nothing is loading cost one compare and wake nothing.
    if (hooks.app && hooks.app->sceneLoader()) {
        const SceneLoader& sl = *hooks.app->sceneLoader();
        const std::size_t total = sl.prewarmTotal();
        const std::size_t done = sl.prewarmDone();
        src.SetBool("menuLoading", sl.swapPrewarming());
        src.SetInt("menuLoadDone", (long long)done);
        src.SetInt("menuLoadTotal", (long long)total);
        src.SetInt("menuLoadPct",
                   total ? (long long)((done * 100) / total) : 100);
    }

    // ---- master volume, written straight into the source by the <Slider> ----
    VolumeWatch& vw = volumeWatches()[&world];
    const float now = std::clamp(src.GetNumber("menuVolume", 0.0f), 0.0f, 1.0f);
    if (vw.last < 0.0f) {
        vw.last = now;                       // prime, without a spurious write
    } else if (now != vw.last) {
        vw.last = now;
        vw.pending = now;
        vw.stillFor = 0;
        vw.dirty = true;
        // Audio follows the drag with no delay -- the whole point of a slider
        // is hearing what you are choosing.
        if (hooks.audio) hooks.audio->SetMasterVolume(now);
        // Keep the percentage readout in step. publishVolume would rewrite
        // menuVolume itself, which is where the slider's value LIVES, so only
        // the derived field is touched here.
        src.SetInt("menuVolumePct", (long long)std::lround(now * 100.0f));
    } else if (vw.dirty && ++vw.stillFor >= kVolumeSettleFrames) {
        commitVolume(hooks, vw.pending);
        vw.dirty = false;
    }
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
