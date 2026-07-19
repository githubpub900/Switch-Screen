#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCApplication.hpp>
#include "ScreenManager.hpp"

using namespace geode::prelude;
using namespace selectscreen;

namespace {
    bool g_appliedOnce = false;

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

#if !defined(GEODE_IS_WINDOWS)
    void scheduleScreenRecovery() {
        Loader::get()->queueInMainThread([] {
            applyConfiguredScreen(false);
        });
    }
#endif
}

$execute {
    listenForSettingChanges<int64_t>("screen", [](int64_t) {
        Loader::get()->queueInMainThread([] {
            applyConfiguredScreen(true);
        });
    });
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

#if !defined(GEODE_IS_WINDOWS)
class $modify(SelectScreenApplication, CCApplication) {
    void applicationWillEnterForeground() {
        CCApplication::applicationWillEnterForeground();
        scheduleScreenRecovery();
    }

    void applicationWillBecomeActive() {
        CCApplication::applicationWillBecomeActive();
        scheduleScreenRecovery();
    }
};
#endif
