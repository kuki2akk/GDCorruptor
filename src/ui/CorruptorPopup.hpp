#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;

class CorruptorPopup : public geode::Popup {
protected:
    Slider* m_slider = nullptr;
    CCLabelBMFont* m_valueLabel = nullptr;
    ButtonSprite* m_autoBtnSprite = nullptr;

    // В v5 Popup::init не виртуальный — просто перекрываем его именем
    // (create() вызывает именно наш init через указатель CorruptorPopup*)
    bool init(float width, float height, char const* bg, cocos2d::CCRect bgRect);
    void updateLabel();
    void updateAutoLabel();
    void onSliderChanged(CCObject*);
    void onReset(CCObject*);
    void onToggleAuto(CCObject*);

public:
    static CorruptorPopup* create();
};
