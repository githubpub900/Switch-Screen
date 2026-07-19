#include "../ScreenManager.hpp"
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

namespace selectscreen {
namespace {
    NSArray<NSScreen*>* orderedScreens() {
        return [NSScreen screens];
    }

    NSWindow* gameWindow() {
        NSWindow* win = [NSApp keyWindow];
        if (!win) win = [NSApp mainWindow];
        if (!win && NSApp.windows.count) win = NSApp.windows.firstObject;
        return win;
    }
}

std::vector<ScreenInfo> ScreenManager::enumerate() {
    std::vector<ScreenInfo> out;
    NSArray<NSScreen*>* screens = orderedScreens();
    NSScreen* primary = [NSScreen mainScreen];

    for (NSUInteger i = 0; i < screens.count; ++i) {
        NSScreen* screen = screens[i];
        NSRect frame = screen.frame;
        NSDictionary* desc = screen.deviceDescription;
        NSNumber* number = desc[@"NSScreenNumber"];
        CGDirectDisplayID displayID = number.unsignedIntValue;
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
        int refresh = mode ? static_cast<int>(CGDisplayModeGetRefreshRate(mode) + 0.5) : 0;
        if (mode) CGDisplayModeRelease(mode);

        NSString* name = @"Display";
        if (@available(macOS 10.15, *)) name = screen.localizedName;

        out.push_back(ScreenInfo{
            static_cast<int>(i),
            name.UTF8String ?: "Display",
            static_cast<int>(frame.origin.x),
            static_cast<int>(frame.origin.y),
            static_cast<int>(frame.size.width),
            static_cast<int>(frame.size.height),
            refresh,
            screen == primary
        });
    }

    return out;
}

bool ScreenManager::apply(ApplyOptions const& options, std::string& error) {
    NSArray<NSScreen*>* screens = orderedScreens();
    if (options.screenIndex < 0 || options.screenIndex >= static_cast<int>(screens.count)) {
        error = "The selected screen index is out of range.";
        return false;
    }

    NSWindow* win = gameWindow();
    if (!win) {
        error = "Geometry Dash's NSWindow could not be found.";
        return false;
    }

    NSScreen* target = screens[options.screenIndex];
    bool isFullscreen = (win.styleMask & NSWindowStyleMaskFullScreen) != 0;
    NSRect originalFrame = win.frame;

    if (isFullscreen) {
        [win toggleFullScreen:nil];
        [win setFrame:target.frame display:YES animate:NO];
        [win toggleFullScreen:nil];
    } else {
        NSRect frame = originalFrame;
        frame.origin.x = target.frame.origin.x + (target.frame.size.width - frame.size.width) / 2.0;
        frame.origin.y = target.frame.origin.y + (target.frame.size.height - frame.size.height) / 2.0;
        [win setFrame:frame display:YES animate:NO];
    }

    [win makeKeyAndOrderFront:nil];
    return true;
}

} // namespace selectscreen
