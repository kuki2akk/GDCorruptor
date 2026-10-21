#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/NodeIDs.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../ui/CorruptorPopup.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace {
    // В 2.2081 файл circle.png — белый квадрат-заглушка, поэтому круг рисуем
    // сами через CCDrawNode: белый диск с тёмной обводкой, иконка тёмная.
    CCNode* makeCircleButton(const char* iconFrame, float iconScale, ccColor3B iconColor) {
        const float size = 54.f;

        auto* bg = CCDrawNode::create();
        bg->setContentSize({ size, size });
        const CCPoint center(size / 2.f, size / 2.f);
        bg->drawDot(center, size / 2.f, ccc4f(0.f, 0.f, 0.f, 0.35f));
        bg->drawDot(center, size / 2.f - 2.5f, ccc4f(1.f, 1.f, 1.f, 1.f));

        if (auto* icon = CCSprite::createWithSpriteFrameName(iconFrame)) {
            icon->setPosition(center);
            const float iw = std::max(icon->getContentSize().width, 1.f);
            const float ih = std::max(icon->getContentSize().height, 1.f);
            icon->setScale(size * iconScale / std::max(iw, ih));
            icon->setColor(iconColor);
            bg->addChild(icon);
        }
        return bg;
    }
}

struct MenuLayerHook : geode::Modify<MenuLayerHook, MenuLayer> {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        if (auto* menu = this->getChildByID("bottom-menu")) {
            // Единственная кнопка мода в главном меню — открывает наш попап,
            // где ползунок и авто-режим.
            auto* settingsBtn = CCMenuItemSpriteExtra::create(
                makeCircleButton("GJ_hammerIcon_001.png", 0.6f, { 40, 40, 40 }),
                this, menu_selector(MenuLayerHook::onCorruptorButton));
            settingsBtn->setID("corruptor-settings"_spr);
            menu->addChild(settingsBtn);

            menu->updateLayout();
        }
        return true;
    }

    void onCorruptorButton(CCObject*) {
        CorruptorPopup::create()->show();
    }
};
