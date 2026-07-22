#pragma once
// Borderless-window support for the editor's custom title bar (Windows).
//
// The engine window is created decorated by GLFW; Install() strips the OS
// caption via a WM_NCCALCSIZE hook while KEEPING the native resize frame, so
// the editor draws its own title bar yet still gets OS resize, Aero snap,
// snap-layouts, and double-click-to-maximise for free. The alternative --
// GLFW_DECORATED off + hand-rolled drag/resize -- loses all of that.
//
// The ImGui title bar calls SetDragRegion each frame with the screen-space
// rectangle of its empty draggable strip; the Win32 hit-test reports that
// rectangle as the caption, so dragging there moves/snaps the window natively
// while the menus and window buttons stay clickable.
//
// No-op on non-Windows (the engine targets Windows, but this keeps the editor
// buildable elsewhere).

struct GLFWwindow;

namespace EditorTitleBar {
    // Subclass the window and switch it to a client-only (captionless) frame.
    // Safe to call once, after the GL context/window exist. Returns false if
    // the platform is unsupported or the native handle is unavailable.
    bool Install(GLFWwindow* window);

    // Per-frame, from the ImGui title bar: the screen-space rect of the drag
    // strip (the empty title-bar space) and the bar height. A zero-area rect
    // disables dragging for that frame.
    void SetDragRegion(float x0, float y0, float x1, float y1);
    void SetBarHeight(float px);

    // Per-frame: the screen-space rect covering the min/max/close buttons.
    // The hit-test returns HTCLIENT here BEFORE testing resize borders, so the
    // buttons stay clickable even where they touch the window's top/right edge
    // (the alternative is the top few pixels of each button triggering a
    // resize instead of a click). Costs the exact top-right corner resize,
    // which is the standard trade every custom title bar makes.
    void SetButtonsRegion(float x0, float y0, float x1, float y1);
}
