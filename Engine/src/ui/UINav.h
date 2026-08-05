#pragma once
// Directional navigation — the UI's third input state, beside the pointer and
// the keyboard, and the whole of what a gamepad needs.
//
// THE UI NEVER LEARNS WHAT A GAMEPAD IS. This carries INTENTS ("move up",
// "activate"), not devices, so the host synthesises them from whatever it
// likes: a stick, a d-pad, WASD, the arrow keys, an on-screen touch pad. Device
// knowledge stays in InputMap where it already lives, which is the same
// division UIPointerState and UIKeyboardState already use and for the same
// reason — only the host knows whether this frame's input belongs to the game's
// UI or to something else.
//
// EDGE-TRIGGERED, like UIKeyboardState::keys. A held stick is not a state the
// UI samples; it is a series of moves the HOST emits, because auto-repeat needs
// dt and there is no clock anywhere in the UI — UpdatePointer does not even
// take one. UINavRepeater below is that clock, shared so both hosts cannot
// drift on the feel of it.
#include "../core/Core.h"

#include <cstdint>
#include <vector>

namespace MyCoreEngine::ui {

    enum class UINavDir : std::uint8_t { None, Up, Down, Left, Right };

    struct UINavState {
        // One entry per discrete move this frame. A vector rather than a single
        // direction because a fast flick can legitimately produce two, and
        // dropping one makes the UI feel like it missed the input.
        std::vector<UINavDir> moves;
        // A / Enter. Routed to UIDocument::ActivateFocused, which is the same
        // path a click takes — a pad needs no second mechanism.
        bool activate = false;
        // B / Escape. Means "back out of what I am in": it blurs the focused
        // element and reports that it did, so a host can treat an unhandled
        // back as "close the menu" or "quit" without guessing.
        bool back = false;
        // Shoulder buttons: -1 previous page, +1 next. Zero is no change.
        int  page = 0;

        bool empty() const {
            return moves.empty() && !activate && !back && page == 0;
        }
        void clear() { moves.clear(); activate = false; back = false; page = 0; }
    };

    // Turns a HELD direction into the series of edges a UI expects: one
    // immediately, then nothing for `delay`, then one every `interval`.
    //
    // Lives here rather than in each host because "how a held stick feels" is
    // exactly the kind of constant that drifts when it is written twice, and
    // because neither host has anywhere better to put it. It is pure — no
    // device, no GLFW — so it is also the part of the gamepad path that can be
    // tested without hardware.
    class UINavRepeater {
    public:
        // Defaults chosen to match what a console UI feels like: a beat before
        // it starts running, then brisk. Tunable because "brisk" is a taste.
        float delay = 0.40f;
        float interval = 0.12f;

        // Feed the direction currently held (None when nothing is), and the
        // frame's dt. Returns how many discrete moves to emit this frame —
        // normally 0 or 1, but more if a frame ran long, so a stall does not
        // silently eat input.
        int Tick(UINavDir held, float dt) {
            if (held == UINavDir::None) {
                last_ = UINavDir::None;
                timer_ = 0.0f;
                return 0;
            }
            if (held != last_) {
                // A NEW direction fires at once. Waiting `delay` for the first
                // move is the difference between a menu that answers and one
                // that feels broken.
                last_ = held;
                timer_ = -delay;   // negative == still in the initial pause
                return 1;
            }
            timer_ += dt;
            int n = 0;
            while (timer_ >= interval) {
                timer_ -= interval;
                ++n;
                if (n > 8) break;   // a very long stall is not a burst of input
            }
            return n;
        }

        void Reset() { last_ = UINavDir::None; timer_ = 0.0f; }

    private:
        UINavDir last_ = UINavDir::None;
        float    timer_ = 0.0f;
    };

    // Axes -> a single held direction, with the dominant axis winning so a
    // diagonal push does not fire both. `threshold` is applied on top of
    // whatever deadzone the input layer already removed.
    inline UINavDir NavDirFromAxes(float x, float y, float threshold = 0.5f) {
        const float ax = x < 0.0f ? -x : x;
        const float ay = y < 0.0f ? -y : y;
        if (ax < threshold && ay < threshold) return UINavDir::None;
        if (ax >= ay) return x > 0.0f ? UINavDir::Right : UINavDir::Left;
        return y > 0.0f ? UINavDir::Up : UINavDir::Down;
    }

} // namespace MyCoreEngine::ui
