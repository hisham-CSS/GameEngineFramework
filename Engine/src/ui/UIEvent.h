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

#include <functional>

namespace MyCoreEngine::ui {

    class UIElement;

    enum class UIEventType {
        PointerMove,   // bubbles
        PointerEnter,  // does NOT bubble (fires per element entered, like CSS :hover)
        PointerLeave,  // does NOT bubble
        PointerDown,   // bubbles
        PointerUp,     // bubbles
        Click,         // bubbles; fires on press+release over the SAME element
    };

    struct UIEvent {
        UIEventType type = UIEventType::PointerMove;
        glm::vec2   position{ 0.0f };  // UI-space pixels (top-left origin)
        int         button = 0;        // 0 = primary

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
