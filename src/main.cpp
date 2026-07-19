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
        auto mod = Mod::get();
        ApplyOptions options;
        options.screenIndex = static_cast<int>(mod->getSettingValue<int64_t>("screen")) - 1;
        options.mode = parseDisplayMode(mod->getSettingValue<std::string>("display-mode"));
        options.refreshRate = static_cast<int>(mod->getSettingValue<int64_t>("refresh-rate"));
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
            log::warn("Configured screen {} does not exist; using screen {}", options.screenIndex + 1, screens.size());
            options.screenIndex = static_cast<int>(screens.size()) - 1;
        }

        auto const& target = screens.at(options.screenIndex);
        std::string error;
        if (!ScreenManager::apply(options, error)) {
            log::error("Select Screen failed: {}", error);
            if (showFailurePopup) {
                FLAlertLayer::create("Select Screen", fmt::format("Unable to apply the selected screen.<br><br>{}", error), "OK")->show();
            }
            return;
        }

        log::info("Applied {} on screen {}: {} ({}x{} @ {} Hz)",
            displayModeName(options.mode), options.screenIndex + 1, target.name,
            target.width, target.height, target.refreshRate);
    }

    void applySoon() {
        Loader::get()->queueInMainThread([] {
            applyConfiguredScreen(true);
        });
    }
}

$on_mod(Loaded) {
    auto screens = ScreenManager::enumerate();
    log::info("Select Screen detected {} display(s)", screens.size());
    for (auto const& screen : screens) {
        log::info("Screen {}: {} - {}x{} @ {} Hz at {},{}{}",
            screen.index + 1, screen.name, screen.width, screen.height,
            screen.refreshRate, screen.x, screen.y, screen.primary ? " [primary]" : "");
    }

    listenForSettingChanges<int64_t>("screen", [](int64_t) {
        if (Mod::get()->getSettingValue<bool>("apply-immediately")) applySoon();
    });
    listenForSettingChanges<std::string>("display-mode", [](std::string) {
        if (Mod::get()->getSettingValue<bool>("apply-immediately")) applySoon();
    });
    listenForSettingChanges<int64_t>("refresh-rate", [](int64_t) {
        if (Mod::get()->getSettingValue<bool>("apply-immediately")) applySoon();
    });
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
    void applicationWillEnterForeground() {
        CCApplication::applicationWillEnterForeground();
        if (Mod::get()->getSettingValue<bool>("restore-on-focus")) {
            Loader::get()->queueInMainThread([] { applyConfiguredScreen(false); });
        }
    }
};
