#include "../ScreenManager.hpp"
#include <windows.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <algorithm>

namespace selectscreen {
namespace {
    struct MonitorEntry {
        HMONITOR handle;
        MONITORINFOEXW info;
    };

    std::vector<MonitorEntry> g_monitors;
    int g_targetIndex = 0;
    HWND g_gameWindow = nullptr;
    WNDPROC g_originalWndProc = nullptr;
    bool g_correcting = false;
    UINT_PTR g_restoreTimer = 0;

    BOOL CALLBACK enumProc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        auto& out = *reinterpret_cast<std::vector<MonitorEntry>*>(data);
        MonitorEntry entry{};
        entry.handle = monitor;
        entry.info.cbSize = sizeof(entry.info);
        if (GetMonitorInfoW(monitor, &entry.info)) out.push_back(entry);
        return TRUE;
    }

    std::vector<MonitorEntry> monitors() {
        std::vector<MonitorEntry> result;
        EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&result));
        return result;
    }

    BOOL CALLBACK findProcessWindowProc(HWND hwnd, LPARAM data) {
        auto* out = reinterpret_cast<HWND*>(data);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(hwnd)) return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

        wchar_t className[128]{};
        GetClassNameW(hwnd, className, static_cast<int>(std::size(className)));

        // Prefer the cocos2d/GLFW game window, but any visible unowned top-level
        // window belonging to GeometryDash.exe is a better fallback than the
        // foreground window (which may belong to another app after Alt+Tab).
        if (wcsstr(className, L"GLFW") || wcsstr(className, L"Cocos") || !*out) {
            *out = hwnd;
        }
        return TRUE;
    }

    HWND findGameWindow() {
        if (g_gameWindow && IsWindow(g_gameWindow)) return g_gameWindow;

        HWND result = nullptr;
        EnumWindows(findProcessWindowProc, reinterpret_cast<LPARAM>(&result));
        g_gameWindow = result;
        return result;
    }

    std::string narrow(wchar_t const* value) {
        if (!value || !*value) return "Display";
        int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) return "Display";
        std::string out(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), size, nullptr, nullptr);
        out.pop_back();
        return out;
    }

    bool isFullscreenLike(HWND hwnd, RECT const& monitorRect) {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if ((style & WS_POPUP) != 0 || (style & WS_OVERLAPPEDWINDOW) == 0) return true;

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) return false;
        constexpr int tolerance = 8;
        return std::abs(rect.left - monitorRect.left) <= tolerance &&
               std::abs(rect.top - monitorRect.top) <= tolerance &&
               std::abs(rect.right - monitorRect.right) <= tolerance &&
               std::abs(rect.bottom - monitorRect.bottom) <= tolerance;
    }

    bool targetRect(RECT& rect) {
        g_monitors = monitors();
        if (g_monitors.empty()) return false;
        g_targetIndex = std::clamp(g_targetIndex, 0, static_cast<int>(g_monitors.size()) - 1);
        rect = g_monitors[g_targetIndex].info.rcMonitor;
        return true;
    }

    void forceWindowToTarget(HWND hwnd) {
        if (!hwnd || g_correcting) return;

        RECT target{};
        if (!targetRect(target)) return;

        HMONITOR current = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO currentInfo{};
        currentInfo.cbSize = sizeof(currentInfo);
        GetMonitorInfoW(current, &currentInfo);

        RECT currentRect{};
        if (!GetWindowRect(hwnd, &currentRect)) return;

        bool fullscreen = isFullscreenLike(hwnd, currentInfo.rcMonitor);
        int width = currentRect.right - currentRect.left;
        int height = currentRect.bottom - currentRect.top;
        int x = target.left + ((target.right - target.left) - width) / 2;
        int y = target.top + ((target.bottom - target.top) - height) / 2;

        if (fullscreen) {
            x = target.left;
            y = target.top;
            width = target.right - target.left;
            height = target.bottom - target.top;
        }

        g_correcting = true;
        SetWindowPos(
            hwnd,
            HWND_TOP,
            x,
            y,
            width,
            height,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW
        );
        g_correcting = false;
    }

    LRESULT CALLBACK hookedWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_WINDOWPOSCHANGING && !g_correcting) {
            auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
            if (pos && (pos->flags & SWP_NOMOVE) == 0) {
                RECT target{};
                if (targetRect(target)) {
                    HMONITOR current = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO currentInfo{};
                    currentInfo.cbSize = sizeof(currentInfo);
                    GetMonitorInfoW(current, &currentInfo);

                    // This is the key fix: when GD restores exclusive fullscreen it
                    // asks Windows to move its popup window to the primary monitor.
                    // Rewrite that pending WINDOWPOS before Windows applies it.
                    if (isFullscreenLike(hwnd, currentInfo.rcMonitor)) {
                        pos->x = target.left;
                        pos->y = target.top;
                        pos->cx = target.right - target.left;
                        pos->cy = target.bottom - target.top;
                        pos->flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
                    }
                }
            }
        }
        else if ((message == WM_ACTIVATEAPP && wParam) || message == WM_SETFOCUS ||
                 message == WM_DISPLAYCHANGE || message == WM_SIZE) {
            if (g_restoreTimer) KillTimer(hwnd, g_restoreTimer);
            g_restoreTimer = SetTimer(hwnd, 0x5353, 50, nullptr);
        }
        else if (message == WM_TIMER && wParam == 0x5353) {
            forceWindowToTarget(hwnd);

            static int repeats = 0;
            if (++repeats >= 40) { // two seconds at 50 ms
                KillTimer(hwnd, g_restoreTimer);
                g_restoreTimer = 0;
                repeats = 0;
            }
            return 0;
        }
        else if (message == WM_NCDESTROY) {
            if (g_restoreTimer) KillTimer(hwnd, g_restoreTimer);
            g_restoreTimer = 0;
            auto original = g_originalWndProc;
            g_originalWndProc = nullptr;
            g_gameWindow = nullptr;
            return CallWindowProcW(original, hwnd, message, wParam, lParam);
        }

        return CallWindowProcW(g_originalWndProc, hwnd, message, wParam, lParam);
    }

    void installWindowHook(HWND hwnd) {
        if (!hwnd) return;
        if (g_originalWndProc && g_gameWindow == hwnd) return;

        SetLastError(0);
        auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            hwnd,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(hookedWndProc)
        ));
        if (previous || GetLastError() == 0) {
            g_originalWndProc = previous;
            g_gameWindow = hwnd;
        }
    }
}

std::vector<ScreenInfo> ScreenManager::enumerate() {
    std::vector<ScreenInfo> result;
    auto list = monitors();

    for (size_t i = 0; i < list.size(); ++i) {
        auto const& monitor = list[i];
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        EnumDisplaySettingsW(monitor.info.szDevice, ENUM_CURRENT_SETTINGS, &mode);

        auto const& rect = monitor.info.rcMonitor;
        result.push_back(ScreenInfo{
            static_cast<int>(i),
            narrow(monitor.info.szDevice),
            rect.left,
            rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            static_cast<int>(mode.dmDisplayFrequency),
            (monitor.info.dwFlags & MONITORINFOF_PRIMARY) != 0
        });
    }

    return result;
}

bool ScreenManager::apply(ApplyOptions const& options, std::string& error) {
    auto list = monitors();
    if (options.screenIndex < 0 || options.screenIndex >= static_cast<int>(list.size())) {
        error = "The selected screen index is out of range.";
        return false;
    }

    HWND hwnd = findGameWindow();
    if (!hwnd) {
        error = "Geometry Dash's native window could not be found.";
        return false;
    }

    g_targetIndex = options.screenIndex;
    installWindowHook(hwnd);
    forceWindowToTarget(hwnd);
    return true;
}

} // namespace selectscreen
