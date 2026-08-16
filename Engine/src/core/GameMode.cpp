#include "GameMode.h"

#include <cstddef>
#include <utility>

namespace {
    // int index -> vector subscript, in one place. Every caller below has
    // already range-checked, so this is about the signed/unsigned conversion
    // rather than about safety.
    inline std::size_t at(int i) { return static_cast<std::size_t>(i); }
}

namespace MyCoreEngine {

    GameModeRegistry::~GameModeRegistry() {
        // A mode that is live when the host shuts down still gets its Exit.
        // Modes hold host-lifetime resources (a session, a built match, a
        // recording being written), and "the process is ending anyway" is the
        // reasoning that loses the last few seconds of a replay file.
        Leave();
    }

    void GameModeRegistry::Add(std::unique_ptr<IGameMode> mode) {
        if (!mode) return;   // a failed registration must not become a null
                             // dereference several frames later, in the menu
        modes_.push_back(std::move(mode));
    }

    const char* GameModeRegistry::DisplayNameAt(int index) const {
        if (index < 0 || index >= Count()) return "";
        const char* n = modes_[at(index)]->DisplayName();
        return n ? n : "";
    }

    bool GameModeRegistry::Enter(int index, const GameModeContext& ctx,
                                 std::string& error) {
        if (index < 0 || index >= Count()) {
            error = "no such game mode";
            return false;
        }

        // Whatever was live goes first, unconditionally -- including the case
        // where it is the SAME index, which is a restart and has to be a clean
        // one. A mode re-entered without an Exit would be handed a second Enter
        // on top of its own live state.
        Leave();

        IGameMode& mode = *modes_[at(index)];
        // Cleared BEFORE Enter, not after: a mode is allowed to decide during
        // Enter that it is done (a replay with nothing in it), and clearing
        // afterwards would throw that decision away and leave it stuck on
        // screen.
        mode.clearExitRequest();

        std::string why;
        if (!mode.Enter(ctx, why)) {
            // Exit even though Enter failed. The host does not track how far
            // Enter got, so the mode is the only thing that can know what it
            // half-built -- which is why IGameMode::Exit is documented as safe
            // after a failed Enter.
            mode.Exit();
            error = why.empty() ? "the mode refused to start" : why;
            return false;
        }

        activeIndex_ = index;
        return true;
    }

    void GameModeRegistry::Leave() {
        if (activeIndex_ < 0) return;
        const int idx = activeIndex_;
        // Cleared FIRST. Exit() may reach back into the host (releasing a
        // capture, asking for a scene) and anything that asks "is a mode
        // active" during teardown must be told no -- a half-exited mode that
        // still reports itself as owning the screen is how the menu comes back
        // with the keyboard still pointed somewhere else.
        activeIndex_ = -1;
        modes_[at(idx)]->Exit();
    }

    bool GameModeRegistry::activeOwnsScreen() const {
        const IGameMode* m = active();
        return m && m->OwnsScreen();
    }

    bool GameModeRegistry::DrainExitRequest() {
        IGameMode* m = active();
        if (!m || !m->wantsExit()) return false;
        Leave();
        return true;
    }

} // namespace MyCoreEngine
