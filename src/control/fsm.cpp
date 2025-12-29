/**
 * Smart-Column S3 - Конечный автомат (FSM)
 *
 * Управление фазами авто-ректификации
 */

#include "fsm.h"
#include "../drivers/heater.h"
#include "../drivers/pump.h"
#include "../drivers/valves.h"
#include "../drivers/sensors.h"
#include "../interface/mqtt.h"

namespace FSM {

static uint32_t phaseStartTime = 0;

void update(SystemState& state, const Settings& settings) {
    uint32_t now = millis();
    
    // Обработка разных режимов
    switch (state.mode) {
        case Mode::RECTIFICATION:
            // Обработка ректификации (существующий код)
            {
                uint32_t elapsed = now - phaseStartTime;

    switch (state.rectPhase) {
        case RectPhase::IDLE:
            // Ожидание старта
            break;

        case RectPhase::HEATING:
            // Разгон: ТЭН 100%, вода вкл при T_cube > 45°C
            Heater::setPower(100);

            if (state.temps.cube > 45.0f) {
                Valves::setWater(true);
            }

            // Переход к стабилизации когда T стабильна
            if (state.temps.valid[TEMP_COLUMN_BOTTOM] &&
                state.temps.columnBottom > 78.0f) {
                LOG_I("FSM: HEATING → STABILIZATION");
                state.rectPhase = RectPhase::STABILIZATION;
                phaseStartTime = now;

                // Отправка уведомления
                MQTT::publishNotification(
                    "Фаза: Стабилизация",
                    "Разогрев завершён, начата стабилизация колонны",
                    "info"
                );
            }
            break;

        case RectPhase::STABILIZATION:
            // Работа "на себя" 20 минут
            if (elapsed > settings.rectParams.stabilizationMin * 60 * 1000UL) {
                LOG_I("FSM: STABILIZATION → HEADS");
                state.rectPhase = RectPhase::HEADS;
                phaseStartTime = now;

                // Отправка уведомления
                MQTT::publishNotification(
                    "Фаза: Отбор голов",
                    "Стабилизация завершена, начат отбор голов",
                    "info"
                );
            }
            break;

        case RectPhase::HEADS: {
            // Отбор голов
            float headsSpeed = settings.rectParams.headsSpeedMlHKw *
                               (settings.equipment.heaterPowerW / 1000.0f);
            Pump::start(headsSpeed);
            Valves::setHeads(true);

            // TODO: Проверка завершения по объёму или T_column
            break;
        }

        case RectPhase::PURGE:
            // Продувка
            Pump::stop();
            Valves::closeAll();

            if (elapsed > settings.rectParams.purgeMin * 60 * 1000UL) {
                LOG_I("FSM: PURGE → BODY");
                state.rectPhase = RectPhase::BODY;
                phaseStartTime = now;

                // Отправка уведомления
                MQTT::publishNotification(
                    "Фаза: Отбор тела",
                    "Продувка завершена, начат отбор тела (основной продукт)",
                    "success"
                );
            }
            break;

        case RectPhase::BODY:
            // Отбор тела с Smart Decrement
            // TODO: Реализовать логику Smart Decrement
            break;

        case RectPhase::TAILS:
            // Отбор хвостов
            break;

        case RectPhase::FINISH:
            // Завершение
            Heater::setPower(0);
            Pump::stop();
            Valves::setWater(true); // Охлаждение еще 5 мин

            if (elapsed > 5 * 60 * 1000UL) {
                Valves::closeAll();
                state.rectPhase = RectPhase::IDLE;
                state.mode = Mode::IDLE;
                LOG_I("FSM: Process complete!");

                // Отправка уведомления о завершении
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "Процесс завершён! Собрано: %.0f мл",
                         state.pump.totalVolumeMl);
                MQTT::publishNotification(
                    "Процесс завершён",
                    msg,
                    "success"
                );
            }
            break;

        default:
            break;
    }
            }
            break;
            
        case Mode::MASHING:
            Mashing::update(state, settings);
            break;
            
        case Mode::HOLD:
            Hold::update(state, settings);
            break;
            
        case Mode::DISTILLATION:
            Distillation::update(state, settings);
            break;
            
        case Mode::MANUAL_RECT:
            ManualRect::update(state, settings);
            break;
            
        default:
            break;
    }
}

void startMode(SystemState& state, const Settings& settings, Mode mode) {
    LOG_I("FSM: Starting mode %d", static_cast<int>(mode));

    state.mode = mode;
    state.paused = false;

    if (mode == Mode::RECTIFICATION) {
        state.rectPhase = RectPhase::HEATING;
        phaseStartTime = millis();

        // Отправка уведомления о старте
        MQTT::publishNotification(
            "Процесс запущен",
            "Начат процесс ректификации - фаза разогрева",
            "info"
        );
    } else if (mode == Mode::DISTILLATION) {
        // TODO: Инициализация дистилляции
        LOG_I("FSM: Distillation mode started");
    } else if (mode == Mode::MANUAL_RECT) {
        // TODO: Инициализация ручной ректификации
        LOG_I("FSM: Manual rectification mode started");
    } else if (mode == Mode::MASHING) {
        state.mashing.phase = MashPhase::IDLE;
        state.mashing.currentStep = 0;
        state.mashing.active = true;
        LOG_I("FSM: Mashing mode started");
    } else if (mode == Mode::HOLD) {
        state.hold.active = true;
        state.hold.currentStep = 0;
        state.hold.stepStartTime = now;
        LOG_I("FSM: Hold mode started");
    }
}

void stopMode(SystemState& state) {
    LOG_I("FSM: Stopping");

    Heater::setPower(0);
    Pump::stop();
    Valves::closeAll();

    state.mode = Mode::IDLE;
    state.rectPhase = RectPhase::IDLE;
    state.mashing.phase = MashPhase::IDLE;
    state.mashing.active = false;
    state.hold.active = false;
    state.paused = false;

    // Отправка уведомления об остановке
    MQTT::publishNotification(
        "Процесс остановлен",
        "Процесс остановлен пользователем",
        "warning"
    );
}

void pause(SystemState& state) {
    state.paused = true;
    LOG_I("FSM: Paused");
}

void resume(SystemState& state) {
    state.paused = false;
    LOG_I("FSM: Resumed");
}

// =============================================================================
// ЗАТИРКА СОЛОДА
// =============================================================================

namespace Mashing {

static const MashProfile* currentProfile = nullptr;

void setProfile(const MashProfile* profile) {
    currentProfile = profile;
    LOG_I("Mashing: Profile set to %s", profile ? profile->name : "NULL");
}

void update(SystemState& state, const Settings& settings) {
    if (!state.mashing.active || !currentProfile) {
        return;
    }
    
    uint32_t now = millis();
    float currentTemp = state.temps.cube; // Используем температуру куба для затирки
    
    // Простая логика PID для поддержания температуры
    float targetTemp = state.mashing.targetTemp;
    float error = targetTemp - currentTemp;
    
    // Пропорциональное управление мощностью
    // Kp = 2 означает, что при ошибке 1°C мощность изменится на 2%
    float Kp = 2.0f;
    int8_t powerDelta = (int8_t)(error * Kp);
    uint8_t currentPower = Heater::getPower();
    uint8_t newPower = currentPower + powerDelta;
    
    // Ограничить диапазон 0-100%
    if (newPower > 100) newPower = 100;
    if (newPower < 0) newPower = 0;
    
    Heater::setPower(newPower);
    
    // Проверка завершения текущей паузы
    if (state.mashing.currentStep < currentProfile->stepCount) {
        uint32_t stepElapsed = (now - state.mashing.stepStartTime) / 1000; // секунды
        uint32_t stepDuration = state.mashing.stepDuration;
        
        // Проверка: достигнута ли температура и прошло ли время
        bool tempReached = fabs(currentTemp - targetTemp) < 1.0f; // Допуск ±1°C
        bool timeElapsed = stepElapsed >= stepDuration;
        
        if (tempReached && timeElapsed) {
            // Переход к следующей паузе
            nextStep(state);
        }
    } else {
        // Все паузы завершены
        state.mashing.phase = MashPhase::FINISH;
        state.mode = Mode::IDLE;
        Heater::setPower(0);
        LOG_I("Mashing: Process complete!");
        
        MQTT::publishNotification(
            "Затирка завершена",
            "Все температурные паузы завершены",
            "success"
        );
    }
}

void nextStep(SystemState& state) {
    if (!currentProfile) return;
    
    state.mashing.currentStep++;
    
    if (state.mashing.currentStep < currentProfile->stepCount) {
        state.mashing.targetTemp = currentProfile->steps[state.mashing.currentStep].temperature;
        state.mashing.stepDuration = currentProfile->steps[state.mashing.currentStep].duration * 60; // минуты -> секунды
        state.mashing.stepStartTime = millis();
        
        LOG_I("Mashing: Step %d/%d - Target %.1f°C, Duration %d min",
              state.mashing.currentStep + 1,
              currentProfile->stepCount,
              state.mashing.targetTemp,
              currentProfile->steps[state.mashing.currentStep].duration);
        
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "Пауза %d/%d: %.1f°C (%d мин)",
                 state.mashing.currentStep + 1,
                 currentProfile->stepCount,
                 state.mashing.targetTemp,
                 currentProfile->steps[state.mashing.currentStep].duration);
        
        MQTT::publishNotification("Затирка: Новая пауза", msg, "info");
    }
}

void start(SystemState& state, const MashProfile* profile) {
    if (!profile || profile->stepCount == 0) {
        LOG_E("Mashing: Invalid profile!");
        return;
    }
    
    setProfile(profile);
    state.mashing.active = true;
    state.mashing.currentStep = 0;
    state.mashing.targetTemp = profile->steps[0].temperature;
    state.mashing.stepDuration = profile->steps[0].duration * 60; // минуты -> секунды
    state.mashing.stepStartTime = millis();
    state.mode = Mode::MASHING;
    
    LOG_I("Mashing: Started profile '%s' (%d steps)", profile->name, profile->stepCount);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Начата затирка: %s", profile->name);
    MQTT::publishNotification("Затирка запущена", msg, "info");
}

} // namespace Mashing

// =============================================================================
// HOLD РЕЖИМ (температурные ступени)
// =============================================================================

namespace Hold {

void setSteps(const TempStep* steps, uint8_t count) {
    if (!steps || count == 0 || count > 10) {
        LOG_E("Hold: Invalid steps!");
        return;
    }
    
    // Сохранить шаги в состоянии (нужно передавать через state)
    LOG_I("Hold: Steps set (%d steps)", count);
}

void update(SystemState& state, const Settings& settings) {
    if (!state.hold.active || state.hold.stepCount == 0) {
        return;
    }
    
    uint32_t now = millis();
    float currentTemp = state.temps.cube; // Используем температуру куба
    
    if (state.hold.currentStep < state.hold.stepCount) {
        float targetTemp = state.hold.targetTemp;
        float error = targetTemp - currentTemp;
        
        // Пропорциональное управление мощностью
        float Kp = 2.0f;
        int8_t powerDelta = (int8_t)(error * Kp);
        uint8_t currentPower = Heater::getPower();
        uint8_t newPower = currentPower + powerDelta;
        
        // Ограничить диапазон 0-100%
        if (newPower > 100) newPower = 100;
        if (newPower < 0) newPower = 0;
        
        Heater::setPower(newPower);
        
        // Проверка завершения текущего шага
        uint32_t stepElapsed = (now - state.hold.stepStartTime) / 1000; // секунды
        uint32_t stepDuration = state.hold.steps[state.hold.currentStep].duration * 60; // минуты -> секунды
        
        bool tempReached = fabs(currentTemp - targetTemp) < 1.0f; // Допуск ±1°C
        bool timeElapsed = stepElapsed >= stepDuration;
        
        if (tempReached && timeElapsed) {
            // Переход к следующему шагу
            state.hold.currentStep++;
            
            if (state.hold.currentStep < state.hold.stepCount) {
                state.hold.targetTemp = state.hold.steps[state.hold.currentStep].temperature;
                state.hold.stepStartTime = now;
                
                LOG_I("Hold: Step %d/%d - Target %.1f°C, Duration %d min",
                      state.hold.currentStep + 1,
                      state.hold.stepCount,
                      state.hold.targetTemp,
                      state.hold.steps[state.hold.currentStep].duration);
                
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "Шаг %d/%d: %.1f°C (%d мин)",
                         state.hold.currentStep + 1,
                         state.hold.stepCount,
                         state.hold.targetTemp,
                         state.hold.steps[state.hold.currentStep].duration);
                
                MQTT::publishNotification("Hold: Новый шаг", msg, "info");
            } else {
                // Все шаги завершены
                state.hold.active = false;
                state.mode = Mode::IDLE;
                Heater::setPower(0);
                LOG_I("Hold: Process complete!");
                
                MQTT::publishNotification(
                    "Hold режим завершён",
                    "Все температурные ступени завершены",
                    "success"
                );
            }
        }
    }
}

void start(SystemState& state, const TempStep* steps, uint8_t count) {
    if (!steps || count == 0 || count > 10) {
        LOG_E("Hold: Invalid steps!");
        return;
    }
    
    // Копировать шаги в состояние
    for (uint8_t i = 0; i < count; i++) {
        state.hold.steps[i] = steps[i];
    }
    
    state.hold.stepCount = count;
    state.hold.currentStep = 0;
    state.hold.targetTemp = steps[0].temperature;
    state.hold.stepStartTime = millis();
    state.hold.active = true;
    state.mode = Mode::HOLD;
    
    LOG_I("Hold: Started with %d steps", count);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Начат Hold режим: %d температурных ступеней", count);
    MQTT::publishNotification("Hold запущен", msg, "info");
}

} // namespace Hold

// =============================================================================
// ДИСТИЛЛЯЦИЯ (заглушка)
// =============================================================================

namespace Distillation {
    void update(SystemState& state, const Settings& settings) {
        // TODO: Реализовать логику дистилляции
    }
}

// =============================================================================
// РУЧНАЯ РЕКТИФИКАЦИЯ (заглушка)
// =============================================================================

namespace ManualRect {
    void update(SystemState& state, const Settings& settings) {
        // TODO: Реализовать логику ручной ректификации
    }
}

// =============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// =============================================================================

const char* getModeName(Mode mode) {
    switch (mode) {
        case Mode::IDLE: return "Ожидание";
        case Mode::RECTIFICATION: return "Ректификация";
        case Mode::DISTILLATION: return "Дистилляция";
        case Mode::MANUAL_RECT: return "Ручная ректификация";
        case Mode::MASHING: return "Затирка солода";
        case Mode::HOLD: return "Hold режим";
        default: return "Неизвестно";
    }
}

const char* getPhaseName(RectPhase phase) {
    switch (phase) {
        case RectPhase::IDLE: return "Ожидание";
        case RectPhase::HEATING: return "Нагрев";
        case RectPhase::STABILIZATION: return "Стабилизация";
        case RectPhase::HEADS: return "Отбор голов";
        case RectPhase::POST_HEADS_STABILIZATION: return "Стабилизация после голов";
        case RectPhase::BODY: return "Отбор тела";
        case RectPhase::TAILS: return "Отбор хвостов";
        case RectPhase::PURGE: return "Продувка";
        case RectPhase::FINISH: return "Завершение";
        case RectPhase::COMPLETED: return "Завершено";
        default: return "Неизвестно";
    }
}

} // namespace FSM
