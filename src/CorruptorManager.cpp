#include "CorruptorManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/CCPartAnimSprite.hpp>
#include <Geode/fmod/fmod.hpp>
#include <Geode/fmod/fmod_dsp_effects.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string_view>

using namespace geode::prelude;

namespace {
    constexpr size_t kMaxCorruptedNodes = 1200;
    constexpr size_t kMaxFramePool = 256;
    constexpr size_t kMaxVisited = 6000;

    // Яркий «неоновый» цвет: один канал на максимуме — заметно на любом фоне
    ccColor3B glitchColor(float r01, float r02, float r03) {
        GLubyte ch[3] = { 0, 0, 0 };
        const int hot = static_cast<int>(r01 * 3.f) % 3;
        const int mid = (hot + 1 + static_cast<int>(r02 * 2.f)) % 3;
        const int low = 3 - hot - mid;
        ch[hot] = 255;
        ch[mid] = static_cast<GLubyte>(40 + r03 * 180.f);
        ch[low] = static_cast<GLubyte>(r02 * 70.f);
        return ccc3(ch[0], ch[1], ch[2]);
    }
}

CorruptorManager* CorruptorManager::get() {
    static CorruptorManager instance;
    return &instance;
}

void CorruptorManager::loadFromSettings() {
    // При запуске всегда 0% и авто выключено: хаос не должен включаться сам.
    // Сохранённые значения перезаписываем, чтобы старые тестовые настройки
    // не оживали при следующем старте.
    Mod::get()->setSavedValue<int>("chaos", 0);
    Mod::get()->setSavedValue<bool>("auto", false);
    m_chaos = 0;
    m_autoMode = false;
}

void CorruptorManager::setChaos(int percent) {
    m_chaos = std::clamp(percent, 0, 100);
    Mod::get()->setSavedValue<int>("chaos", m_chaos);
}

void CorruptorManager::setAutoMode(bool on) {
    m_autoMode = on;
    Mod::get()->setSavedValue<bool>("auto", on);
    if (!on) {
        // Выключили авто — гасим всё, что сейчас портится
        stopAll();
    }
}

void CorruptorManager::previewBurst() {
    if (m_chaos <= 0) {
        return;
    }
    // Мгновенный показ интенсивности при движении ползунка:
    // короткий таймер, по которому ломается всё (текстуры, сцена, питч).
    m_previewTime = 1.2f;
}

float CorruptorManager::roll01() {
    return std::uniform_real_distribution<float>(0.f, 1.f)(m_rng);
}

bool CorruptorManager::chance(float base) {
    return roll01() < (static_cast<float>(m_chaos) / 100.f) * base;
}

void CorruptorManager::triggerBurst() {
    if (m_chaos <= 0) {
        return;
    }
    // 6..20 секунд хаоса в зависимости от процента
    m_burstTime = 6.f + 14.f * (static_cast<float>(m_chaos) / 100.f);
    m_audioTimer = 0.f;
    m_speedCooldown = 0.f;
    m_glitchCooldown = 0.f;
}

void CorruptorManager::onGlobalTick(float dt) {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (scene == nullptr) {
        return;
    }

    // Игровой слой есть только в уровне (и в паузе поверх него).
    // В меню/гардеробе/выборе уровней его нет — порча работает по всей сцене.
    auto* playLayer = scene->getChildByType<PlayLayer>(0);

    // Смена контекста: отпускаем порченые узлы прошлой сцены
    if (playLayer != m_currentLayer) {
        m_currentLayer = playLayer;
        m_p1 = PlayerState{};
        m_p2 = PlayerState{};
        m_speedCooldown = 0.f;
        m_audioTimer = 0.f;
        m_glitchCooldown = 0.f;
        for (auto& c : m_corruptions) {
            restoreCorruption(c);
        }
        m_corruptions.clear();
        clearFramePool();
        if (playLayer != nullptr) {
            if (auto* p1 = playLayer->m_player1) {
                m_p1.baseSpeed = p1->m_playerSpeed;
            }
            if (auto* p2 = playLayer->m_player2) {
                m_p2.baseSpeed = p2->m_playerSpeed;
            }
        }
    }

    if (m_chaos <= 0) {
        if (hasActiveChaos()) {
            stopAll();
        }
        return;
    }

    // Без бёрста, превью ползунка и авто-режима хаос полностью отключён
    const bool preview = m_previewTime > 0.f;
    const bool active = m_autoMode || m_burstTime > 0.f || preview;
    if (!active) {
        return;
    }
    if (!m_autoMode) {
        m_burstTime -= dt;
    }
    if (preview) {
        m_previewTime -= dt;
    }
    const float chaos01 = static_cast<float>(m_chaos) / 100.f;

    // Превью ползунка ломает всё (как бёрст), но ограничено по времени:
    // таймер обновляется при каждом движении ползунка и гасится ниже.
    m_speedCooldown = std::max(0.f, m_speedCooldown - dt);
    m_glitchCooldown = std::max(0.f, m_glitchCooldown - dt);

    // Скорость игроков — только внутри уровня
    if (playLayer != nullptr) {
        auto* player1 = playLayer->m_player1;
        auto* player2 = playLayer->m_player2;

        if (player1 != nullptr && !m_p1.speedCorrupted) {
            m_p1.baseSpeed = player1->m_playerSpeed;
        }
        if (player2 != nullptr && !m_p2.speedCorrupted) {
            m_p2.baseSpeed = player2->m_playerSpeed;
        }

        // Случайная подмена скорости (только горизонталь — игрока не подбрасывает)
        if (player1 != nullptr && !player1->m_isDead && chance(0.9f)) {
            applySpeedChaos(player1, m_p1, dt);
        }
        if (player2 != nullptr && !player2->m_isDead && chance(0.9f)) {
            applySpeedChaos(player2, m_p2, dt);
        }
    }

    // Аудио: цели меняются редко (1.2с), значения плавно ползут к ним
    if (m_audioTimer <= 0.f) {
        pickAudioTargets();
        m_audioTimer = 1.2f;
    } else {
        m_audioTimer -= dt;
    }
    applyAudioSmoothing(dt);

    // Сдвиг/поворот всей сцены — видно в любом экране игры
    applySceneGlitch(scene);

    // Текстурная порча не каждый тик — с паузой, чтобы хаос нарастал плавно
    m_textureCooldown = std::max(0.f, m_textureCooldown - dt);
    if (m_textureCooldown <= 0.f) {
        applyTextureChaos(dt);
        m_textureCooldown = 0.2f;
    }

    if (!m_autoMode && m_burstTime <= 0.f && m_previewTime <= 0.f) {
        stopAll();
    }
}

void CorruptorManager::applySpeedChaos(PlayerObject* player, PlayerState& state, float dt) {
    if (player == nullptr || m_speedCooldown > 0.f) {
        return;
    }
    if (roll01() < 0.5f) {
        static const float kSpeeds[] = { 0.7f, 0.8f, 0.9f, 1.1f, 1.3f };
        if (!state.speedCorrupted) {
            state.baseSpeed = player->m_playerSpeed;
        }
        const int idx = static_cast<int>(roll01() * std::size(kSpeeds)) % std::size(kSpeeds);
        player->m_playerSpeed = kSpeeds[idx];
        state.speedCorrupted = true;
        m_speedCooldown = 1.2f;
    }
}

void CorruptorManager::pickAudioTargets() {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (engine == nullptr || engine->m_system == nullptr) {
        return;
    }
    const float chaos01 = static_cast<float>(m_chaos) / 100.f;

    // Питч всего микса (музыка + сфиксы), дисторшн и громкость музыки
    m_targetPitch = 0.6f + roll01() * 1.1f;             // 0.6 .. 1.7
    m_targetDistortion = (0.15f + roll01() * 0.75f) * chaos01; // 0 .. 0.9
    m_targetMusicVolume = 0.25f + roll01() * 1.25f;     // 0.25 .. 1.5

    if (!m_musicVolumeSaved) {
        m_origMusicVolume = engine->m_musicVolume;
        m_musicVolumeSaved = true;
    }
}

void CorruptorManager::applyAudioSmoothing(float dt) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (engine == nullptr || engine->m_system == nullptr) {
        return;
    }

    // Медленное подползание к целям: переход занимает ~1.5-2 секунды,
    // так что питч/дисторшн "плывут", а не дёргаются каждый раз.
    const float k = std::min(1.f, dt * 2.0f);
    m_curPitch += (m_targetPitch - m_curPitch) * k;
    m_curDistortion += (m_targetDistortion - m_curDistortion) * k;
    m_curMusicVolume += (m_targetMusicVolume - m_curMusicVolume) * k;

    FMOD::ChannelGroup* master = nullptr;
    if (engine->m_system->getMasterChannelGroup(&master) == FMOD_OK && master != nullptr) {
        master->setPitch(m_curPitch);
    }

    // DSP-дисторшн на канале музыки и на глобальном канале (сфиксы).
    // DSP создаётся один раз и висит до stopAll; bypass экономит CPU при 0.
    auto applyDsp = [engine](FMOD::DSP*& dsp, FMOD::ChannelGroup* group, float level) {
        if (dsp == nullptr && group != nullptr) {
            engine->m_system->createDSPByType(FMOD_DSP_TYPE_DISTORTION, &dsp);
            if (dsp != nullptr) {
                group->addDSP(0, dsp);
            }
        }
        if (dsp != nullptr) {
            dsp->setBypass(level < 0.01f);
            dsp->setParameterFloat(FMOD_DSP_DISTORTION_LEVEL, level);
        }
    };
    applyDsp(m_musicDistortion, engine->m_backgroundMusicChannel, m_curDistortion);
    applyDsp(m_sfxDistortion, engine->m_globalChannel, m_curDistortion);

    // Громкость музыки: пишем и в движок, и в сам канал,
    // чтобы игровой код не вернул громкость сам.
    engine->m_musicVolume = m_curMusicVolume;
    if (engine->m_backgroundMusicChannel != nullptr) {
        engine->m_backgroundMusicChannel->setVolume(m_curMusicVolume);
    }
}

void CorruptorManager::applySceneGlitch(CCScene* scene) {
    if (m_glitchCooldown > 0.f || scene == nullptr) {
        return;
    }
    const float chaos01 = static_cast<float>(m_chaos) / 100.f;

    // Захватили новую сцену — прежнюю (если держали) возвращаем на место.
    // Сцена удержана retain(): даже если игра её уже сменила, указатель валиден.
    if (m_glitchScene != scene) {
        if (m_glitchScene != nullptr) {
            m_glitchScene->setPosition(m_glitchSceneOrigPos);
            m_glitchScene->setRotation(m_glitchSceneOrigRot);
            m_glitchScene->release();
        }
        m_glitchScene = scene;
        m_glitchScene->retain();
        m_glitchSceneOrigPos = scene->getPosition();
        m_glitchSceneOrigRot = scene->getRotation();
    }

    const float amp = 1.5f + chaos01 * 5.f;
    scene->setPosition(m_glitchSceneOrigPos + ccp(
        (roll01() * 2.f - 1.f) * amp,
        (roll01() * 2.f - 1.f) * amp));
    scene->setRotation(
        m_glitchSceneOrigRot + (roll01() * 2.f - 1.f) * 1.5f * chaos01);

    // Сильный "глюк-скачок" на высоких процентах
    if (m_chaos >= 70 && roll01() < 0.35f) {
        scene->setPosition(m_glitchSceneOrigPos + ccp(
            (roll01() * 2.f - 1.f) * 12.f,
            (roll01() * 2.f - 1.f) * 12.f));
    }
    m_glitchCooldown = 0.25f;
}

void CorruptorManager::applyTextureChaos(float dt) {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (scene == nullptr) {
        return;
    }

    const float chaos01 = static_cast<float>(m_chaos) / 100.f;

    // Сколько узлов портить в этом кадре (0..18, растёт с процентом хаоса)
    const float base = chaos01 * 18.f;
    int budget = static_cast<int>(base);
    if (roll01() < base - static_cast<float>(budget)) {
        ++budget;
    }
    if (budget <= 0) {
        return;
    }

    // Один раз за сессию добавляем в пул подмены известные кадры игры
    if (!m_framePoolSeeded) {
        auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
        static const char* kSeedFrames[] = {
            "circle.png", "sliderthumb.png", "sliderBar.png", "slidergroove.png",
            "GJ_button_01.png", "GJ_square_01.png", "GJ_square_02.png",
            "GJ_hammerIcon_001.png", "GJ_arrow_01_001.png",
            "player_01.png", "player_02.png", "player_03.png", "player_05.png",
            "GJ_playBtn2_001.png", "GJ_likeBtn_001.png", "GJ_deleteIcon_001.png",
            "GJ_stopEditorBtn_001.png", "GJ_arrow_02_001.png",
            "gj_icon_001.png", "gj_icon_002.png", "gj_icon_003.png",
            "gj_icon_010.png", "goldCoin_small01_001.png",
        };
        if (cache != nullptr) {
            for (const char* name : kSeedFrames) {
                if (auto* frame = cache->spriteFrameByName(name)) {
                    PoolEntry entry;
                    entry.frame = frame;
                    entry.texture = frame->getTexture();
                    frame->retain();
                    if (entry.texture != nullptr) {
                        entry.texture->retain();
                    }
                    m_framePool.push_back(entry);
                }
            }
        }
        m_framePoolSeeded = true;
    }

    // Обход сцены: собираем узлы и пополняем пул кадров теми,
    // что реально используются в текущей сцене.
    m_visitCache.clear();
    m_visitCache.push_back(scene);
    for (size_t i = 0; i < m_visitCache.size() && m_visitCache.size() < kMaxVisited; ++i) {
        auto* node = m_visitCache[i];
        if (node == nullptr) {
            continue;
        }
        // Наши собственные кнопки/попап не трогаем
        const std::string_view nodeId(node->getID());
        if (!nodeId.empty() && nodeId.rfind("corruptor", 0) == 0) {
            continue;
        }
        // Внутрь игрока не лезем: его спрайты управляются игровой логикой
        if (geode::cast::typeinfo_cast<PlayerObject*>(node) != nullptr) {
            continue;
        }
        // Батч-узлы (в т.ч. CCLabelBMFont) не трогаем совсем: у их дочерних
        // спрайтов setColor идёт в CCSprite::updateColor -> updateQuad на атласе,
        // чей буфер/текстура после чистки кэшей игры оказываются освобождены —
        // это и есть вылеты в CCTextureAtlas::updateQuad.
        if (geode::cast::typeinfo_cast<CCSpriteBatchNode*>(node) != nullptr) {
            continue;
        }
        // Атлас-узлы (CCAtlasNode) переопределяют setColor/setScale на
        // updateAtlasValues с той же проблемой — пропускаем тоже.
        if (geode::cast::typeinfo_cast<CCAtlasNode*>(node) != nullptr) {
            continue;
        }
        for (auto* child : node->getChildrenExt()) {
            if (child != nullptr) {
                m_visitCache.push_back(child);
            }
        }
        if (auto* sprite = geode::cast::typeinfo_cast<CCSprite*>(node)) {
            // Пул полон — просто пропускаем. Ничего не выселяем:
            // освобождённый кадр мог остаться показанным на испорченном
            // спрайте и привести к вылету рендера.
            if (m_framePool.size() < kMaxFramePool) {
                auto* tex = sprite->getTexture();
                if (tex != nullptr) {
                    // Свой кадр из текстуры спрайта: не зависит от кэша кадров
                    // игры, который чистится при смене сцен (displayFrame() мог
                    // вернуть освобождённый указатель — по нему и были вылеты).
                    auto* frame = CCSpriteFrame::createWithTexture(
                        tex, sprite->getTextureRect());
                    if (frame != nullptr) {
                        PoolEntry entry;
                        entry.frame = frame;
                        entry.texture = tex;
                        frame->retain();
                        tex->retain();
                        m_framePool.push_back(entry);
                    }
                }
            }
        }
    }
    if (m_visitCache.size() <= 1) {
        return;
    }

    for (int k = 0; k < budget; ++k) {
        const size_t idx = 1 + static_cast<size_t>(
            roll01() * static_cast<float>(m_visitCache.size() - 1));
        auto* node = m_visitCache[idx % m_visitCache.size()];
        if (node == nullptr || node == scene) {
            continue;
        }

        // Дети батч-узлов не переносят смену текстуры/кадра — только цвет
        const bool batchChild = node->getParent() != nullptr &&
            geode::cast::typeinfo_cast<CCSpriteBatchNode*>(node->getParent()) != nullptr;
        // У PartAnim-спрайтов своя логика кадров/частей — текстуру не трогаем
        const bool partAnim = geode::cast::typeinfo_cast<CCPartAnimSprite*>(node) != nullptr;

        // Находим запись о порче узла; если нет — создаём и фиксируем
        // оригинальное состояние, чтобы всё вернуть по концу бёрста.
        NodeCorruption* entry = nullptr;
        for (auto& c : m_corruptions) {
            if (c.node == node) {
                entry = &c;
                break;
            }
        }
        if (entry == nullptr) {
            if (m_corruptions.size() >= kMaxCorruptedNodes) {
                restoreCorruption(m_corruptions.front());
                m_corruptions.erase(m_corruptions.begin());
            }
            NodeCorruption c;
            c.node = node;
            c.origRotation = node->getRotation();
            c.origScaleX = node->getScaleX();
            c.origScaleY = node->getScaleY();
            c.origPos = node->getPosition();
            if (auto* rgba = geode::cast::typeinfo_cast<CCNodeRGBA*>(node)) {
                c.origColor = rgba->getColor();
                c.origCascadeColor = rgba->isCascadeColorEnabled();
                c.hasColor = true;
            }
            if (auto* sprite = geode::cast::typeinfo_cast<CCSprite*>(node)) {
                c.origFlipX = sprite->isFlipX();
                // Текстуру фиксируем у любого спрайта, которому можем её
                // подменить (не ребёнок батч-узла и не PartAnim-спрайт).
                // displayFrame() НЕ используем: после чистки кэша кадров он
                // может вернуть освобождённый указатель. Текстура спрайта
                // удерживается самим спрайтом, плюс мы её явно retain()'им.
                if (!batchChild && !partAnim && sprite->getTexture() != nullptr) {
                    c.origTexture = sprite->getTexture();
                    c.origTexture->retain();
                    c.origRect = sprite->getTextureRect();
                    c.hasTexture = true;
                }
            }
            node->retain();
            m_corruptions.push_back(c);
            entry = &m_corruptions.back();
        }

        // Тип искажения: подмена текстуры / кусок текстуры / зеркалирование /
        // неоновый цвет / сдвиг позиции / поворот / масштаб.
        auto* sprite = geode::cast::typeinfo_cast<CCSprite*>(node);
        auto* rgba = geode::cast::typeinfo_cast<CCNodeRGBA*>(node);
        // Подмена текстуры работает на любом CCSprite-подклассе: setDisplayFrame
        // не вызываем вообще (GD-подклассы переопределяют его и могут упасть),
        // а setTexture + setTextureRect дают тот же визуальный результат.
        const bool canSwap = sprite != nullptr && !batchChild && !partAnim &&
            sprite->getTexture() != nullptr && !m_framePool.empty();
        const float r = roll01();
        if (r < 0.35f) {
            if (canSwap) {
                const auto& poolEntry = m_framePool[
                    static_cast<size_t>(roll01() * m_framePool.size()) % m_framePool.size()];
                // Кадр и его текстура из нашего пула — оба retain()'ированы,
                // чистка кэшей игры их не трогает.
                sprite->setTexture(poolEntry.texture);
                sprite->setTextureRect(poolEntry.frame->getRect());
            } else if (rgba != nullptr) {
                rgba->setCascadeColorEnabled(false);
                rgba->setColor(glitchColor(roll01(), roll01(), roll01()));
                entry->colorCorrupted = true;
            } else {
                node->setRotation((roll01() * 2.f - 1.f) * (10.f + 30.f * chaos01));
            }
        } else if (r < 0.46f) {
            // Случайный кусок собственной текстуры спрайта
            if (sprite != nullptr && !batchChild && !partAnim &&
                sprite->getTexture() != nullptr) {
                const auto texSize = sprite->getTexture()->getContentSize();
                if (texSize.width > 4.f && texSize.height > 4.f) {
                    const float w = std::min(texSize.width, std::max(2.f, roll01() * texSize.width));
                    const float h = std::min(texSize.height, std::max(2.f, roll01() * texSize.height));
                    const float x = roll01() * (texSize.width - w);
                    const float y = roll01() * (texSize.height - h);
                    sprite->setTextureRect(CCRectMake(x, y, w, h));
                }
            } else if (rgba != nullptr) {
                rgba->setCascadeColorEnabled(false);
                rgba->setColor(glitchColor(roll01(), roll01(), roll01()));
                entry->colorCorrupted = true;
            }
        } else if (r < 0.54f) {
            if (sprite != nullptr && !batchChild && !partAnim) {
                sprite->setFlipX(roll01() < 0.5f);
            } else {
                node->setScaleX(-std::fabs(node->getScaleX()));
            }
        } else if (r < 0.74f) {
            if (rgba != nullptr) {
                // Каскад отключаем: иначе setColor дойдёт до детей, в т.ч. до
                // спрайтов лейблов (батч-узлов), чей updateQuad падает на
                // битом атласе после чистки кэшей игры.
                rgba->setCascadeColorEnabled(false);
                rgba->setColor(glitchColor(roll01(), roll01(), roll01()));
                entry->colorCorrupted = true;
            } else {
                node->setRotation((roll01() * 2.f - 1.f) * (10.f + 30.f * chaos01));
            }
        } else if (r < 0.88f) {
            // Сдвиг позиции — трясёт всё сразу, видно на любом узле
            const float amp = 1.5f + chaos01 * 6.f;
            node->setPosition(entry->origPos + ccp(
                (roll01() * 2.f - 1.f) * amp,
                (roll01() * 2.f - 1.f) * amp));
        } else if (r < 0.96f) {
            node->setRotation((roll01() * 2.f - 1.f) * (10.f + 30.f * chaos01));
        } else {
            node->setScaleX(0.7f + roll01());
            node->setScaleY(0.7f + roll01());
        }
    }
}

void CorruptorManager::restoreCorruption(NodeCorruption& corruption) {
    auto* node = corruption.node;
    if (node == nullptr) {
        return;
    }
    // Если узел уже удалён из сцены (родитель == nullptr) — визуальное
    // восстановление не нужно, просто отпускаем удержанные ссылки.
    if (node->getParent() != nullptr) {
        if (auto* sprite = geode::cast::typeinfo_cast<CCSprite*>(node)) {
            // Возвращаем исходную текстуру и прямоугольник: текстура
            // retain()'ирована нами при захвате — жива в любом случае.
            if (corruption.hasTexture && corruption.origTexture != nullptr) {
                sprite->setTexture(corruption.origTexture);
                sprite->setTextureRect(corruption.origRect);
            }
            sprite->setFlipX(corruption.origFlipX);
        }
        if (corruption.hasColor && corruption.colorCorrupted) {
            if (auto* rgba = geode::cast::typeinfo_cast<CCNodeRGBA*>(node)) {
                // Возврат цвета без каскада, потом восстанавливаем исходный
                // флаг каскада (обычно true).
                rgba->setCascadeColorEnabled(false);
                rgba->setColor(corruption.origColor);
                rgba->setCascadeColorEnabled(corruption.origCascadeColor);
            }
        }
        node->setRotation(corruption.origRotation);
        node->setScaleX(corruption.origScaleX);
        node->setScaleY(corruption.origScaleY);
        node->setPosition(corruption.origPos);
    }
    if (corruption.origTexture != nullptr) {
        corruption.origTexture->release();
        corruption.origTexture = nullptr;
    }
    node->release();
    corruption.node = nullptr;
}

void CorruptorManager::clearFramePool() {
    for (auto& entry : m_framePool) {
        if (entry.frame != nullptr) {
            entry.frame->release();
        }
        if (entry.texture != nullptr) {
            entry.texture->release();
        }
    }
    m_framePool.clear();
    // После очистки пул нужно заново засеять (кадры могли быть выгружены)
    m_framePoolSeeded = false;
}

bool CorruptorManager::hasActiveChaos() const {
    return m_burstTime > 0.f || m_previewTime > 0.f ||
        !m_corruptions.empty() ||
        m_glitchScene != nullptr ||
        m_p1.speedCorrupted || m_p2.speedCorrupted;
}

void CorruptorManager::stopAll() {
    m_burstTime = 0.f;
    m_previewTime = 0.f;
    m_audioTimer = 0.f;
    m_speedCooldown = 0.f;
    m_glitchCooldown = 0.f;

    // Аудио -> нормальный питч, снятый дисторшн и исходная громкость музыки
    auto* engine = FMODAudioEngine::sharedEngine();
    if (engine != nullptr && engine->m_system != nullptr) {
        FMOD::ChannelGroup* master = nullptr;
        if (engine->m_system->getMasterChannelGroup(&master) == FMOD_OK && master != nullptr) {
            master->setPitch(1.f);
        }
    }
    if (engine != nullptr) {
        auto removeDsp = [](FMOD::DSP*& dsp, FMOD::ChannelGroup* group) {
            if (dsp != nullptr) {
                if (group != nullptr) {
                    group->removeDSP(dsp);
                }
                dsp->release();
            }
            dsp = nullptr;
        };
        removeDsp(m_musicDistortion, engine->m_backgroundMusicChannel);
        removeDsp(m_sfxDistortion, engine->m_globalChannel);
    }
    m_curPitch = 1.f;
    m_targetPitch = 1.f;
    m_curDistortion = 0.f;
    m_targetDistortion = 0.f;
    if (engine != nullptr && m_musicVolumeSaved) {
        engine->m_musicVolume = m_origMusicVolume;
        if (engine->m_backgroundMusicChannel != nullptr) {
            engine->m_backgroundMusicChannel->setVolume(m_origMusicVolume);
        }
        m_curMusicVolume = m_origMusicVolume;
        m_targetMusicVolume = m_origMusicVolume;
        m_musicVolumeSaved = false;
    }

    // Порченые узлы -> исходное состояние
    for (auto& c : m_corruptions) {
        restoreCorruption(c);
    }
    m_corruptions.clear();
    clearFramePool();

    // Сцена -> исходное положение. Сцена удержана retain(), указатель валиден.
    if (m_glitchScene != nullptr) {
        m_glitchScene->setPosition(m_glitchSceneOrigPos);
        m_glitchScene->setRotation(m_glitchSceneOrigRot);
        m_glitchScene->release();
        m_glitchScene = nullptr;
    }

    // Игроки. Работаем только с живым слоем из текущей сцены:
    // после выхода из уровня PlayLayer освобождается и его нельзя разыменовывать.
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (scene != nullptr) {
        if (auto* playLayer = scene->getChildByType<PlayLayer>(0)) {
            auto* player1 = playLayer->m_player1;
            auto* player2 = playLayer->m_player2;
            if (player1 != nullptr) {
                if (m_p1.speedCorrupted) {
                    player1->m_playerSpeed = m_p1.baseSpeed;
                }
                m_p1.speedCorrupted = false;
            }
            if (player2 != nullptr) {
                if (m_p2.speedCorrupted) {
                    player2->m_playerSpeed = m_p2.baseSpeed;
                }
                m_p2.speedCorrupted = false;
            }
        }
    } else {
        m_currentLayer = nullptr;
    }
}
