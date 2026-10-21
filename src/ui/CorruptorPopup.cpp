#include "CorruptorPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/NodeIDs.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../CorruptorManager.hpp"

#include <fmt/format.h>
#include <cmath>

using namespace geode::prelude;

CorruptorPopup* CorruptorPopup::create() {
    auto* ret = new CorruptorPopup();
    if (ret->init(340.f, 210.f, "GJ_square01.png", {})) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool CorruptorPopup::init(float width, float height, char const* bg, cocos2d::CCRect bgRect) {
    if (!Popup::init(width, height, bg, bgRect)) {
        return false;
    }

    // Защита от собственной порчи: DFS хаоса пропускает поддеревья с ID "corruptor*"
    this->setID("corruptor-popup"_spr);
    this->setTitle("GDCorruptor");

    const auto center = m_mainLayer->getContentSize() / 2.f;

    auto* info = CCLabelBMFont::create(
        "Chaos level. Auto = endless corruption.", "bigFont.fnt");
    info->setScale(0.45f);
    info->setPosition(center.width, center.height + 58.f);
    m_mainLayer->addChild(info);

    m_valueLabel = CCLabelBMFont::create("0%", "bigFont.fnt");
    m_valueLabel->setPosition(center.width, center.height + 26.f);
    m_mainLayer->addChild(m_valueLabel);

    // Игровой композит Slider: сам создаёт полосу, желобок и SliderTouchLogic,
    // корректно инициализированный кодом игры (в отличие от ручного
    // SliderTouchLogic::create, который оставляет внутренний указатель m_slider
    // мусором — на нём падает хук других модов, например xdbot).
    m_slider = Slider::create(
        this,
        menu_selector(CorruptorPopup::onSliderChanged),
        1.f
    );
    if (m_slider != nullptr) {
        m_slider->setID("corruptor-slider"_spr);
        m_slider->setLiveDragging(true);
        m_slider->setValue(
            static_cast<float>(CorruptorManager::get()->getChaos()) / 100.f);
        // Центрируем видимую дорожку по фактическому положению groove внутри
        // слайдера (компенсируем любой внутренний layout игры).
        CCPoint trackCenter = CCPointZero;
        if (m_slider->m_groove != nullptr) {
            trackCenter = m_slider->m_groove->getPosition();
        }
        m_slider->setPosition(center.width - trackCenter.x, center.height - trackCenter.y);
        m_mainLayer->addChild(m_slider);
    }

    // Кнопка сброса в 0%
    auto* resetBtn = ButtonSprite::create("Reset (0%)", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    auto* resetItem = CCMenuItemSpriteExtra::create(
        resetBtn, this, menu_selector(CorruptorPopup::onReset));
    resetItem->setPosition(center.width, center.height - 48.f);
    m_buttonMenu->addChild(resetItem);

    // Кнопка авто-режима: порча идёт бесконечно, пока включено
    m_autoBtnSprite = ButtonSprite::create("Auto: OFF", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    auto* autoItem = CCMenuItemSpriteExtra::create(
        m_autoBtnSprite, this, menu_selector(CorruptorPopup::onToggleAuto));
    autoItem->setPosition(center.width, center.height - 88.f);
    m_buttonMenu->addChild(autoItem);

    this->updateLabel();
    this->updateAutoLabel();
    return true;
}

void CorruptorPopup::updateAutoLabel() {
    if (m_autoBtnSprite != nullptr) {
        m_autoBtnSprite->setString(
            CorruptorManager::get()->isAutoMode() ? "Auto: ON" : "Auto: OFF");
    }
}

void CorruptorPopup::onToggleAuto(CCObject*) {
    auto* mgr = CorruptorManager::get();
    mgr->setAutoMode(!mgr->isAutoMode());
    this->updateAutoLabel();
}

void CorruptorPopup::updateLabel() {
    if (m_valueLabel != nullptr) {
        m_valueLabel->setString(
            fmt::format("{}%", CorruptorManager::get()->getChaos()).c_str());
    }
}

void CorruptorPopup::onSliderChanged(CCObject*) {
    // Отправитель может быть и SliderThumb, и самим Slider'ом — не полагаемся
    // на него, читаем текущее значение напрямую из композита.
    if (m_slider == nullptr) {
        return;
    }
    const float value = m_slider->getValue();
    const int percent = static_cast<int>(std::round(value * 100.f));
    auto* mgr = CorruptorManager::get();
    mgr->setChaos(percent);
    if (percent <= 0) {
        // Ползунок на ноль — сразу всё гасим
        mgr->stopAllNow();
    } else {
        // Мгновенное превью: коротко ломается всё, чтобы сразу видеть
        // выбранную интенсивность (не бесконечно и не зависит от авто-режима)
        mgr->previewBurst();
    }
    this->updateLabel();
}

void CorruptorPopup::onReset(CCObject*) {
    auto* mgr = CorruptorManager::get();
    mgr->setChaos(0);
    mgr->stopAllNow();
    if (m_slider != nullptr) {
        m_slider->setValue(0.f);
    }
    this->updateLabel();
}
