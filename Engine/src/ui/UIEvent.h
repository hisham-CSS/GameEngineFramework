#pragma once
// UI input events — a deliberately DOM-shaped model, for the same reason the
// layout is flexbox: the semantics are already understood, so anyone who has
// written a web UI can predict what happens.
//
// Events BUBBLE from the deepest hit element up through its ancestors, and a
// handler can stop that. This is what makes composite widgets work: a click on
// the label inside a button is still a click on the button.
#include "../core/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MyCoreEngine::ui {

    class UIElement;

    enum class UIEventType {
        PointerMove,   // bubbles
        PointerEnter,  // does NOT bubble (fires per element entered, like CSS :hover)
        PointerLeave,  // does NOT bubble
        PointerDown,   // bubbles
        PointerUp,     // bubbles
        Click,         // bubbles; fires on press+release over the SAME element
        FocusIn,       // does NOT bubble — matches the DOM's `focus`
        FocusOut,      // does NOT bubble — matches the DOM's `blur`
        KeyDown,       // bubbles, to the FOCUSED element
        TextInput,     // bubbles, to the FOCUSED element; carries typed UTF-8
        ValueChanged,  // bubbles; a control's value was edited by the user
    };

    // A platform-neutral key identity. Deliberately small: only keys the UI
    // itself has to act on appear here, because every host has to map its own
    // key codes onto this and a long list is a long mapping to get wrong.
    // Anything printable arrives as TextInput instead, already decoded — which
    // is the only way to handle layouts, dead keys and IMEs correctly.
    enum class UIKey : std::uint16_t {
        None = 0,
        Tab, Enter, Escape, Backspace, Delete,
        Left, Right, Up, Down, Home, End, PageUp, PageDown,
        // Letters used by editing shortcuts, not for text: typing 'a' delivers
        // TextInput("a"), while Ctrl+A delivers KeyDown{A, ctrl}.
        A, C, V, X, Z, Y,
    };

    struct UIKeyEvent {
        UIKey key = UIKey::None;
        bool  shift = false, ctrl = false, alt = false;
    };

    // Keyboard state for one frame, supplied by the HOST — the same division of
    // labour as UIPointerState, and for the same reason: only the host knows
    // whether its keyboard belongs to the game UI this frame or to something
    // else (the editor's own panels, a console, a chat box).
    //
    // Both fields are EDGE-triggered and per-frame: `keys` holds presses that
    // happened since the last update (including auto-repeat), `text` holds what
    // was typed. Neither is a held-key snapshot, because a UI reacts to
    // keystrokes, not to key state.
    struct UIKeyboardState {
        std::vector<UIKeyEvent> keys;
        std::string             text;   // UTF-8, already decoded by the host
        bool empty() const { return keys.empty() && text.empty(); }
        void clear() { keys.clear(); text.clear(); }
    };

    struct UIEvent {
        UIEventType type = UIEventType::PointerMove;
        glm::vec2   position{ 0.0f };  // UI-space pixels (top-left origin)
        int         button = 0;        // 0 = primary

        // KeyDown only.
        UIKey key = UIKey::None;
        bool  shift = false, ctrl = false, alt = false;
        // TextInput only: the UTF-8 typed this frame.
        std::string text;

        // `target` is the deepest element hit; `currentTarget` is the element
        // whose handler is running right now. During bubbling the two differ —
        // exactly like the DOM, and exactly what a handler on a container needs
        // in order to tell which child was actually clicked.
        UIElement* target = nullptr;
        UIElement* currentTarget = nullptr;

        bool propagationStopped = false;
        void StopPropagation() { propagationStopped = true; }
    };

    using UIEventHandler = std::function<void(UIEvent&)>;

    // Pointer state for one frame, supplied by the HOST in UI-LOCAL pixels.
    //
    // The host has to do this conversion because only it knows where the UI
    // surface sits: in the shipped player the UI covers the window, but in the
    // editor it is an image inside a dockable panel that can be moved, resized
    // or dragged to another monitor. Feeding raw window coordinates would make
    // buttons work only when the panel happened to be at the origin.
    struct UIPointerState {
        glm::vec2 position{ 0.0f };
        bool inside = false;      // pointer is over the UI surface at all
        bool buttonDown = false;  // primary button held
    };

} // namespace MyCoreEngine::ui
