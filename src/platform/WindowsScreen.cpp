#include "../ScreenManager.hpp"
#include <windows.h>
#include <vector>
#include <string>

namespace selectscreen {
namespace {
    struct MonitorEntry { HMONITOR handle; MONITORINFOEXW info; };

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
        HWND window = GetActiveWindow();
        if (window) return window;
        window = FindWindowW(nullptr, L"Geometry Dash");
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
            static_cast<int>(i), narrow(monitor.info.szDevice), rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top,
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
    RECT rect = target.info.rcMonitor;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (options.mode == DisplayMode::Windowed) {
        ChangeDisplaySettingsExW(target.info.szDevice, nullptr, nullptr, 0, nullptr);
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
        int w = width * 3 / 4;
        int h = height * 3 / 4;
        SetWindowPos(hwnd, HWND_NOTOPMOST, rect.left + (width - w) / 2, rect.top + (height - h) / 2,
            w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return true;
    }

    if (options.mode == DisplayMode::Exclusive) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        mode.dmPelsWidth = static_cast<DWORD>(width);
        mode.dmPelsHeight = static_cast<DWORD>(height);
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        if (options.refreshRate > 0) {
            mode.dmDisplayFrequency = static_cast<DWORD>(options.refreshRate);
            mode.dmFields |= DM_DISPLAYFREQUENCY;
        }
        auto status = ChangeDisplaySettingsExW(target.info.szDevice, &mode, nullptr, CDS_FULLSCREEN, nullptr);
        if (status != DISP_CHANGE_SUCCESSFUL) {
            error = "Windows rejected the requested exclusive fullscreen display mode (code " + std::to_string(status) + ").";
            return false;
        }
    } else {
        ChangeDisplaySettingsExW(target.info.szDevice, nullptr, nullptr, 0, nullptr);
    }

    SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
    SetWindowPos(hwnd, HWND_TOP, rect.left, rect.top, width, height,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    return true;
}

bool ScreenManager::isGameFocused() {
    return gameWindow() == GetForegroundWindow();
}

} // namespace selectscreen
