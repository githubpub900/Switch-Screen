# Select Screen

A Geometry Dash Mod that lets you select which monitor you want to use. (Fixes the bug where you move your Geometry Dash to another monitor, and it goes back on the previous monitor when you tab out and tab back in)

## Features

- Monitor selection from Geode's mod settings
- Exclusive fullscreen
- Borderless fullscreen
- Windowed mode
- Reapply after Alt-Tab/focus restoration
- Windows, Linux, and macOS support

## Build

Install the Geode CLI and SDK, then run from this folder:

```sh
mkdir build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Platform notes

- **Windows:** Uses `EnumDisplayMonitors`, `ChangeDisplaySettingsExW`, and native window positioning. Exclusive mode changes the selected monitor's display mode before making GD fullscreen.
- **Linux:** Targets native X11 using XRandR and EWMH fullscreen. Wayland sessions should launch GD through XWayland. Some compositors may treat “exclusive” and borderless identically. (You may need Linux Fullscreen Fix for Exclusive Fullscreen support)
- **macOS:** Uses `NSScreen` and `NSWindow`. macOS fullscreen is implemented through the system fullscreen Space; borderless mode stays on the current desktop.
