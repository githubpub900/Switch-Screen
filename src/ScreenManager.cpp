#include "ScreenManager.hpp"

namespace selectscreen {

DisplayMode parseDisplayMode(std::string const& value) {
    if (value == "borderless") return DisplayMode::Borderless;
    if (value == "windowed") return DisplayMode::Windowed;
    return DisplayMode::Exclusive;
}

char const* displayModeName(DisplayMode mode) {
    switch (mode) {
        case DisplayMode::Windowed: return "windowed";
        case DisplayMode::Borderless: return "borderless";
        case DisplayMode::Exclusive: return "exclusive";
    }
    return "exclusive";
}

} // namespace selectscreen
