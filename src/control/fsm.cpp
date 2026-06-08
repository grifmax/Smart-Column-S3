/**
 * Smart-Column S3 - Конечный автомат (FSM)
 *
 * Управление фазами авто-ректификации и оркестрация режимов
 */

#include "fsm.h"
#include "fsm_utils.h"
#include "v2/status_adapter.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"
#include "../drivers/stirrer.h"
#include "../drivers/valves.h"
#include "../interface/mqtt.h"
#include "../interface/localization.h"
#include "../storage/logger.h"

namespace FSM {

static uint32_t pauseStartTime = 0;

namespace {

ControlV2::ReasonCodeV2 getSafetyAbortReason(const SystemState& state) {
    switch (state.currentAlarm.type) {
        case AlarmType::COLUMN_FLOOD:
        case AlarmType::PRESSURE_RISE_RATE:
            return ControlV2::ReasonCodeV2::RC_SAFETY_TRIP_PRESSURE;
        case AlarmType::SENSOR_FAILURE:
            return ControlV2::ReasonCodeV2::RC_SAFETY_TRIP_SENSOR;
        case AlarmType::POWER_FAILURE:
            return ControlV2::ReasonCodeV2::RC_SAFETY_TRIP_POWER;
        case AlarmType::VAPOR_BREAKTHROUGH:
        case AlarmType::WATER_OVERHEAT:
        case AlarmType::WATER_RISE_RATE:
        case AlarmType::OVERHEAT:
        case AlarmType::LOW_WATER:
        case AlarmType::EMERGENCY_STOP:
            return ControlV2::ReasonCodeV2::RC_SAFETY_TRIP_OVERHEAT;
        default:
            return ControlV2::ReasonCodeV2::RC_SAFETY_TRIP_GENERIC;
    }
}

const char* getOperatorStopMessage(Mode mode) {
    switch (mode) {
        case Mode::RECTIFICATION: return "Rectification stopped by operator";
        case Mode::DISTILLATION: return "Distillation stopped by operator";
        case Mode::MANUAL_RECT: return "Manual rectification stopped by operator";
        case Mode::MASHING: return "Mashing stopped by operator";
        case Mode::HOLD: return "Hold program stopped by operator";
        case Mode::NBK: return "NBK stopped by operator";
        case Mode::FERMENTATION: return "Fermentation stopped by operator";
        case Mode::IDLE:
        default:
            return "Process stopped by operator";
    }
}

void noteModeExitTransition(const SystemState& state,
                            ControlV2::ReasonCodeV2 reasonCode,
                            const char* operatorMessage) {
    switch (state.mode) {
        case Mode::RECTIFICATION:
            if (state.rectPhase != RectPhase::IDLE) {
                ControlV2::notePhaseTransition(
                    Mode::RECTIFICATION, static_cast<uint16_t>(state.rectPhase),
                    static_cast<uint16_t>(RectPhase::IDLE), reasonCode,
                    operatorMessage);
            }
            break;
        case Mode::DISTILLATION:
            if (state.rectPhase != RectPhase::IDLE) {
                ControlV2::notePhaseTransition(
                    Mode::DISTILLATION, static_cast<uint16_t>(state.rectPhase),
                    static_cast<uint16_t>(RectPhase::IDLE), reasonCode,
                    operatorMessage);
            }
            break;
        case Mode::MANUAL_RECT:
            if (state.rectPhase != RectPhase::IDLE) {
                ControlV2::notePhaseTransition(
                    Mode::MANUAL_RECT, static_cast<uint16_t>(state.rectPhase),
                    static_cast<uint16_t>(RectPhase::IDLE), reasonCode,
                    operatorMessage);
            }
            break;
        case Mode::MASHING:
            if (state.mashing.phase != MashPhase::IDLE) {
                ControlV2::notePhaseTransition(
                    Mode::MASHING, static_cast<uint16_t>(state.mashing.phase),
                    static_cast<uint16_t>(MashPhase::IDLE), reasonCode,
                    operatorMessage);
            }
            break;
        case Mode::HOLD:
            if (state.hold.active) {
                ControlV2::notePhaseTransition(
                    Mode::HOLD, static_cast<uint16_t>(state.hold.currentStep),
                    ControlV2::kNoPhaseIdV2, reasonCode, operatorMessage);
            }
            break;
        case Mode::NBK:
            if (state.nbkPhase != NbkPhase::IDLE) {
                ControlV2::notePhaseTransition(
                    Mode::NBK, static_cast<uint16_t>(state.nbkPhase),
                    static_cast<uint16_t>(NbkPhase::IDLE), reasonCode,
                    operatorMessage);
            }
            break;
        case Mode::FERMENTATION:
            if (state.fermPhase != FermentationPhase::IDLE) {
                ControlV2::notePhaseTransition(
                    Mode::FERMENTATION, static_cast<uint16_t>(state.fermPhase),
                    static_cast<uint16_t>(FermentationPhase::IDLE), reasonCode,
                    operatorMessage);
            }
            break;
        case Mode::IDLE:
        default:
            break;
    }
}

void finalizeModeStop(SystemState& state) {
    Heater::setPower(0);
    Pump::stop();
    Stirrer::stop(); // остановить мешалку при выходе из любого режима
    Valves::closeAll();

    state.mode = Mode::IDLE;
    state.rectPhase = RectPhase::IDLE;
    state.nbkPhase = NbkPhase::IDLE;
    state.fermPhase = FermentationPhase::IDLE;
    state.mashing.active = false;
    state.hold.active = false;
    state.paused = false;
    state.stirrer.autoMode = false;
    pauseStartTime = 0;
}

} // namespace

void update(SystemState& state, const Settings& settings) {
    if (!state.paused) {
        switch (state.mode) {
            case Mode::RECTIFICATION:
                Rectification::update(state, settings);
                break;
            case Mode::DISTILLATION:
                Distillation::update(state, settings);
                break;
            case Mode::MASHING:
                Mashing::update(state, settings);
                break;
            case Mode::HOLD:
                Hold::update(state, settings);
                break;
            case Mode::MANUAL_RECT:
                ManualRect::update(state, settings);
                break;
            case Mode::NBK:
                Nbk::update(state, settings);
                break;
            case Mode::FERMENTATION:
                Fermentation::update(state, settings);
                break;
            default:
                break;
        }
    }

}

void startMode(SystemState& state, const Settings& settings, Mode mode) {
    LOG_I("FSM: Starting mode %s", getModeName(mode));
    
    // Сброс предыдущего состояния
    Heater::setPower(0);
    Pump::stop();
    Valves::closeAll();
    
    state.mode = mode;
    state.paused = false;
    Pump::resetVolume();
    state.stats = ProcessStats{};

    uint32_t now = millis();
    setPhaseStartTime(now);
    pauseStartTime = 0;

    switch (mode) {
        case Mode::RECTIFICATION:
            Rectification::initSession(state, settings);
            state.rectPhase = RectPhase::HEATING;
            ControlV2::notePhaseTransition(
                Mode::RECTIFICATION, static_cast<uint16_t>(RectPhase::IDLE),
                static_cast<uint16_t>(RectPhase::HEATING),
                ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
                "Rectification started");
            MQTT::publishNotification("Процесс запущен", "Начат процесс ректификации", "info");
            break;
        case Mode::DISTILLATION:
            state.rectPhase = RectPhase::HEATING;
            ControlV2::notePhaseTransition(
                Mode::DISTILLATION, static_cast<uint16_t>(RectPhase::IDLE),
                static_cast<uint16_t>(RectPhase::HEATING),
                ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
                "Distillation started");
            break;
        case Mode::MANUAL_RECT:
            ManualRect::setPhase(state, RectPhase::HEATING);
            break;
        case Mode::MASHING:
            state.mashing.active = true;
            // Авто-запуск мешалки при затирании
            if (settings.stirrer.enabled && settings.stirrer.autoMashing) {
                state.stirrer.autoMode = true;
                Stirrer::start(settings.stirrer.defaultSpeedPercent);
                LOG_I("FSM: Stirrer auto-start for MASHING at %u%%", settings.stirrer.defaultSpeedPercent);
            }
            break;
        case Mode::HOLD:
            state.hold.active = true;
            break;
        case Mode::NBK:
            state.nbkPhase = NbkPhase::HEATING;
            ControlV2::notePhaseTransition(
                Mode::NBK, static_cast<uint16_t>(NbkPhase::IDLE),
                static_cast<uint16_t>(NbkPhase::HEATING),
                ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
                "NBK started");
            // Авто-запуск мешалки при НБК
            if (settings.stirrer.enabled && settings.stirrer.autoNbk) {
                state.stirrer.autoMode = true;
                Stirrer::start(settings.stirrer.defaultSpeedPercent);
            }
            break;
        case Mode::FERMENTATION:
            state.fermPhase = FermentationPhase::RUNNING;
            ControlV2::notePhaseTransition(
                Mode::FERMENTATION,
                static_cast<uint16_t>(FermentationPhase::IDLE),
                static_cast<uint16_t>(FermentationPhase::RUNNING),
                ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
                "Fermentation started");
            // Авто-запуск мешалки при ферментации
            if (settings.stirrer.enabled && settings.stirrer.autoFermentation) {
                state.stirrer.autoMode = true;
                Stirrer::start(settings.stirrer.defaultSpeedPercent);
            }
            break;
        default: break;
    }
    
    Logger::logf(0, "Mode started: %s", getModeName(mode));
}

void stopMode(SystemState& state) {
    if (state.mode == Mode::IDLE) return;

    LOG_I("FSM: Stopping %s", getModeName(state.mode));

    if (state.mode == Mode::MANUAL_RECT) {
        noteModeExitTransition(
            state, ControlV2::ReasonCodeV2::RC_MANUAL_OPERATOR_STOP,
            getOperatorStopMessage(state.mode));
    } else {
        noteModeExitTransition(state, ControlV2::ReasonCodeV2::RC_MODE_STOP_REQUEST,
                               getOperatorStopMessage(state.mode));
    }

    finalizeModeStop(state);

    MQTT::publishNotification("Процесс остановлен", "Процесс остановлен пользователем", "warning");
    Logger::logf(0, "Mode stopped by user");
}

void abortMode(SystemState& state) {
    LOG_W("FSM: Aborting due to safety!");
    noteModeExitTransition(state, getSafetyAbortReason(state),
                           state.currentAlarm.message);
    finalizeModeStop(state);
    Logger::logf(2, "Mode aborted by safety");
}

void pause(SystemState& state) {
    if (state.paused) return;
    state.paused = true;
    pauseStartTime = millis();

    Heater::setPower(0);
    Pump::stop();
    // Охлаждение оставляем если куб горячий
    if (state.temps.cube < 45.0f) Valves::setWater(false);

    LOG_I("FSM: Paused");
}

void resume(SystemState& state) {
    if (!state.paused) return;
    if (pauseStartTime > 0) {
        uint32_t pausedMs = millis() - pauseStartTime;
        setPhaseStartTime(getPhaseStartTime() + pausedMs);
    }
    state.paused = false;
    pauseStartTime = 0;
    LOG_I("FSM: Resumed");
}

void nextFraction(SystemState& state, const Settings& settings) {
    if (state.mode == Mode::MANUAL_RECT) {
        if (state.rectPhase == RectPhase::HEADS) ManualRect::setPhase(state, RectPhase::BODY);
        else if (state.rectPhase == RectPhase::BODY) ManualRect::setPhase(state, RectPhase::TAILS);
        else if (state.rectPhase == RectPhase::TAILS) ManualRect::setPhase(state, RectPhase::FINISH);
    }
}

const char* getModeName(Mode mode) {
    switch (mode) {
        case Mode::IDLE: return msg(Msg::MODE_IDLE);
        case Mode::RECTIFICATION: return msg(Msg::MODE_RECTIFICATION);
        case Mode::DISTILLATION: return msg(Msg::MODE_DISTILLATION);
        case Mode::MANUAL_RECT: return msg(Msg::MODE_MANUAL_RECT);
        case Mode::MASHING: return msg(Msg::MODE_MASHING);
        case Mode::HOLD: return msg(Msg::MODE_HOLD);
        case Mode::NBK: return "НБК";
        case Mode::FERMENTATION: return "Ферментация";
        default: return "???";
    }
}

const char* getPhaseName(RectPhase phase) {
    switch (phase) {
        case RectPhase::IDLE: return msg(Msg::PHASE_IDLE);
        case RectPhase::HEATING: return msg(Msg::PHASE_HEATING);
        case RectPhase::STABILIZATION: return msg(Msg::PHASE_STABILIZATION);
        case RectPhase::HEADS: return msg(Msg::PHASE_HEADS);
        case RectPhase::BODY: return msg(Msg::PHASE_BODY);
        case RectPhase::TAILS: return msg(Msg::PHASE_TAILS);
        case RectPhase::FINISH: return msg(Msg::PHASE_FINISH);
        default: return "???";
    }
}

const char* getNbkPhaseName(NbkPhase phase) {
    switch (phase) {
        case NbkPhase::IDLE: return "Ожидание";
        case NbkPhase::HEATING: return "Разогрев";
        case NbkPhase::STABILIZATION: return "Старт";
        case NbkPhase::WORKING: return "Работа";
        case NbkPhase::FINISH: return "Финиш";
        default: return "???";
    }
}

const char* getFermPhaseName(FermentationPhase phase) {
    return phase == FermentationPhase::RUNNING ? "Работа" : "Ожидание";
}

void getRectTargetsMl(float& headsMl, float& bodyMl, float& tailsMl) {
    Rectification::getTargets(headsMl, bodyMl, tailsMl);
}

uint32_t getPhaseElapsedSec() {
    uint32_t start = getPhaseStartTime();
    if (start == 0) return 0;
    return (millis() - start) / 1000UL;
}

uint32_t getPhaseTargetSec(const SystemState& state, const Settings& settings) {
    if (state.mode == Mode::RECTIFICATION) {
        switch (state.rectPhase) {
            case RectPhase::STABILIZATION:
                return settings.rectParams.stabilizationMin * 60UL;
            default: return 0;
        }
    }
    // #4 fix: Hold и Mashing — длительность текущего шага
    if (state.mode == Mode::HOLD && state.hold.currentStep < state.hold.stepCount) {
        return (uint32_t)state.hold.steps[state.hold.currentStep].duration * 60UL;
    }
    if (state.mode == Mode::MASHING) {
        return state.mashing.stepDuration; // уже в секундах, задаётся в mashing_handler
    }
    // #4 fix: NBK стабилизация — 5 мин
    if (state.mode == Mode::NBK && state.nbkPhase == NbkPhase::STABILIZATION) {
        return 5 * 60UL;
    }
    // #4 fix: Ферментация — если задано время
    if (state.mode == Mode::FERMENTATION && settings.fermentation.durationHours > 0) {
        return (uint32_t)settings.fermentation.durationHours * 3600UL;
    }
    return 0;
}

uint8_t getPhaseProgressPercent(const SystemState& state, const Settings& settings) {
    if (state.mode == Mode::IDLE) return 0;

    if (state.mode == Mode::RECTIFICATION) {
        float headsTarget, bodyTarget, tailsTarget;
        Rectification::getTargets(headsTarget, bodyTarget, tailsTarget);

        switch (state.rectPhase) {
            case RectPhase::STABILIZATION: {
                uint32_t total = settings.rectParams.stabilizationMin * 60UL;
                if (total == 0) return 100;
                uint32_t elapsed = getPhaseElapsedSec();
                return (elapsed >= total) ? 100 : (uint8_t)(elapsed * 100 / total);
            }
            case RectPhase::HEADS: {
                if (headsTarget <= 0) return 100;
                float current = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
                return (current >= headsTarget) ? 100 : (uint8_t)(current * 100 / headsTarget);
            }
            case RectPhase::BODY: {
                if (bodyTarget <= 0) return 100;
                float current = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
                return (current >= bodyTarget) ? 100 : (uint8_t)(current * 100 / bodyTarget);
            }
            case RectPhase::TAILS: {
                if (tailsTarget <= 0) return 100;
                float current = state.pump.totalVolumeMl - getPhaseStartVolumeMl();
                return (current >= tailsTarget) ? 100 : (uint8_t)(current * 100 / tailsTarget);
            }
            default: return 0;
        }
    }
    
    // Для других режимов (дистилляция и т.д.)
    if (state.mode == Mode::DISTILLATION) {
        if (state.rectPhase == RectPhase::HEATING) {
            float start = 20.0f;
            float end = 85.0f;
            if (state.temps.cube <= start) return 0;
            if (state.temps.cube >= end) return 100;
            return (uint8_t)((state.temps.cube - start) * 100 / (end - start));
        }
    }

    // #4 fix: Mashing — прогресс текущего шага по выдержке при температуре
    if (state.mode == Mode::MASHING) {
        if (state.mashing.stepDuration == 0) return 0;
        uint32_t stepDurMs = state.mashing.stepDuration * 1000UL;
        if (state.mashing.tempInRange && state.mashing.inRangeStartTime > 0) {
            uint32_t elapsed = millis() - state.mashing.inRangeStartTime;
            if (elapsed >= stepDurMs) return 100;
            return (uint8_t)(elapsed * 100UL / stepDurMs);
        }
        // Температура ещё не достигнута — показываем прогресс нагрева
        float target = state.mashing.targetTemp;
        float current = state.temps.cube;
        float prev = (state.mashing.currentStep > 0) ? 20.0f : 20.0f;
        if (current >= target) return 0; // достигли, ждём входа в диапазон
        if (target <= prev) return 0;
        float p = (current - prev) / (target - prev);
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        return (uint8_t)(p * 50.0f); // max 50% пока не в диапазоне
    }

    // #4 fix: Hold — прогресс текущего шага
    if (state.mode == Mode::HOLD && state.hold.currentStep < state.hold.stepCount) {
        uint32_t stepDurSec = (uint32_t)state.hold.steps[state.hold.currentStep].duration * 60UL;
        if (stepDurSec == 0) return 0;
        uint32_t stepDurMs = stepDurSec * 1000UL;
        if (state.hold.tempInRange && state.hold.inRangeStartTime > 0) {
            uint32_t elapsed = millis() - state.hold.inRangeStartTime;
            if (elapsed >= stepDurMs) return 100;
            return (uint8_t)(elapsed * 100UL / stepDurMs);
        }
        return 0; // ещё не достигли температуры
    }

    // #4 fix: NBK — прогресс по фазам
    if (state.mode == Mode::NBK) {
        switch (state.nbkPhase) {
            case NbkPhase::HEATING: {
                // Прогресс нагрева куба 0→98°C
                float t = state.temps.cube;
                float tEnd = 98.0f;
                float tStart = 20.0f;
                if (t <= tStart) return 0;
                if (t >= tEnd) return 100;
                return (uint8_t)((t - tStart) * 100.0f / (tEnd - tStart));
            }
            case NbkPhase::STABILIZATION: {
                uint32_t elapsed = (millis() - getPhaseStartTime());
                uint32_t total = 5 * 60 * 1000UL;
                if (elapsed >= total) return 100;
                return (uint8_t)(elapsed * 100UL / total);
            }
            case NbkPhase::WORKING: {
                // Если задан целевой объём — по объёму, иначе не известно
                if (settings.nbk.targetVolumeMl > 0.1f) {
                    float v = state.pump.totalVolumeMl;
                    float target = settings.nbk.targetVolumeMl;
                    if (v >= target) return 100;
                    return (uint8_t)(v * 100.0f / target);
                }
                return 0; // объём не задан — прогресс неизвестен
            }
            default: return 0;
        }
    }

    // #4 fix: Fermentation — прогресс по заданному времени
    if (state.mode == Mode::FERMENTATION && settings.fermentation.durationHours > 0) {
        uint32_t totalMs = (uint32_t)settings.fermentation.durationHours * 3600UL * 1000UL;
        uint32_t elapsed = millis() - getPhaseStartTime();
        if (elapsed >= totalMs) return 100;
        return (uint8_t)(elapsed * 100UL / totalMs);
    }

    return 0;
}

void getDistillationParams(float& speedMlH, float& headsVolumeMl, float& targetVolumeMl,
                           float& endTempC, uint8_t& powerPercent) {
    // Реализация в distillation_handler.cpp
}

} // namespace FSM
