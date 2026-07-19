#include <Geode/Geode.hpp>
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

    class ScreenRecoveryRunner : public CCNode {
    protected:
        int m_tick = 0;

        bool init() override {
            if (!CCNode::init()) return false;
            this->schedule(schedule_selector(ScreenRecoveryRunner::tick), 0.05f);
            return true;
        }

        void tick(float) {
            ++m_tick;

            // Exclusive fullscreen is commonly recreated a little after focus returns.
            // Reapply across that restoration window instead of moving it only once.
            if (m_tick == 1 || m_tick == 3 || m_tick == 6 || m_tick == 10 ||
                m_tick == 16 || m_tick == 24 || m_tick == 32) {
                applyConfiguredScreen(false);
            }

            if (m_tick >= 36) {
                this->unschedule(schedule_selector(ScreenRecoveryRunner::tick));
                this->removeFromParentAndCleanup(true);
            }
        }

    public:
        static ScreenRecoveryRunner* create() {
            auto result = new ScreenRecoveryRunner();
            if (result && result->init()) {
                result->autorelease();
                return result;
            }
            CC_SAFE_DELETE(result);
            return nullptr;
        }
    };

    void scheduleScreenRecovery() {
        Loader::get()->queueInMainThread([] {
            auto scene = CCDirector::sharedDirector()->getRunningScene();
            if (!scene) {
                applyConfiguredScreen(false);
                return;
            }

            // Replace any previous recovery runner so repeated focus events do not
            // create several overlapping correction loops.
            constexpr int recoveryTag = 0x53534352; // "SSCR"
            scene->removeChildByTag(recoveryTag, true);

            auto runner = ScreenRecoveryRunner::create();
            if (!runner) {
                applyConfiguredScreen(false);
                return;
            }
            runner->setTag(recoveryTag);
            scene->addChild(runner);
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
    void applicationWillEnterForeground() {
        CCApplication::applicationWillEnterForeground();
        scheduleScreenRecovery();
    }

    void applicationWillBecomeActive() {
        CCApplication::applicationWillBecomeActive();

        // Alt+Tab normally causes an inactive/active transition without necessarily
        // minimizing the native window. Geometry Dash may recreate exclusive fullscreen
        // after this callback, so schedule several corrections over the next ~1.8 seconds.
        scheduleScreenRecovery();
    }
};
