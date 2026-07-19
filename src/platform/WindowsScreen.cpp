#include "../ScreenManager.hpp"
#include <windows.h>
#include <vector>
#include <string>
#include <cstdlib>

namespace selectscreen {
namespace {
    struct MonitorEntry {
        HMONITOR handle;
        MONITORINFOEXW info;
    };

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

    HWND gameWindow() {
        HWND window = FindWindowW(nullptr, L"Geometry Dash");
        if (window) return window;

        window = GetActiveWindow();
        if (window) return window;

        return GetForegroundWindow();
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

    bool isFullscreenLike(HWND hwnd, RECT const& currentMonitorRect) {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if ((style & WS_OVERLAPPEDWINDOW) == 0 || (style & WS_POPUP) != 0) {
            return true;
        }

        RECT rect{};
        if (!GetWindowRect(hwnd, &rect)) return false;

        constexpr int tolerance = 8;
        return std::abs(rect.left - currentMonitorRect.left) <= tolerance &&
               std::abs(rect.top - currentMonitorRect.top) <= tolerance &&
               std::abs(rect.right - currentMonitorRect.right) <= tolerance &&
               std::abs(rect.bottom - currentMonitorRect.bottom) <= tolerance;
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

    HWND hwnd = gameWindow();
    if (!hwnd) {
        error = "Geometry Dash's native window could not be found.";
        return false;
    }

    auto const& target = list[options.screenIndex];
    RECT targetRect = target.info.rcMonitor;
    int targetWidth = targetRect.right - targetRect.left;
    int targetHeight = targetRect.bottom - targetRect.top;

    HMONITOR currentMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO currentInfo{};
    currentInfo.cbSize = sizeof(currentInfo);
    GetMonitorInfoW(currentMonitor, &currentInfo);

    RECT windowRect{};
    if (!GetWindowRect(hwnd, &windowRect)) {
        error = "Unable to read Geometry Dash's window position.";
        return false;
    }

    bool fullscreenLike = isFullscreenLike(hwnd, currentInfo.rcMonitor);

    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    int x = targetRect.left + (targetWidth - width) / 2;
    int y = targetRect.top + (targetHeight - height) / 2;

    if (fullscreenLike) {
        x = targetRect.left;
        y = targetRect.top;
        width = targetWidth;
        height = targetHeight;
    }

    // Do not change window styles, refresh rate, or fullscreen mode. Geometry Dash
    // remains responsible for exclusive, borderless, or windowed mode; this mod only
    // places the existing native window on the chosen monitor.
    if (!SetWindowPos(
            hwnd,
            nullptr,
            x,
            y,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW
        )) {
        error = "Windows rejected the requested monitor position (error " +
            std::to_string(GetLastError()) + ").";
        return false;
    }

    return true;
}

} // namespace selectscreen
