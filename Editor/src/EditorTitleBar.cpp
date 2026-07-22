#include "EditorTitleBar.h"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM
#include <shellapi.h>  // SHAppBarMessage: auto-hide taskbar detection
#pragma comment(lib, "shell32.lib")

namespace {
    WNDPROC g_glfwProc = nullptr;    // GLFW's original wndproc (chained to)
    HWND    g_hwnd = nullptr;

    // Set every frame by the ImGui title bar. The drag rect is the empty
    // title-bar strip (everything but the window buttons), in screen pixels; a
    // zero-area rect means "nothing draggable this frame" -- the caller reports
    // that while a menu/modal popup is open so a drag can't yank the window out
    // from under it.
    float g_barHeight = 32.0f;
    RECT  g_dragRect = { 0, 0, 0, 0 };
    RECT  g_buttonsRect = { 0, 0, 0, 0 };

    bool PtIn(const RECT& r, const POINT& p) {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }

    bool WindowIsMaximized(HWND hwnd) {
        WINDOWPLACEMENT wp{ sizeof(wp) };
        return GetWindowPlacement(hwnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED;
    }

    // Frame metrics for the DPI of the monitor the window is ON, not the
    // primary monitor. GLFW makes the process per-monitor-DPI-aware, so plain
    // GetSystemMetrics would return primary-DPI values and mis-inset the client
    // when maximised (or when resizing) on a differently-scaled monitor -- a
    // real case here (laptop panel + external monitor at different scales).
    int FrameMetric(HWND hwnd, int index) {
        return GetSystemMetricsForDpi(index, GetDpiForWindow(hwnd));
    }

    // If an auto-hide taskbar is docked to an edge of `mon`, return its ABE_*
    // edge, else -1. A maximised borderless client that exactly fills the work
    // area (which, for an auto-hide bar, is the whole monitor) would cover the
    // bar's reveal strip; the OS normally shaves 1px for a framed window, but
    // our WM_NCCALCSIZE override bypasses that, so we redo it.
    int AutoHideEdge(HMONITOR mon) {
        MONITORINFO mi{ sizeof(mi) };
        if (!GetMonitorInfo(mon, &mi)) return -1;
        const int edges[4] = { ABE_LEFT, ABE_TOP, ABE_RIGHT, ABE_BOTTOM };
        for (int e : edges) {
            APPBARDATA q{ sizeof(q) };
            q.uEdge = static_cast<UINT>(e);
            q.rc = mi.rcMonitor;
            if (SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &q)) return e;
        }
        return -1;
    }

    LRESULT CALLBACK TitleBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_NCCALCSIZE: {
            // Client area == whole window: no OS caption, no visible frame.
            // WS_THICKFRAME stays on the window, so WM_NCHITTEST below still
            // drives native resize. When maximised, Windows oversizes the
            // window by the frame width on every edge (so a real title bar's
            // border sits offscreen); reclaim that inset or our content — and
            // the taskbar — would be clipped/covered.
            if (wParam) {
                if (WindowIsMaximized(hwnd)) {
                    NCCALCSIZE_PARAMS* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    const int fx = FrameMetric(hwnd, SM_CXFRAME) + FrameMetric(hwnd, SM_CXPADDEDBORDER);
                    const int fy = FrameMetric(hwnd, SM_CYFRAME) + FrameMetric(hwnd, SM_CXPADDEDBORDER);
                    p->rgrc[0].left   += fx;
                    p->rgrc[0].right  -= fx;
                    p->rgrc[0].top    += fy;
                    p->rgrc[0].bottom -= fy;

                    // Keep an auto-hide taskbar reachable: leave 1px on its edge.
                    const int e = AutoHideEdge(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
                    if      (e == ABE_TOP)    p->rgrc[0].top    += 1;
                    else if (e == ABE_BOTTOM) p->rgrc[0].bottom -= 1;
                    else if (e == ABE_LEFT)   p->rgrc[0].left   += 1;
                    else if (e == ABE_RIGHT)  p->rgrc[0].right  -= 1;
                }
                return 0;
            }
            break;
        }
        case WM_NCHITTEST: {
            // Resize borders first (skipped when maximised — no edges to grab),
            // then the caption drag strip the ImGui bar reported, else client.
            const POINT screen{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT wr; GetWindowRect(hwnd, &wr);
            const bool maxed = WindowIsMaximized(hwnd);

            // Window buttons win over everything (resize borders included), so
            // clicking min/max/close never resizes the window instead.
            if (PtIn(g_buttonsRect, screen)) return HTCLIENT;

            if (!maxed) {
                const int b = FrameMetric(hwnd, SM_CXSIZEFRAME) +
                              FrameMetric(hwnd, SM_CXPADDEDBORDER);
                const bool left   = screen.x <  wr.left + b;
                const bool right  = screen.x >= wr.right - b;
                const bool top    = screen.y <  wr.top + b;
                const bool bottom = screen.y >= wr.bottom - b;
                if (top && left)     return HTTOPLEFT;
                if (top && right)    return HTTOPRIGHT;
                if (bottom && left)  return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left)   return HTLEFT;
                if (right)  return HTRIGHT;
                if (top)    return HTTOP;
                if (bottom) return HTBOTTOM;
            }

            // Caption: only where the ImGui bar has an empty drag strip. This
            // keeps the menus and min/max/close buttons on HTCLIENT so ImGui
            // still gets their clicks, while empty title-bar space drags (and
            // Aero-snaps, double-click-maximises) the window natively.
            if (PtIn(g_dragRect, screen)) return HTCAPTION;
            return HTCLIENT;
        }
        }
        return CallWindowProc(g_glfwProc, hwnd, msg, wParam, lParam);
    }
} // namespace

bool EditorTitleBar::Install(GLFWwindow* window) {
    if (!window) return false;
    g_hwnd = glfwGetWin32Window(window);
    if (!g_hwnd) return false;

    g_glfwProc = reinterpret_cast<WNDPROC>(
        GetWindowLongPtr(g_hwnd, GWLP_WNDPROC));
    SetWindowLongPtr(g_hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&TitleBarProc));

    // Force a non-client recalc so WM_NCCALCSIZE runs and the caption drops now.
    SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

void EditorTitleBar::SetDragRegion(float x0, float y0, float x1, float y1) {
    g_dragRect.left   = static_cast<LONG>(x0);
    g_dragRect.top    = static_cast<LONG>(y0);
    g_dragRect.right  = static_cast<LONG>(x1);
    g_dragRect.bottom = static_cast<LONG>(y1);
}

void EditorTitleBar::SetButtonsRegion(float x0, float y0, float x1, float y1) {
    g_buttonsRect.left   = static_cast<LONG>(x0);
    g_buttonsRect.top    = static_cast<LONG>(y0);
    g_buttonsRect.right  = static_cast<LONG>(x1);
    g_buttonsRect.bottom = static_cast<LONG>(y1);
}

void EditorTitleBar::SetBarHeight(float px) { g_barHeight = px; }

#else // non-Windows: no borderless support, keep the OS title bar.

bool EditorTitleBar::Install(GLFWwindow*) { return false; }
void EditorTitleBar::SetDragRegion(float, float, float, float) {}
void EditorTitleBar::SetButtonsRegion(float, float, float, float) {}
void EditorTitleBar::SetBarHeight(float) {}

#endif
