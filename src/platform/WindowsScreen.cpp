#include "../ScreenManager.hpp"
#include <windows.h>
#include <vector>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <cstdint>

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
    bool g_appActive = false;
    ULONGLONG g_recoveryUntil = 0;

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

    bool targetRect(RECT& rect) {
        g_monitors = monitors();
        if (g_monitors.empty()) return false;
        g_targetIndex = std::clamp(g_targetIndex, 0, static_cast<int>(g_monitors.size()) - 1);
        rect = g_monitors[g_targetIndex].info.rcMonitor;
        return true;
    }

    bool approximately(int a, int b, int tolerance = 12) {
        return std::abs(a - b) <= tolerance;
    }

    bool proposedFullscreenOnAnyMonitor(WINDOWPOS const& pos) {
        auto list = monitors();
        for (auto const& monitor : list) {
            auto const& r = monitor.info.rcMonitor;
            int width = r.right - r.left;
            int height = r.bottom - r.top;
            if (approximately(pos.cx, width) && approximately(pos.cy, height) &&
                approximately(pos.x, r.left) && approximately(pos.y, r.top)) {
                return true;
            }
        }
        return false;
    }

    bool windowIsFullscreenLike(HWND hwnd) {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if ((style & WS_POPUP) != 0 || (style & WS_OVERLAPPEDWINDOW) == 0) return true;

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) return false;
        auto list = monitors();
        for (auto const& monitor : list) {
            auto const& r = monitor.info.rcMonitor;
            if (approximately(rect.left, r.left) && approximately(rect.top, r.top) &&
                approximately(rect.right, r.right) && approximately(rect.bottom, r.bottom)) {
                return true;
            }
        }
        return false;
    }

    void moveWindowToTarget(HWND hwnd) {
        if (!hwnd || g_correcting) return;

        RECT target{};
        if (!targetRect(target)) return;

        RECT current{};
        if (!GetWindowRect(hwnd, &current)) return;

        bool fullscreen = windowIsFullscreenLike(hwnd);
        int width = current.right - current.left;
        int height = current.bottom - current.top;
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
            nullptr,
            x,
            y,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED
        );
        g_correcting = false;
    }

    LRESULT CALLBACK hookedWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_ACTIVATEAPP) {
            g_appActive = wParam != FALSE;
            if (g_appActive) {
                // Only guard the short interval in which GD recreates/restores its
                // exclusive-fullscreen swap-chain window after returning to the game.
                g_recoveryUntil = GetTickCount64() + 2500;
            } else {
                g_recoveryUntil = 0;
            }
        }
        else if (message == WM_ACTIVATE) {
            bool active = LOWORD(wParam) != WA_INACTIVE;
            if (active) {
                g_appActive = true;
                g_recoveryUntil = GetTickCount64() + 2500;
            }
        }
        else if (message == WM_WINDOWPOSCHANGING && !g_correcting) {
            auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
            bool inRecovery = g_appActive && GetTickCount64() <= g_recoveryUntil;

            // Do not interfere while switching away, minimizing, hiding, or with
            // ordinary windowed-mode movement. Only rewrite a fullscreen-sized
            // restore request during the short reactivation window.
            if (pos && inRecovery && !IsIconic(hwnd) &&
                (pos->flags & (SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE)) == 0 &&
                proposedFullscreenOnAnyMonitor(*pos)) {
                RECT target{};
                if (targetRect(target)) {
                    int targetWidth = target.right - target.left;
                    int targetHeight = target.bottom - target.top;
                    bool alreadyTarget = approximately(pos->x, target.left) &&
                                         approximately(pos->y, target.top) &&
                                         approximately(pos->cx, targetWidth) &&
                                         approximately(pos->cy, targetHeight);
                    if (!alreadyTarget) {
                        pos->x = target.left;
                        pos->y = target.top;
                        pos->cx = targetWidth;
                        pos->cy = targetHeight;
                    }
                }
            }
        }
        else if (message == WM_DISPLAYCHANGE && g_appActive) {
            g_recoveryUntil = GetTickCount64() + 2500;
        }
        else if (message == WM_NCDESTROY) {
            auto original = g_originalWndProc;
            g_originalWndProc = nullptr;
            g_gameWindow = nullptr;
            g_appActive = false;
            g_recoveryUntil = 0;
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
            g_appActive = GetForegroundWindow() == hwnd;
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
    moveWindowToTarget(hwnd);
    return true;
}

} // namespace selectscreen
