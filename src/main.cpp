#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCApplication.hpp>
#include "ScreenManager.hpp"

using namespace geode::prelude;
using namespace selectscreen;

namespace {
    bool g_appliedOnce = false;
    bool g_wasBackgrounded = false;

    ApplyOptions readOptions() {
        ApplyOptions options;
        options.screenIndex = static_cast<int>(Mod::get()->getSettingValue<int64_t>("screen")) - 1;
        return options;
    }

    void applyConfiguredScreen(bool showFailurePopup = false) {
        auto screens = ScreenManager::enumerate();
        if (screens.empty()) {
            log::error("Select Screen: no displays were detected");
            return;
        }

        auto options = readOptions();
        if (options.screenIndex < 0) options.screenIndex = 0;
        if (options.screenIndex >= static_cast<int>(screens.size())) {
            log::warn(
                "Configured screen {} does not exist; using screen {}",
                options.screenIndex + 1,
                screens.size()
            );
            options.screenIndex = static_cast<int>(screens.size()) - 1;
        }

        std::string error;
        if (!ScreenManager::apply(options, error)) {
            log::error("Select Screen failed: {}", error);
            if (showFailurePopup) {
                FLAlertLayer::create(
                    "Select Screen",
                    fmt::format("Unable to move Geometry Dash to the selected screen.<br><br>{}", error),
                    "OK"
                )->show();
            }
            return;
        }

        auto const& target = screens.at(options.screenIndex);
        log::info(
            "Selected screen {}: {} ({}x{} at {},{})",
            options.screenIndex + 1,
            target.name,
            target.width,
            target.height,
            target.x,
            target.y
        );
    }

    void applyNextFrame(bool showFailurePopup = false) {
        Loader::get()->queueInMainThread([showFailurePopup] {
            applyConfiguredScreen(showFailurePopup);
        });
    }
}

$on_mod(Loaded) {
    auto screens = ScreenManager::enumerate();
    log::info("Select Screen detected {} display(s)", screens.size());
    for (auto const& screen : screens) {
        log::info(
            "Screen {}: {} - {}x{} at {},{}{}",
            screen.index + 1,
            screen.name,
            screen.width,
            screen.height,
            screen.x,
            screen.y,
            screen.primary ? " [primary]" : ""
        );
    }
}

class $modify(SelectScreenMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        if (!g_appliedOnce) {
            g_appliedOnce = true;
            this->runAction(CCSequence::create(
                CCDelayTime::create(0.15f),
                CCCallFunc::create(this, callfunc_selector(SelectScreenMenuLayer::applyInitialScreen)),
                nullptr
            ));
        }
        return true;
    }

    void applyInitialScreen() {
        applyConfiguredScreen(false);
    }
};

class $modify(SelectScreenApplication, CCApplication) {
    void applicationDidEnterBackground() {
        g_wasBackgrounded = true;
        CCApplication::applicationDidEnterBackground();
    }

    void applicationWillEnterForeground() {
        CCApplication::applicationWillEnterForeground();

        // Reapply only after the game was actually minimized/backgrounded. This is
        // intentionally not an Alt-Tab/focus option: it fixes the native window being
        // recreated or restored on the primary monitor after minimizing Geometry Dash.
        if (g_wasBackgrounded) {
            g_wasBackgrounded = false;
            applyNextFrame(false);
        }
    }
};
