#include "../fsm_utils.h"
#include "../fsm.h"

#include "../../drivers/heater.h"
#include "../../drivers/pump.h"
#include "../../drivers/valves.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"
#include "../rect_takeoff.h"
#include "../power_command_gateway.h"
#include "../v2/reason_codes.h"
#include "../v2/safety_policy.h"
#include "../v2/status_adapter.h"

#include <Arduino.h>

namespace FSM {
namespace ManualRect {

namespace {

bool alertSent = false;
uint32_t lastFloodTime = 0;
float manualTakeoffRateMlH = 0.0f;

uint8_t getManualPhasePowerPercent(const Settings& settings, RectPhase phase) {
    // Используем новые настройки из ManualRectSettings
    const float maxPowerW = settings.equipment.heaterPowerW > 0
        ? settings.equipment.heaterPowerW
        : 3000.0f;

    float targetPowerW = 0.0f;
    switch (phase) {
        case RectPhase::STABILIZATION:
        case RectPhase::POST_HEADS_STABILIZATION:
        case RectPhase::PURGE:
            targetPowerW = settings.manualRect.headsPowerW * 0.8f; // Стабилизация 80% от голов
            break;
        case RectPhase::HEADS:
            targetPowerW = settings.manualRect.headsPowerW;
            break;
        case RectPhase::BODY:
            targetPowerW = settings.manualRect.bodyPowerW;
            break;
        case RectPhase::TAILS:
            targetPowerW = settings.manualRect.tailsPowerW;
            break;
        default:
            return 100;
    }

    // Конвертируем в проценты
    uint8_t powerPercent = static_cast<uint8_t>((targetPowerW / maxPowerW) * 100.0f);
    if (powerPercent > 100) powerPercent = 100;
    if (powerPercent < 10) powerPercent = 10;

    return powerPercent;
}

float getDefaultTakeoffSpeedMlH(const Settings& settings, RectPhase phase) {
    // Используем новые настройки скорости из ManualRectSettings
    switch (phase) {
        case RectPhase::HEADS:
            return settings.manualRect.headsSpeedMlH;
        case RectPhase::BODY:
            return settings.manualRect.bodySpeedMlH;
        case RectPhase::TAILS:
            // Для хвостов используем старую логику из rectParams
            return getRectificationDirectTakeoffSpeedMlH(settings, phase);
        default:
            return 0.0f;
    }
}

RectTakeoffFraction getTakeoffFractionForPhase(RectPhase phase) {
    switch (phase) {
        case RectPhase::HEADS:
            return RectTakeoffFraction::HEADS;
        case RectPhase::BODY:
            return RectTakeoffFraction::BODY;
        case RectPhase::TAILS:
            return RectTakeoffFraction::TAILS;
        default:
            return RectTakeoffFraction::NONE;
    }
}

bool isTakeoffWindowOpen(const RectParams& params, uint32_t elapsedMs,
                         uint32_t* cycleMsOut = nullptr,
                         uint32_t* openMsOut = nullptr) {
    if (params.refluxMode == RectRefluxMode::ML_H) {
        if (cycleMsOut) {
            *cycleMsOut = 0;
        }
        if (openMsOut) {
            *openMsOut = 0;
        }
        return true;
    }

    if (params.refluxMode == RectRefluxMode::SR_RATIO) {
        const uint32_t cycleMs = RECT_SR_CONTROL_CYCLE_SEC * 1000UL;
        if (params.srRatio <= 0.0f) {
            if (cycleMsOut) {
                *cycleMsOut = cycleMs;
            }
            if (openMsOut) {
                *openMsOut = 0;
            }
            return false;
        }

        uint32_t openMs = static_cast<uint32_t>(
            static_cast<float>(cycleMs) / (params.srRatio + 1.0f));
        if (openMs == 0) {
            openMs = 1;
        }
        if (cycleMsOut) {
            *cycleMsOut = cycleMs;
        }
        if (openMsOut) {
            *openMsOut = openMs;
        }
        return (elapsedMs % cycleMs) < openMs;
    }

    const uint32_t cycleSec =
        params.autonomousCycleSec > 0 ? params.autonomousCycleSec : 1;
    const uint32_t pauseSec =
        params.autonomousPauseSec < cycleSec ? params.autonomousPauseSec
                                             : (cycleSec - 1);
    const uint32_t cycleMs = cycleSec * 1000UL;
    const uint32_t openMs = (cycleSec - pauseSec) * 1000UL;
    if (cycleMsOut) {
        *cycleMsOut = cycleMs;
    }
    if (openMsOut) {
        *openMsOut = openMs;
    }
    return (elapsedMs % cycleMs) < openMs;
}

RectTakeoffCommand buildManualTakeoffCommand(const Settings& settings,
                                             RectPhase phase,
                                             uint32_t elapsedMs) {
    RectTakeoffCommand command;
    command.backendType = settings.rectParams.takeoffBackendType;
    command.fraction = getTakeoffFractionForPhase(phase);
    command.equivalentRateMlH =
        manualTakeoffRateMlH > 0.0f ? manualTakeoffRateMlH : 0.0f;
    command.enabled =
        command.fraction != RectTakeoffFraction::NONE &&
        command.equivalentRateMlH > 0.0f;
    command.fullReflux = !command.enabled;
    command.periodicTakeoff =
        settings.rectParams.refluxMode != RectRefluxMode::ML_H;
    command.periodicTakeoffActive =
        !command.periodicTakeoff ||
        isTakeoffWindowOpen(settings.rectParams, elapsedMs,
                            &command.periodicCycleMs,
                            &command.periodicOpenMs);
    return command;
}

float getCurrentTakeoffTotalVolumeMl(const SystemState& state,
                                     const Settings& settings) {
    if (settings.rectParams.takeoffBackendType == RectTakeoffBackendType::PUMP) {
        return state.pump.totalVolumeMl;
    }
    return RectTakeoff::getFeedback().sessionVolumeMl;
}

ControlV2::ReasonCodeV2 getPhaseTransitionReason(RectPhase fromPhase,
                                                 RectPhase toPhase) {
    if (toPhase == RectPhase::HEATING && fromPhase == RectPhase::IDLE) {
        return ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST;
    }
    if (toPhase == RectPhase::IDLE) {
        return ControlV2::ReasonCodeV2::RC_MANUAL_OPERATOR_STOP;
    }
    return ControlV2::ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH;
}

const char* getPhaseTransitionMessage(RectPhase fromPhase, RectPhase toPhase) {
    if (toPhase == RectPhase::HEATING && fromPhase == RectPhase::IDLE) {
        return "Manual rectification started";
    }
    switch (toPhase) {
        case RectPhase::HEADS:
            return "Manual switch to heads";
        case RectPhase::BODY:
            return "Manual switch to body";
        case RectPhase::TAILS:
            return "Manual switch to tails";
        case RectPhase::FINISH:
            return "Manual switch to finish";
        case RectPhase::IDLE:
            return "Manual rectification stopped by operator";
        default:
            return "Manual rectification phase switched";
    }
}

} // namespace

void update(SystemState& state, const Settings& settings) {
    PowerCommandGateway::setHeaterBoosterEnabled(
        PowerCommandGateway::Owner::FSM,
        PowerCommandGateway::Reason::FSM_CONTROL, false);
    if (state.temps.valid[TEMP_CUBE] &&
        state.temps.cube >= getWaterAutoStartTempC(settings) &&
        !Valves::getWater()) {
        PowerCommandGateway::setValve(PowerCommandGateway::Owner::FSM,
            PowerCommandGateway::Reason::FSM_CONTROL,
            PowerCommandGateway::Valve::WATER, true);
    }

    const uint32_t now = millis();
    const ControlV2::ManualRectFloodPolicyV2 floodPolicy =
        ControlV2::SafetyPolicyV2::evaluateManualRectFloodPower(
            Heater::getPower(), state, settings, now, lastFloodTime);

    if (floodPolicy.stepdownRecommended) {
        lastFloodTime = now;
        PowerCommandGateway::setHeaterPower(
            PowerCommandGateway::Owner::FSM,
            PowerCommandGateway::Reason::FSM_CONTROL,
            floodPolicy.appliedPowerPercent);
        MQTT::publishNotification(
            "Р—Р°С…Р»С‘Р±!",
            "Р”Р°РІР»РµРЅРёРµ РєСЂРёС‚РёС‡РµСЃРєРѕРµ! РњРѕС‰РЅРѕСЃС‚СЊ РўР­РќР° СЃРЅРёР¶РµРЅР°.",
            "warning");
    }

    const uint32_t elapsedMs = now - getPhaseStartTime();
    switch (state.rectPhase) {
        case RectPhase::HEADS:
        case RectPhase::BODY:
        case RectPhase::TAILS:
            applyProcessHeaterPower(state, settings,
                                    getManualPhasePowerPercent(settings, state.rectPhase));
            RectTakeoff::apply(
                buildManualTakeoffCommand(settings, state.rectPhase, elapsedMs));
            break;
        default:
            RectTakeoff::stop();
            break;
    }

    float collected =
        getCurrentTakeoffTotalVolumeMl(state, settings) - getPhaseStartVolumeMl();
    if (collected < 0.0f) {
        collected = 0.0f;
    }

    // Проверка автоматической смены фракций по объему
    bool shouldChangeFraction = false;
    RectPhase nextPhase = state.rectPhase;

    switch (state.rectPhase) {
        case RectPhase::HEADS:
            state.stats.headsVolume = collected;
            // Проверяем достижение целевого объема голов
            if (settings.manualRect.headsTargetVolumeMl > 0.0f &&
                collected >= settings.manualRect.headsTargetVolumeMl) {
                shouldChangeFraction = true;
                nextPhase = RectPhase::BODY;
            }
            break;
        case RectPhase::BODY:
            state.stats.bodyVolume = collected;
            // Проверяем достижение целевого объема тела
            if (settings.manualRect.bodyTargetVolumeMl > 0.0f &&
                collected >= settings.manualRect.bodyTargetVolumeMl) {
                shouldChangeFraction = true;
                nextPhase = RectPhase::TAILS;
            }
            break;
        case RectPhase::TAILS:
            state.stats.tailsVolume = collected;
            // Проверяем достижение целевого объема хвостов
            if (settings.manualRect.tailsTargetVolumeMl > 0.0f &&
                collected >= settings.manualRect.tailsTargetVolumeMl) {
                shouldChangeFraction = true;
                nextPhase = RectPhase::FINISH;
            }
            break;
        default:
            break;
    }

    // Если достигнут целевой объем - инициируем смену тары
    if (shouldChangeFraction && !state.manualRectContainerChangePending) {
        // Останавливаем отбор
        RectTakeoff::stop();
        PowerCommandGateway::setHeaterPower(
            PowerCommandGateway::Owner::FSM,
            PowerCommandGateway::Reason::FSM_CONTROL, 0);

        // Устанавливаем флаг ожидания смены тары
        state.manualRectContainerChangePending = true;
        state.manualRectPendingPhase = nextPhase;

        // Звуковой сигнал (если включен)
        if (settings.manualRect.fractionChangeSoundEnabled && settings.soundEnabled) {
            // Звуковой сигнал будет обработан в веб-интерфейсе через состояние
            Logger::logf(0, "Manual rect: fraction change signal, %s -> %s",
                         getPhaseName(state.rectPhase),
                         getPhaseName(nextPhase));
        }

        MQTT::publishNotification(
            "Смена фракции",
            "Отбор остановлен. Смените тару и подтвердите продолжение.",
            "info");
    }
}

void setPhase(SystemState& state, RectPhase phase) {
    const RectPhase previousPhase = state.rectPhase;
    if (state.mode == Mode::MANUAL_RECT && previousPhase != phase) {
        ControlV2::notePhaseTransition(
            Mode::MANUAL_RECT, static_cast<uint16_t>(previousPhase),
            static_cast<uint16_t>(phase),
            getPhaseTransitionReason(previousPhase, phase),
            getPhaseTransitionMessage(previousPhase, phase));
    }

    state.rectPhase = phase;
    if (phase == RectPhase::HEADS || phase == RectPhase::BODY ||
        phase == RectPhase::TAILS) {
        manualTakeoffRateMlH = getDefaultTakeoffSpeedMlH(g_settings, phase);
    } else {
        manualTakeoffRateMlH = 0.0f;
    }
    setPhaseStartVolumeMl(getCurrentTakeoffTotalVolumeMl(state, g_settings));
    setPhaseStartTime(millis());
    alertSent = false;

    if (phase == RectPhase::HEATING) {
        applyFullHeatPower(g_settings);
    } else if (phase == RectPhase::HEADS || phase == RectPhase::BODY ||
               phase == RectPhase::TAILS || phase == RectPhase::STABILIZATION) {
        applyProcessHeaterPower(state, g_settings,
                                getManualPhasePowerPercent(g_settings, phase));
    }

    const char* phaseName = "Unknown";
    switch (phase) {
        case RectPhase::IDLE:
            phaseName = "РћР¶РёРґР°РЅРёРµ";
            break;
        case RectPhase::HEATING:
            phaseName = "РќР°РіСЂРµРІ";
            break;
        case RectPhase::STABILIZATION:
            phaseName = "РЎС‚Р°Р±РёР»РёР·Р°С†РёСЏ";
            break;
        case RectPhase::HEADS:
            phaseName = "Р“РѕР»РѕРІС‹";
            break;
        case RectPhase::BODY:
            phaseName = "РўРµР»Рѕ";
            break;
        case RectPhase::TAILS:
            phaseName = "РҐРІРѕСЃС‚С‹";
            break;
        case RectPhase::FINISH:
            phaseName = "Р—Р°РІРµСЂС€РµРЅРѕ";
            break;
        default:
            break;
    }

    LOG_I("ManualRect: phase changed to %s", phaseName);
    Logger::logf(0, "ManualRect: Р¤Р°Р·Р° РёР·РјРµРЅРµРЅР° РЅР° %s", phaseName);
}

void setTakeoffRateMlH(float speedMlH) {
    manualTakeoffRateMlH = speedMlH > 0.0f ? speedMlH : 0.0f;
}

bool confirmContainerChange(SystemState& state, const Settings& settings) {
    if (!state.manualRectContainerChangePending) {
        return false;
    }

    // Сбрасываем флаг ожидания смены тары
    state.manualRectContainerChangePending = false;

    // Переходим к следующей фазе
    const RectPhase nextPhase = state.manualRectPendingPhase;
    state.manualRectPendingPhase = RectPhase::IDLE;

    if (nextPhase != RectPhase::IDLE) {
        setPhase(state, nextPhase);
        Logger::logf(0, "Manual rect: container changed, starting %s",
                     getPhaseName(nextPhase));
        MQTT::publishNotification(
            "Продолжение отбора",
            "Смена тары подтверждена. Отбор возобновлён.",
            "info");
    }

    return true;
}

} // namespace ManualRect
} // namespace FSM
