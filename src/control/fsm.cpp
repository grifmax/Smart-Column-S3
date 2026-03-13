/**
 * Smart-Column S3 - Конечный автомат (FSM)
 *
 * Управление фазами авто-ректификации и оркестрация режимов
 */

#include "fsm.h"
#include "fsm_utils.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"
#include "../drivers/valves.h"
#include "../interface/mqtt.h"
#include "../interface/localization.h"
#include "../storage/logger.h"

namespace FSM {

static uint32_t pauseStartTime = 0;

void update(SystemState& state, const Settings& settings) {
    if (state.paused) return;

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
            MQTT::publishNotification("Процесс запущен", "Начат процесс ректификации", "info");
            break;
        case Mode::DISTILLATION:
            state.rectPhase = RectPhase::HEATING;
            break;
        case Mode::MANUAL_RECT:
            state.rectPhase = RectPhase::HEATING;
            ManualRect::setPhase(state, RectPhase::HEATING);
            break;
        case Mode::MASHING:
            state.mashing.active = true;
            break;
        case Mode::HOLD:
            state.hold.active = true;
            break;
        case Mode::NBK:
            state.nbkPhase = NbkPhase::HEATING;
            break;
        case Mode::FERMENTATION:
            state.fermPhase = FermentationPhase::RUNNING;
            break;
        default: break;
    }
    
    Logger::logf(0, "Mode started: %s", getModeName(mode));
}

void stopMode(SystemState& state) {
    if (state.mode == Mode::IDLE) return;

    LOG_I("FSM: Stopping %s", getModeName(state.mode));
    
    Heater::setPower(0);
    Pump::stop();
    Valves::closeAll();

    state.mode = Mode::IDLE;
    state.rectPhase = RectPhase::IDLE;
    state.nbkPhase = NbkPhase::IDLE;
    state.fermPhase = FermentationPhase::IDLE;
    state.mashing.active = false;
    state.hold.active = false;
    state.paused = false;
    pauseStartTime = 0;

    MQTT::publishNotification("Процесс остановлен", "Процесс остановлен пользователем", "warning");
    Logger::logf(0, "Mode stopped by user");
}

void abortMode(SystemState& state) {
    LOG_W("FSM: Aborting due to safety!");
    stopMode(state);
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
            // Объемы пересчитать в секунды невозможно точно, возвращаем 0
            default: return 0;
        }
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
            float start = 20.0f; // Примерно
            float end = 85.0f;
            if (state.temps.cube <= start) return 0;
            if (state.temps.cube >= end) return 100;
            return (uint8_t)((state.temps.cube - start) * 100 / (end - start));
        }
    }

    return 0;
}

void getDistillationParams(float& speedMlH, float& headsVolumeMl, float& targetVolumeMl,
                           float& endTempC, uint8_t& powerPercent) {
    // Реализация в distillation_handler.cpp
}

} // namespace FSM
