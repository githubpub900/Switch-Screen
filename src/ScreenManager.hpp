#pragma once

#include <string>
#include <vector>

namespace selectscreen {

enum class DisplayMode {
    Windowed,
    Exclusive,
    Borderless,
};

struct ScreenInfo {
    int index = 0;
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int refreshRate = 0;
    bool primary = false;
};

struct ApplyOptions {
    int screenIndex = 0; // zero-based
    DisplayMode mode = DisplayMode::Exclusive;
    int refreshRate = 0;
};

class ScreenManager final {
public:
    static std::vector<ScreenInfo> enumerate();
    static bool apply(ApplyOptions const& options, std::string& error);
    static bool isGameFocused();
};

DisplayMode parseDisplayMode(std::string const& value);
char const* displayModeName(DisplayMode mode);

} // namespace selectscreen
