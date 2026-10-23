#pragma once

#include <Geode/Geode.hpp>
#include <random>
#include <vector>

using namespace geode::prelude;

class PlayLayer;
class PlayerObject;

namespace FMOD {
    class DSP;
}

class CorruptorManager {
protected:
    struct PlayerState {
        float baseSpeed = 1.f;
        bool speedCorrupted = false;
    };

    struct NodeCorruption {
        CCNode* node = nullptr; // retain() при порче, release() при восстановлении
        CCTexture2D* origTexture = nullptr; // retain() при захвате, release() при восстановлении
        CCRect origRect = { 0.f, 0.f, 0.f, 0.f };
        ccColor3B origColor = { 255, 255, 255 };
        CCPoint origPos = CCPointZero;
        float origRotation = 0.f;
        float origScaleX = 1.f;
        float origScaleY = 1.f;
        bool origFlipX = false;
        bool origCascadeColor = true;
        bool hasTexture = false;
        bool hasColor = false;
        bool colorCorrupted = false; // цвет реально меняли — только тогда и возвращаем
    };

    int m_chaos = 0;

    // Активный бёрст хаоса: сколько секунд осталось (0 = выключено)
    float m_burstTime = 0.f;

    // Короткое превью при движении ползунка: полная порча (текстуры, сцена,
    // питч, скорость), но только на эти секунды — не бесконечно.
    float m_previewTime = 0.f;

    // Режим автокоррапта: порча идёт непрерывно, пока включено
    bool m_autoMode = false;

    // Только детектор смены уровня. НИКОГДА не разыменовывать вне тика:
    // после выхода из уровня PlayLayer освобождается и указатель висит.
    PlayLayer* m_currentLayer = nullptr;
    PlayerState m_p1;
    PlayerState m_p2;

    float m_speedCooldown = 0.f;
    float m_audioTimer = 0.f;
    float m_glitchCooldown = 0.f;
    float m_textureCooldown = 0.f;

    // Исходная громкость музыки: сохраняем при первой порче, возвращаем в stopAll
    float m_origMusicVolume = 1.f;
    bool m_musicVolumeSaved = false;

    // Плавное аудио: цели меняются редко, текущие значения медленно
    // подползают к ним — никаких резких скачков питча/дисторшна.
    float m_curPitch = 1.f;
    float m_targetPitch = 1.f;
    float m_curDistortion = 0.f;
    float m_targetDistortion = 0.f;
    float m_curMusicVolume = 1.f;
    float m_targetMusicVolume = 1.f;

    // DSP-дисторшн: на канале музыки и на глобальном канале (сфиксы).
    FMOD::DSP* m_musicDistortion = nullptr;
    FMOD::DSP* m_sfxDistortion = nullptr;

    // Глюк всей сцены (сдвиг/поворот корня). Сцена retain()'ирована,
    // пока хранится указатель — переживает смену сцен без висения.
    CCScene* m_glitchScene = nullptr;
    CCPoint m_glitchSceneOrigPos = CCPointZero;
    float m_glitchSceneOrigRot = 0.f;

    std::vector<NodeCorruption> m_corruptions;
    // Кадры для подмены: и кадр, и его текстура retain()'ированы —
    // чистка кэшей игры (removeUnusedTextures/SpriteFrames) их не тронет.
    struct PoolEntry {
        CCSpriteFrame* frame = nullptr;
        CCTexture2D* texture = nullptr;
    };
    std::vector<PoolEntry> m_framePool;
    std::vector<CCNode*> m_visitCache;
    bool m_framePoolSeeded = false;

    std::mt19937 m_rng;

    float roll01();
    bool chance(float base);

    void applySpeedChaos(PlayerObject* player, PlayerState& state, float dt);
    void pickAudioTargets();
    void applyAudioSmoothing(float dt);
    void applySceneGlitch(CCScene* scene);
    void applyTextureChaos(float dt);
    void restoreCorruption(NodeCorruption& corruption);
    void clearFramePool();
    bool hasActiveChaos() const;
    void stopAll();

public:
    static CorruptorManager* get();

    void setChaos(int percent);
    int getChaos() const { return m_chaos; }
    void loadFromSettings();

    // Запустить бёрст хаоса (по кнопке). Интенсивность = текущий процент.
    void triggerBurst();

    // Короткое превью при движении ползунка: на мгновение ломает всё
    // (как бёрст), работает независимо от авто-режима и никогда не бесконечно
    void previewBurst();

    void setAutoMode(bool on);
    bool isAutoMode() const { return m_autoMode; }

    // Немедленно остановить порчу (кнопка Reset / ползунок на 0%)
    void stopAllNow() { stopAll(); }

    // Глобальный тик (0.05с) из main.cpp: работает в любом экране —
    // уровень, выбор уровней, гардероб, пауза, выбор редактора и т.д.
    void onGlobalTick(float dt);
};
