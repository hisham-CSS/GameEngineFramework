#include "BuildSettingsPanel.h"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

using MyCoreEngine::BuildPhase;
using MyCoreEngine::BuildProfile;
using MyCoreEngine::BuildResult;
using MyCoreEngine::BuildSettings;
using MyCoreEngine::BuildSettingsStatus;

namespace {

    // Same three colours the Combo Prover uses, so a refusal reads the same way
    // in both panels.
    const ImVec4 kBad (1.00f, 0.35f, 0.30f, 1.f);
    const ImVec4 kWarn(1.00f, 0.60f, 0.20f, 1.f);
    const ImVec4 kGood(0.40f, 0.90f, 0.40f, 1.f);

    void textDim(const char* s) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(s);
        ImGui::PopStyleColor();
    }

    void wrapped(const ImVec4& colour, const std::string& s) {
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextWrapped("%s", s.c_str());
        ImGui::PopStyleColor();
    }

    void tip(const char* s) {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s);
    }

    // 64-bit mixing, FNV-1a's constants. Cheap enough to run over the whole
    // scene list every frame -- which is the point: see note 3 in the header.
    // The alternative is a dirty flag somebody eventually forgets to set, and
    // its failure mode is a row that says MISSING about a file that is there.
    std::uint64_t mix(std::uint64_t h, const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= '|';               // a separator, so ["ab","c"] != ["a","bc"]
        h *= 1099511628211ull;
        return h;
    }

    std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xffu;
            h *= 1099511628211ull;
        }
        return h;
    }

} // namespace

// ===========================================================================
// The disk checks. Run when a fingerprint moves, never per frame.
// ===========================================================================
void BuildSettingsPanel::refreshScenes_(const BuildSettings& settings)
{
    namespace fs = std::filesystem;

    rows_.clear();
    rows_.reserve(settings.scenes.size());
    for (const std::string& rel : settings.scenes) {
        SceneRow row;
        fs::path full;
        // The base is the WORKING DIRECTORY, which is how every loader in the
        // engine resolves an "Exported/..." path and is the default AddScene
        // itself uses. Checking containment before the stat is not ceremony: a
        // path this refuses is one the panel must not turn into a filesystem
        // call at all, and it is a different answer from "not there" (see
        // SceneRowState).
        if (!MyCoreEngine::PathIsContained(std::string(), rel, full)) {
            row.state = SceneRowState::Refused;
            row.detail = "'" + rel + "' is absolute, names a drive/UNC root, or "
                         "escapes the project with '..'";
        }
        else {
            std::error_code ec;
            // is_regular_file rather than exists: a DIRECTORY called menu.json
            // is not a scene, and reporting it as present would move the failure
            // to the middle of a build. The error_code overload rather than the
            // throwing one, for the reason BuildSettings::Load records about
            // exists(): a path on a disconnected network drive throws from the
            // plain form, and a panel asking "is my scene there" must not take an
            // exception for an answer.
            row.state = fs::is_regular_file(full, ec) ? SceneRowState::Present
                                                      : SceneRowState::Missing;
        }
        rows_.push_back(std::move(row));
    }
    haveRows_ = true;
}

void BuildSettingsPanel::refreshOutput_(const BuildSettings& settings,
                                        const MyCoreEngine::BuildPreflight* preflight)
{
    namespace fs = std::filesystem;

    output_ = OutputCheck{};

    if (settings.outputDirectory.empty()) {
        output_.spellingOk = false;
        output_.refusal = "The output directory is empty. A build has nowhere to go.";
    }
    else {
        fs::path ignored;
        if (!MyCoreEngine::PathIsContained(std::string(), settings.outputDirectory,
                                           ignored)) {
            output_.spellingOk = false;
            // Phrased as the CONSEQUENCE rather than the rule. This directory is
            // written into and then MIRRORED -- files the build did not write are
            // removed -- so an escaping path is a delete primitive with a text
            // field in front of it, and that is what the author needs to hear.
            output_.refusal =
                "This path is absolute, names a drive/UNC root, or escapes the "
                "project with '..'. A build writes into this directory and then "
                "mirrors it, so it must stay inside the project.";
        }
    }

    // WHERE IT ACTUALLY LANDS IS PREFLIGHT'S ANSWER, not the panel's: the path
    // is relative to BuildEnvironment::outputRoot, which this panel does not
    // know and must not guess at. Without a preflight the panel says so rather
    // than resolving against the working directory and being confidently wrong.
    if (preflight && !preflight->resolvedOutputDir.empty()) {
        output_.haveResolved = true;
        std::error_code ec;
        output_.resolvedExists =
            fs::is_directory(fs::path(preflight->resolvedOutputDir), ec) && !ec;
    }
    haveOutput_ = true;
}

// ===========================================================================
// Sections
// ===========================================================================
void BuildSettingsPanel::drawFileState_(const BuildPanelInputs& in,
                                        BuildSettingsActions& a)
{
    switch (in.load.status) {
    case BuildSettingsStatus::Ok:
        if (in.dirty) {
            ImGui::TextColored(kWarn, "Unsaved changes");
            ImGui::SameLine();
            textDim("(Build saves first)");
        }
        else {
            textDim("Exported/build.json");
        }
        break;

    case BuildSettingsStatus::Missing:
        // Not a problem, and it must not read as one. Every project that
        // predates the build list is in this state on first open, and the file
        // appears the moment anything is saved.
        textDim(in.dirty ? "No Exported/build.json yet -- Save writes one"
                         : "No Exported/build.json yet");
        break;

    case BuildSettingsStatus::Malformed:
        // Note 4. The whole page is read-only from here; everything below draws
        // greyed and neither Save nor Build is offered.
        ImGui::TextColored(kBad, "Exported/build.json could not be read.");
        wrapped(kBad, in.load.message);
        ImGui::TextWrapped(
            "What is on screen is this session's defaults, NOT that file. Saving "
            "would overwrite it, so saving and building are both off until it is "
            "fixed or explicitly discarded.");
        if (ImGui::Button("Reload")) a.reloadRequested = true;
        tip("Re-read Exported/build.json from disk.\n"
            "Fix the file in a text editor first -- the message above names the key.");
        ImGui::SameLine();
        if (ImGui::Button("Discard the file...")) discardConfirm_ = true;
        tip("Overwrite Exported/build.json with what is on screen.\n"
            "Whatever that file contained is lost.");
        break;
    }

    if (discardConfirm_) {
        ImGui::OpenPopup("Discard build.json?");
        discardConfirm_ = false;
    }
    if (ImGui::BeginPopupModal("Discard build.json?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Overwrite Exported/build.json with the settings on screen?");
        ImGui::TextUnformatted("Whatever that file contains now is lost.");
        ImGui::Separator();
        if (ImGui::Button("Overwrite", ImVec2(120, 0))) {
            a.discardRequested = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void BuildSettingsPanel::drawScenes_(BuildSettings& settings,
                                     const BuildPanelInputs& in,
                                     BuildSettingsActions& a)
{
    ImGui::SeparatorText("Scenes in build");

    // DEFERRED, like SceneHierarchyPanel's PendingAction: every one of these
    // mutates the vector the loop below is walking.
    int pendingRemove = -1;
    int moveFrom = -1, moveTo = -1;

    if (settings.scenes.empty()) {
        ImGui::TextColored(kWarn, "This build has no scenes.");
        ImGui::TextWrapped("A build with no scenes has nothing to boot, and "
                           "preflight refuses it. Add one below.");
    }

    for (std::size_t i = 0; i < settings.scenes.size(); ++i) {
        const std::string& path = settings.scenes[i];
        const SceneRow row = (haveRows_ && i < rows_.size()) ? rows_[i] : SceneRow{};
        ImGui::PushID(widgetId_++);

        const bool first = (i == 0);
        const bool last  = (i + 1 == settings.scenes.size());

        if (ImGui::SmallButton("X")) pendingRemove = static_cast<int>(i);
        tip("Remove from the build.\nThe scene file is not deleted.");
        ImGui::SameLine(0.f, 2.f);
        ImGui::BeginDisabled(first);
        if (ImGui::SmallButton("^")) { moveFrom = (int)i; moveTo = (int)i - 1; }
        ImGui::EndDisabled();
        ImGui::SameLine(0.f, 2.f);
        ImGui::BeginDisabled(last);
        if (ImGui::SmallButton("v")) { moveFrom = (int)i; moveTo = (int)i + 1; }
        ImGui::EndDisabled();
        ImGui::SameLine(0.f, 8.f);

        // The row itself. Colour carries the state: a present scene reads
        // normally, a boot scene reads green, anything the build cannot open
        // reads red -- and that is the whole of note 3's "on the row, not in a
        // list at the bottom".
        const bool bad = row.state != SceneRowState::Present;
        if (bad)             ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        else if (first)      ImGui::PushStyleColor(ImGuiCol_Text, kGood);

        char label[512];
        std::snprintf(label, sizeof(label), "%2d  %s", (int)i, path.c_str());
        // WIDTH RESERVED FOR THE TAG. A Selectable defaults to the whole
        // remaining width, which would push BOOTS/MISSING off the right edge --
        // and the tag is the half of the row this panel exists to show.
        float rowW = ImGui::GetContentRegionAvail().x - 76.f;
        if (rowW < 80.f) rowW = 80.f;
        ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick,
                          ImVec2(rowW, 0.f));
        if (bad || first) ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            !in.playing && row.state == SceneRowState::Present) {
            a.openScene = path;
        }

        // Reorder by drag. `to` is the target's CURRENT index, which is exactly
        // what MoveScene's "index after removal" means for a drop: the dragged
        // row takes the target row's place, dragging up or down alike.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            // A LOCAL is enough: SetDragDropPayload COPIES into ImGui's own
            // buffer, so nothing here has to outlive the call.
            const int from = static_cast<int>(i);
            ImGui::SetDragDropPayload(kRowPayload, &from, sizeof(from));
            ImGui::Text("%d  %s", (int)i, path.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload(kRowPayload)) {
                const int from = *(const int*)pl->Data;
                // Re-validated against the CURRENT size: the payload is an index
                // captured at drag start and a context menu can remove a row
                // while the drag is in flight.
                if (from >= 0 && from < (int)settings.scenes.size()) {
                    moveFrom = from;
                    moveTo   = static_cast<int>(i);
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Make Boot Scene", nullptr, false, !first)) {
                moveFrom = (int)i;
                moveTo   = 0;
            }
            if (ImGui::MenuItem("Open Scene", nullptr, false,
                                !in.playing && row.state == SceneRowState::Present)) {
                a.openScene = path;
            }
            if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(path.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from Build")) pendingRemove = (int)i;
            ImGui::EndPopup();
        }

        // The tag, right of the path. SameLine ONLY when there is one to draw:
        // a SameLine with nothing after it leaves the cursor parked, and the
        // next row's first button lands beside this row instead of under it.
        if (row.state == SceneRowState::Missing) {
            ImGui::SameLine();
            ImGui::TextColored(kBad, "MISSING");
            tip("No such file. Preflight refuses a build whose list names a scene\n"
                "the asset root does not have -- it would be a level that is not\n"
                "in the game. Fix the path or remove the row.");
        }
        else if (row.state == SceneRowState::Refused) {
            ImGui::SameLine();
            ImGui::TextColored(kBad, "REFUSED");
            tip(row.detail.c_str());
        }
        else if (first) {
            ImGui::SameLine();
            ImGui::TextColored(kGood, "BOOTS");
            tip("The built game starts here. Drag another row to the top, or\n"
                "right-click it and choose Make Boot Scene, to change that.");
        }

        ImGui::PopID();
    }

    if (pendingRemove >= 0) {
        if (settings.RemoveSceneAt(static_cast<std::size_t>(pendingRemove)))
            a.settingsChanged = true;
    }
    else if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
        if (settings.MoveScene(static_cast<std::size_t>(moveFrom),
                               static_cast<std::size_t>(moveTo)))
            a.settingsChanged = true;
    }

    // What boots, spelled out under the list rather than left implicit in a
    // green row -- and the caveat with it, because the caveat is the difference
    // between what this panel says and what the bundle does. See note 7.
    if (!settings.scenes.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("The built game boots index 0: %s",
                           settings.StartupScene().c_str());
        textDim("...provided the player honours the build manifest flag the build "
                "writes (ProjectSettings::startupSceneFromBuild). A build linking "
                "a title whose player still resolves its own front end first will "
                "start there instead.");
    }
    ImGui::Spacing();
    textDim("Dropping a scene removes the scene, not its assets: the models, "
            "textures and UI it used still ship. Trimming a bundle needs an asset "
            "dependency graph, which does not exist yet.");
}

void BuildSettingsPanel::drawAddRow_(BuildSettings& settings,
                                     const BuildPanelInputs& in,
                                     BuildSettingsActions& a)
{
    ImGui::Spacing();
    // Leaves room for BOTH buttons on the same line; a negative width means
    // "everything except this much", so the field grows with the panel.
    ImGui::SetNextItemWidth(-190.f);
    const bool entered = ImGui::InputText("##addscene", addBuf_, sizeof(addBuf_),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool pressed = ImGui::Button("Add");
    tip("Append a scene to the build, as it is spelled by the loaders:\n"
        "working-directory-relative, with the Exported/ prefix.");

    if (entered || pressed) {
        addError_.clear();
        std::string why;
        // THROUGH AddScene, never a push_back: it is what normalizes the
        // spelling, applies the sandbox and refuses duplicates, and a panel that
        // rolled its own would drift from what Load accepts next launch. The
        // symptom of that drift is a list that saves fine and refuses to load.
        if (settings.AddScene(addBuf_, &why)) {
            addBuf_[0] = '\0';
            a.settingsChanged = true;
        }
        else {
            addError_ = why;
        }
    }

    ImGui::SameLine();
    const bool haveCurrent = !in.currentScene.empty();
    ImGui::BeginDisabled(!haveCurrent);
    if (ImGui::Button("Add Current")) {
        addError_.clear();
        std::string why;
        if (settings.AddScene(in.currentScene, &why)) a.settingsChanged = true;
        else                                          addError_ = why;
    }
    ImGui::EndDisabled();
    tip(haveCurrent ? "Add the scene currently open in the editor."
                    : "The open scene has no file yet. Save it first.");

    if (!addError_.empty()) wrapped(kBad, addError_);
}

void BuildSettingsPanel::drawExcluded_(BuildSettings& settings,
                                       const BuildPanelInputs& in,
                                       BuildSettingsActions& a)
{
    // Scenes the asset root HAS that this build leaves out. Straight from
    // preflight, which already walks the staged tree to compute exactly this --
    // so the panel gets Unity's "scenes in project, not in build" list for free
    // and the author almost never has to type a path.
    if (!in.preflight || in.preflight->scenesExcluded.empty()) return;

    char header[96];
    std::snprintf(header, sizeof(header), "Not in this build (%d)###notinbuild",
                  (int)in.preflight->scenesExcluded.size());
    ImGui::SetNextItemOpen(showExcluded_, ImGuiCond_Always);
    showExcluded_ = ImGui::CollapsingHeader(header);
    if (!showExcluded_) return;

    if (in.preflightStale) {
        textDim("(from the last check; the list has changed since)");
    }
    for (const std::string& path : in.preflight->scenesExcluded) {
        ImGui::PushID(widgetId_++);
        if (ImGui::SmallButton("+")) {
            addError_.clear();
            std::string why;
            if (settings.AddScene(path, &why)) a.settingsChanged = true;
            else                               addError_ = why;
        }
        ImGui::SameLine();
        textDim(path.c_str());
        ImGui::PopID();
    }
}

void BuildSettingsPanel::drawOutput_(BuildSettings& settings,
                                     const BuildPanelInputs& in,
                                     BuildSettingsActions& a)
{
    ImGui::SeparatorText("Output");

    // Seed the edit buffer only when somebody ELSE changed the value -- a
    // Reload, a Discard, the first load. Copying unconditionally would fight the
    // author's own keystrokes; not copying at all would leave a reloaded file
    // invisible behind a stale buffer.
    if (settings.outputDirectory != lastSeenOutput_) {
        std::snprintf(outputBuf_, sizeof(outputBuf_), "%s",
                      settings.outputDirectory.c_str());
        lastSeenOutput_ = settings.outputDirectory;
    }

    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##outdir", outputBuf_, sizeof(outputBuf_))) {
        settings.outputDirectory = outputBuf_;
        lastSeenOutput_ = settings.outputDirectory;
        a.settingsChanged = true;
    }
    textDim("Relative to the project. The build creates this directory and owns it.");

    if (!output_.spellingOk) {
        wrapped(kBad, output_.refusal);
    }

    if (output_.haveResolved && in.preflight) {
        ImGui::TextWrapped("-> %s", in.preflight->resolvedOutputDir.c_str());
        if (in.preflightStale) textDim("(resolved at the last check)");
        if (output_.resolvedExists) {
            // A statement of CONSEQUENCE, not a verdict. Whether an existing
            // directory may be used at all is preflight's call (it looks for the
            // marker a previous build left), and the panel does not second-guess
            // it -- but the mirroring is worth saying either way, because it is
            // the part that deletes.
            wrapped(kWarn,
                    "This directory already exists. A build MIRRORS its output: "
                    "files it did not write are removed, so a stale PlayerDebug.exe "
                    "from a previous profile does not survive into a Shipping "
                    "bundle.");
        }
    }
    else {
        textDim("Where this lands is resolved by the check below.");
    }
}

void BuildSettingsPanel::drawProfile_(BuildSettings& settings,
                                      BuildSettingsActions& a)
{
    ImGui::SeparatorText("Profile");

    ImGui::SetNextItemWidth(200.f);
    if (ImGui::BeginCombo("##profile",
                          MyCoreEngine::BuildProfileDisplayName(settings.profile))) {
        // The three, in enum order. Written out rather than looped over a
        // count, because a fourth profile should make somebody edit this list
        // and decide where it belongs in the order.
        const BuildProfile all[] = { BuildProfile::Debug,
                                     BuildProfile::Development,
                                     BuildProfile::Shipping };
        for (BuildProfile p : all) {
            const bool sel = (p == settings.profile);
            if (ImGui::Selectable(MyCoreEngine::BuildProfileDisplayName(p), sel)) {
                if (!sel) {
                    settings.profile = p;
                    a.settingsChanged = true;
                }
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // What the profile RESOLVES TO, from the accessors rather than from
    // preflight: these three are pure functions of the profile, they are correct
    // the instant the combo changes, and waiting for a preflight to restate them
    // would leave the page showing the previous profile's target.
    ImGui::SameLine();
    textDim((std::string(MyCoreEngine::BuildProfileCMakeConfig(settings.profile)) +
             "  /  " + MyCoreEngine::BuildProfileCMakeTarget(settings.profile) +
             "  ->  " + MyCoreEngine::BuildPlayerExecutableName(settings.profile))
                .c_str());

    switch (settings.profile) {
    case BuildProfile::Shipping:
        textDim("What you give somebody: no console, and a startup failure becomes "
                "a message box instead of a silent exit.");
        break;
    case BuildProfile::Development:
        textDim("Optimized, with symbols and a console. The one that is both "
                "playable and diagnosable.");
        break;
    case BuildProfile::Debug:
        textDim("Unoptimized. For attaching a debugger to the PLAYER; too slow to "
                "judge a game by.");
        break;
    }
}

void BuildSettingsPanel::drawPreflight_(const BuildPanelInputs& in,
                                        BuildSettingsActions& a)
{
    ImGui::SeparatorText("Check");

    if (ImGui::Button("Check")) a.preflightRequested = true;
    tip("Validate the settings against this tree. Creates nothing, builds nothing.\n"
        "Not free: it stats every listed scene and walks the staged asset root.");
    ImGui::SameLine();

    if (!in.preflight) {
        textDim("Not checked yet.");
        return;
    }
    if (in.preflightStale) {
        ImGui::TextColored(kWarn, "The settings changed since this check.");
    }
    else if (in.preflight->ok) {
        ImGui::TextColored(kGood, "Ready to build.");
    }
    else {
        ImGui::TextColored(kBad, "This build would be refused:");
    }

    // EVERY reason, which is what BuildPreflight::errors is a vector for. One
    // problem per attempt makes the author play twenty questions with their own
    // settings.
    for (const std::string& e : in.preflight->errors)   wrapped(kBad, "- " + e);
    for (const std::string& w : in.preflight->warnings) wrapped(kWarn, "- " + w);

    if (!in.preflight->resolvedExeDir.empty()) {
        textDim(("Built from: " + in.preflight->resolvedExeDir).c_str());
    }
}

void BuildSettingsPanel::drawBuildBar_(const BuildSettings& settings,
                                       const BuildPanelInputs& in,
                                       BuildSettingsActions& a)
{
    ImGui::SeparatorText("Build");

    if (!in.environmentProblem.empty()) {
        wrapped(kBad, in.environmentProblem);
    }

    // WHY BUILD IS OFF, ONE REASON, CHOSEN IN THE ORDER THE AUTHOR CAN ACT ON.
    // A disabled button with no sentence beside it is the thing this panel is
    // supposed to prevent. Computed only when idle: while a build runs the
    // progress bar is the answer, and "the check above failed" printed next to a
    // running compile is a sentence about the wrong thing.
    const char* blocked = nullptr;
    if (!in.building) {
        if (!in.load.safeToSave()) {
            blocked = "build.json could not be read; fix or discard it first.";
        }
        else if (!in.environmentProblem.empty()) {
            blocked = "the editor does not know where its build tree is.";
        }
        else if (in.playing) {
            // Note 8: a compile re-stages the runtime Exported/ directory, and
            // that staging MIRRORS. Reading assets out of a directory being
            // rewritten under you can fail with no visible cause.
            blocked = "stop play mode first: a build re-stages the assets the "
                      "running session is loading from.";
        }
        else if (settings.scenes.empty()) {
            blocked = "the build has no scenes.";
        }
        else if (!output_.spellingOk) {
            blocked = "the output directory is not a path a build may write to.";
        }
        else if (in.preflight && !in.preflightStale && !in.preflight->ok) {
            // Only a FRESH failing check blocks. A stale or absent one does not:
            // BuildJob::Start re-runs preflight itself and reports the refusal
            // properly, so refusing here on out-of-date information would be the
            // panel blocking a build that is actually fine.
            blocked = "the check above failed.";
        }
    }

    ImGui::BeginDisabled(in.building || blocked != nullptr);
    if (ImGui::Button("Build", ImVec2(120, 0))) a.buildRequested = true;
    ImGui::EndDisabled();
    tip("Compile the profile's player target, then assemble the bundle.\n"
        "Unsaved settings are written to build.json first, so the bundle and\n"
        "the file agree about what shipped.");

    ImGui::SameLine();
    ImGui::BeginDisabled(!in.building);
    if (ImGui::Button("Cancel", ImVec2(90, 0))) a.cancelRequested = true;
    ImGui::EndDisabled();
    tip("Kill the child process and remove whatever had been written.\n"
        "A partial bundle is worse than none: it has a Player.exe in it and starts.");

    ImGui::SameLine();
    ImGui::BeginDisabled(in.building || !in.load.safeToSave() || !in.dirty);
    if (ImGui::Button("Save")) a.saveRequested = true;
    ImGui::EndDisabled();

    // On its own line and WRAPPED. Beside the buttons it would be clipped by the
    // window edge, and a refusal that only fits in a wide layout is a refusal
    // half the sessions never see.
    if (blocked) wrapped(kWarn, blocked);

    if (in.building) {
        // NEGATIVE MEANS INDETERMINATE and is drawn as such. ImGui animates a
        // sliding bar for a negative fraction; inventing a percentage that sits
        // at 40% for two minutes is what makes people kill a working build.
        if (in.progress.fraction < 0.f) {
            ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1.f, 0.f), "");
        }
        else {
            ImGui::ProgressBar(in.progress.fraction, ImVec2(-1.f, 0.f));
        }
        ImGui::Text("%s", MyCoreEngine::BuildPhaseName(in.progress.phase));
        ImGui::SameLine();
        ImGui::TextWrapped("%s", in.progress.status.c_str());
    }
}

void BuildSettingsPanel::drawReport_(const BuildPanelInputs& in)
{
    if (!in.report || in.report->result == BuildResult::NotFinished) return;
    const MyCoreEngine::BuildReport& r = *in.report;

    ImGui::SeparatorText("Last build");

    // SIX OUTCOMES, SIX SENTENCES. See note 6: these do not differ only in
    // severity, and FailedValidation says the opposite of the other failures
    // about whether anything is on disk.
    switch (r.result) {
    case BuildResult::Succeeded:
        ImGui::TextColored(kGood, "Built in %.1fs", r.seconds);
        break;
    case BuildResult::FailedPreflight:
        ImGui::TextColored(kBad, "Refused before it started. Nothing was written.");
        break;
    case BuildResult::FailedCompile:
        ImGui::TextColored(kBad, "The compile failed (exit code %d). Nothing was written.",
                           r.exitCode);
        textDim("The compiler's own output is in the log below.");
        break;
    case BuildResult::FailedAssemble:
        ImGui::TextColored(kBad, "Assembling failed. What had been written was removed.");
        break;
    case BuildResult::FailedValidation:
        ImGui::TextColored(kBad, "The bundle was produced but did not pass validation.");
        ImGui::TextWrapped("It was LEFT IN PLACE so you can look at what was made. "
                           "Do not ship it.");
        break;
    case BuildResult::Cancelled:
        ImGui::TextColored(kWarn, "Cancelled. What had been written was removed.");
        break;
    case BuildResult::NotFinished:
        break;
    }

    if (!r.message.empty()) ImGui::TextWrapped("%s", r.message.c_str());
    for (const std::string& e : r.errors)   wrapped(kBad, "- " + e);
    for (const std::string& w : r.warnings) wrapped(kWarn, "- " + w);

    if (!r.outputDirectory.empty()) {
        // Set even on failure, deliberately, so the panel can say what it was
        // GOING to write. Labelled accordingly rather than implying it is there.
        const bool onDisk = (r.result == BuildResult::Succeeded ||
                             r.result == BuildResult::FailedValidation);
        ImGui::TextWrapped("%s %s", onDisk ? "At:" : "Would have been at:",
                           r.outputDirectory.c_str());
        if (ImGui::SmallButton("Copy Path"))
            ImGui::SetClipboardText(r.outputDirectory.c_str());
    }

    if (r.filesCopied > 0) {
        ImGui::Text("%d files, %.1f MB", (int)r.filesCopied,
                    (double)r.bytesCopied / (1024.0 * 1024.0));
    }

    // NAMED, NOT COUNTED. A scene silently missing from a bundle is the failure
    // the whole feature exists to make visible, so the exclusions are listed the
    // same way the staging script announces its own removals.
    if (!r.scenesIncluded.empty() &&
        ImGui::TreeNode("included", "Scenes shipped (%d)", (int)r.scenesIncluded.size())) {
        for (const std::string& s : r.scenesIncluded) textDim(s.c_str());
        ImGui::TreePop();
    }
    if (!r.scenesExcluded.empty() &&
        ImGui::TreeNode("excluded", "Scenes left out (%d)", (int)r.scenesExcluded.size())) {
        for (const std::string& s : r.scenesExcluded) textDim(s.c_str());
        ImGui::TreePop();
    }
}

void BuildSettingsPanel::drawLog_(const BuildPanelInputs& in)
{
    if (!in.log || in.log->empty()) return;

    ImGui::SetNextItemOpen(showLog_, ImGuiCond_Always);
    showLog_ = ImGui::CollapsingHeader("Build log");
    if (!showLog_) return;

    const std::string& log = *in.log;
    // RENDER THE TAIL, not the whole thing. A full compile is megabytes and
    // ImGui measures the text it is handed; the interesting end of a build log
    // is the end. Cut at a newline so the first visible line is a whole one.
    const std::size_t kTail = 64 * 1024;
    const char* begin = log.c_str();
    if (log.size() > kTail) {
        begin = log.c_str() + (log.size() - kTail);
        if (const char* nl = std::strchr(begin, '\n')) begin = nl + 1;
        textDim("(earlier output trimmed)");
    }

    ImGui::BeginChild("##buildlog", ImVec2(0, 220), ImGuiChildFlags_Border,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(begin, log.c_str() + log.size());
    // Follow the output only while it is being produced. Sticking to the bottom
    // after the build finished would fight an author scrolling back through it.
    if (in.building) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// ===========================================================================
// Draw
// ===========================================================================
BuildSettingsActions BuildSettingsPanel::Draw(BuildSettings& settings,
                                              const BuildPanelInputs& in,
                                              bool* pOpen)
{
    BuildSettingsActions a;
    widgetId_ = 0;

    // DID THIS PANEL JUST APPEAR? Draw is only called on frames the panel is
    // visible, so a gap in the frame counter IS the window being opened -- no
    // flag for the menu item to set and forget. Reopening re-checks disk state
    // and asks for a fresh preflight, which is what somebody who just opened
    // Build Settings is asking for.
    const int frame = ImGui::GetFrameCount();
    const bool appeared = (frame - lastDrawFrame_) > 1;
    lastDrawFrame_ = frame;
    if (appeared) {
        ++recheckNonce_;
        a.preflightRequested = true;
    }

    if (in.focus) ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowSize(ImVec2(600, 640), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Build Settings", pOpen)) {

        // ---- the fingerprints of note 3 --------------------------------
        // Folded every frame, over strings, with no allocation. Cheaper by
        // orders of magnitude than the stats they gate, and unlike a dirty flag
        // they cannot be forgotten by a second edit path.
        std::uint64_t sf = 1469598103934665603ull;
        sf = mix(sf, (std::uint64_t)settings.scenes.size());
        for (const std::string& s : settings.scenes) sf = mix(sf, s);
        sf = mix(sf, (std::uint64_t)recheckNonce_);
        if (!haveRows_ || sf != scenesFingerprint_) {
            scenesFingerprint_ = sf;
            refreshScenes_(settings);
        }

        std::uint64_t of = 1469598103934665603ull;
        of = mix(of, settings.outputDirectory);
        of = mix(of, in.preflight ? in.preflight->resolvedOutputDir : std::string());
        of = mix(of, (std::uint64_t)recheckNonce_);
        if (!haveOutput_ || of != outputFingerprint_) {
            outputFingerprint_ = of;
            refreshOutput_(settings, in.preflight);
        }

        drawFileState_(in, a);

        // MALFORMED IS READ-ONLY, and it is one BeginDisabled rather than a
        // condition threaded through nine sections -- which is the version where
        // one section is missed and a widget below a red banner quietly edits
        // settings that are about to be thrown away. Save and Build are refused
        // by their own conditions too, so the two agree even if this changes.
        const bool readOnly = (in.load.status == BuildSettingsStatus::Malformed);
        ImGui::BeginDisabled(readOnly);

        ImGui::SameLine();
        if (ImGui::SmallButton("Recheck")) {
            // Bumps both fingerprints, so a file deleted outside the editor is
            // noticed without a timer re-statting the list forever.
            ++recheckNonce_;
            a.preflightRequested = true;
        }
        tip("Re-stat the listed scenes and the output directory, and re-run the check.");

        drawScenes_(settings, in, a);
        drawAddRow_(settings, in, a);
        drawExcluded_(settings, in, a);
        drawOutput_(settings, in, a);
        drawProfile_(settings, a);
        drawPreflight_(in, a);

        ImGui::EndDisabled();

        // Outside the disabled block: the Build bar has its own, more specific
        // refusals (including the malformed one) and says which applies.
        drawBuildBar_(settings, in, a);
        drawReport_(in);
        drawLog_(in);
    }
    ImGui::End();
    return a;
}
