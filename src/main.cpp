#include <Geode/Geode.hpp>
#include "CorruptorManager.hpp"

using namespace geode::prelude;

class CorruptorTicker : public CCObject {
public:
    void tick(float dt) {
        CorruptorManager::get()->onGlobalTick(dt);
    }
};

// В Geode v5 выгрузки модов нет, поэтому тикер живёт до конца сессии игры.
static CorruptorTicker* g_ticker = nullptr;

$on_mod(Loaded) {
    CorruptorManager::get()->loadFromSettings();

    g_ticker = new CorruptorTicker();
    g_ticker->autorelease();
    g_ticker->retain();
    CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
        schedule_selector(CorruptorTicker::tick), g_ticker, 0.05f, false
    );
}
