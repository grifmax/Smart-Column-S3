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
#include "watt_control.h"
#include "../interface/mqtt.h"

namespace FSM {

static uint32_t phaseStartTime = 0;
static float phaseStartVolumeMl = 0.0f;
static uint32_t pauseStartTime = 0;

// Параметры/состояние ректификации (простая реализация)
static float rectHeadsTargetMl = 0.0f;
static bool rectBodyInitialized = false;

// Параметры дистилляции (задаются через API)
namespace {
struct DistillationParamsRuntime {
  float speedMlH = 500.0f;
  float headsVolumeMl = 0.0f;
  float targetVolumeMl = 0.0f;
  float endTempC = 96.0f;
};

static DistillationParamsRuntime g_distParams;

static float estimateChargeAbvPercent(const SystemState &state) {
  // Если есть валидные данные ареометра — используем их
  if (state.hydrometer.valid && state.hydrometer.abv > 0.0f &&
      state.hydrometer.abv < 100.0f) {
    return state.hydrometer.abv;
  }
  // По умолчанию ориентируемся на СС ~40% (самый частый кейс для ректификации)
  return 40.0f;
}

static uint8_t getProcessHeaterPower(const SystemState &state,
                                     const Settings &settings,
                                     uint8_t fallbackPercent) {
  // Если датчик давления жив — используем WattControl (защита от захлёба)
  if (state.pressure.ok) {
    return WattControl::update(state, settings);
  }
  return fallbackPercent;
}
} // namespace

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
                phaseStartVolumeMl = state.pump.totalVolumeMl;
                rectBodyInitialized = false;

                // Отправка уведомления
                MQTT::publishNotification(
                    "Фаза: Стабилизация",
                    "Разогрев завершён, начата стабилизация колонны",
                    "info"
                );
            }
            break;

        case RectPhase::STABILIZATION:
            // Стабилизация "на себя": насос выкл, головы закрыты, охлаждение вкл
            Valves::setHeads(false);
            Pump::stop();
            Valves::setWater(true);

            // Мощность: по давлению если доступно, иначе мягкий фолбэк
            Heater::setPower(getProcessHeaterPower(state, settings, 70));

            // Работа "на себя" 20 минут
            if (elapsed > settings.rectParams.stabilizationMin * 60 * 1000UL) {
                LOG_I("FSM: STABILIZATION → HEADS");
                state.rectPhase = RectPhase::HEADS;
                phaseStartTime = now;
                phaseStartVolumeMl = state.pump.totalVolumeMl;

                // Рассчитать целевой объём голов (мл) из % от АС
                float abv = estimateChargeAbvPercent(state);
                float aaMl =
                    settings.equipment.cubeVolumeL * 1000.0f * (abv / 100.0f);
                rectHeadsTargetMl =
                    aaMl * (settings.rectParams.headsPercent / 100.0f);
                if (rectHeadsTargetMl < 10.0f) {
                  rectHeadsTargetMl = 10.0f; // защита от нулевых настроек
                }

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
            Heater::setPower(getProcessHeaterPower(state, settings, 60));
            float headsSpeed = settings.rectParams.headsSpeedMlHKw *
                               (settings.equipment.heaterPowerW / 1000.0f);
            Pump::start(headsSpeed);
            Valves::setHeads(true);
            Valves::setWater(true);

            const float headsCollected = state.pump.totalVolumeMl - phaseStartVolumeMl;
            state.stats.headsVolume = headsCollected;

            if (rectHeadsTargetMl > 0.0f && headsCollected >= rectHeadsTargetMl) {
              LOG_I("FSM: HEADS → POST_HEADS_STABILIZATION (%.0f/%.0f ml)",
                    headsCollected, rectHeadsTargetMl);
              state.rectPhase = RectPhase::POST_HEADS_STABILIZATION;
              phaseStartTime = now;
              Pump::stop();
              Valves::setHeads(false);

              MQTT::publishNotification(
                  "Фаза: Стабилизация после голов",
                  "Отбор голов завершён, стабилизация перед отбором тела",
                  "info");
            }

            break;
        }

        case RectPhase::POST_HEADS_STABILIZATION:
            // Короткая стабилизация (по умолчанию 5 минут)
            Pump::stop();
            Valves::setHeads(false);
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 65));

            if (elapsed > 5 * 60 * 1000UL) {
              LOG_I("FSM: POST_HEADS_STABILIZATION → PURGE");
              state.rectPhase = RectPhase::PURGE;
              phaseStartTime = now;
            }
            break;

        case RectPhase::PURGE:
            // Продувка
            Pump::stop();
            Valves::closeAll();
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 65));

            if (elapsed > settings.rectParams.purgeMin * 60 * 1000UL) {
                LOG_I("FSM: PURGE → BODY");
                state.rectPhase = RectPhase::BODY;
                phaseStartTime = now;
                phaseStartVolumeMl = state.pump.totalVolumeMl;
                rectBodyInitialized = false;
                SmartDecrement::reset();

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
            Valves::setHeads(false);
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 60));

            // Инициализация Smart Decrement при входе в BODY
            if (!rectBodyInitialized && state.temps.valid[TEMP_COLUMN_TOP]) {
              SmartDecrement::init(state.temps.columnTop);
              rectBodyInitialized = true;
            }

            {
              float bodySpeed = settings.rectParams.bodySpeedMlHKw *
                                (settings.equipment.heaterPowerW / 1000.0f);
              Pump::start(bodySpeed);

              const float bodyCollected = state.pump.totalVolumeMl - phaseStartVolumeMl;
              state.stats.bodyVolume = bodyCollected;

              // Smart Decrement: true означает переход в хвосты
              if (rectBodyInitialized && SmartDecrement::update(state, settings)) {
                LOG_I("FSM: BODY → TAILS (SmartDecrement)");
                state.rectPhase = RectPhase::TAILS;
                phaseStartTime = now;
                phaseStartVolumeMl = state.pump.totalVolumeMl;
              }

              // Дополнительное условие завершения (по температуре куба)
              if (state.temps.valid[TEMP_CUBE] &&
                  state.temps.cube >= TAILS_TEMP_CUBE_MIN) {
                LOG_I("FSM: BODY → TAILS (T_cube %.1fC)", state.temps.cube);
                state.rectPhase = RectPhase::TAILS;
                phaseStartTime = now;
                phaseStartVolumeMl = state.pump.totalVolumeMl;
              }
            }
            break;

        case RectPhase::TAILS:
            // Отбор хвостов
            Valves::setHeads(false);
            Valves::setWater(true);
            Heater::setPower(getProcessHeaterPower(state, settings, 50));

            {
              float tailsSpeed =
                  (settings.rectParams.bodySpeedMlHKw *
                   (settings.equipment.heaterPowerW / 1000.0f)) *
                  0.6f;
              Pump::start(tailsSpeed);

              const float tailsCollected = state.pump.totalVolumeMl - phaseStartVolumeMl;
              state.stats.tailsVolume = tailsCollected;

              // Завершение погона по температуре куба
              if (state.temps.valid[TEMP_CUBE] &&
                  state.temps.cube >= TAILS_TEMP_CUBE_MIN) {
                LOG_I("FSM: TAILS → FINISH (T_cube %.1fC)", state.temps.cube);
                state.rectPhase = RectPhase::FINISH;
                phaseStartTime = now;
              }
            }
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
    uint32_t now = millis();

    state.mode = mode;
    state.paused = false;

    if (mode == Mode::RECTIFICATION) {
        state.rectPhase = RectPhase::HEATING;
        phaseStartTime = now;
        phaseStartVolumeMl = 0.0f;
        rectHeadsTargetMl = 0.0f;
        rectBodyInitialized = false;

        // Сброс статистики/объёма
        state.stats = ProcessStats{};
        Pump::resetVolume();

        // Отправка уведомления о старте
        MQTT::publishNotification(
            "Процесс запущен",
            "Начат процесс ректификации - фаза разогрева",
            "info"
        );
    } else if (mode == Mode::DISTILLATION) {
        state.rectPhase = RectPhase::HEATING; // используем общий enum для UI
        phaseStartTime = now;
        phaseStartVolumeMl = 0.0f;
        state.stats = ProcessStats{};
        Pump::resetVolume();
        LOG_I("FSM: Distillation mode started");
    } else if (mode == Mode::MANUAL_RECT) {
        state.rectPhase = RectPhase::IDLE;
        // TODO: Инициализация ручной ректификации
        LOG_I("FSM: Manual rectification mode started");
    } else if (mode == Mode::MASHING) {
        state.rectPhase = RectPhase::IDLE;
        state.mashing.phase = MashPhase::IDLE;
        state.mashing.currentStep = 0;
        state.mashing.tempInRange = false;
        state.mashing.inRangeStartTime = 0;
        state.mashing.active = true;
        LOG_I("FSM: Mashing mode started");
    } else if (mode == Mode::HOLD) {
        state.rectPhase = RectPhase::IDLE;
        state.hold.active = true;
        state.hold.currentStep = 0;
        state.hold.stepStartTime = now;
        state.hold.tempInRange = false;
        state.hold.inRangeStartTime = 0;
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
    state.mashing.stepCount = 0;
    state.mashing.active = false;
    state.mashing.stepName[0] = '\0';
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
    if (state.paused) {
        return;
    }

    state.paused = true;
    pauseStartTime = millis();

    // Безопасная пауза: выключить нагрев и отбор, оставить охлаждение
    Heater::setPower(0);
    Pump::stop();
    Valves::setHeads(false);
    Valves::setUno(false);
    Valves::setStartStop(0);
    // Охлаждение включаем, если процесс был активен/нагревался
    if (state.mode != Mode::IDLE || state.temps.cube > 45.0f) {
        Valves::setWater(true);
    }

    LOG_I("FSM: Paused (actuators stopped)");
}

void resume(SystemState& state) {
    if (!state.paused) {
        return;
    }

    // Компенсировать время паузы, чтобы таймеры фаз не "убежали"
    uint32_t now = millis();
    if (pauseStartTime > 0 && now > pauseStartTime) {
        uint32_t pausedMs = now - pauseStartTime;
        phaseStartTime += pausedMs;

        // Заморозить таймеры выдержек для затирки/hold
        if (state.mashing.active) {
            state.mashing.stepStartTime += pausedMs;
            if (state.mashing.tempInRange && state.mashing.inRangeStartTime > 0) {
                state.mashing.inRangeStartTime += pausedMs;
            }
        }
        if (state.hold.active) {
            state.hold.stepStartTime += pausedMs;
            if (state.hold.tempInRange && state.hold.inRangeStartTime > 0) {
                state.hold.inRangeStartTime += pausedMs;
            }
        }
    }

    pauseStartTime = 0;
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

    // Затирка не использует насос/клапаны отбора
    Pump::stop();
    Valves::setHeads(false);
    Valves::setUno(false);
    Valves::setStartStop(0);
    
    // Простая логика PID для поддержания температуры
    float targetTemp = state.mashing.targetTemp;
    float error = targetTemp - currentTemp;
    
    // Пропорциональное управление мощностью
    // Kp = 2 означает, что при ошибке 1°C мощность изменится на 2%
    float Kp = 2.0f;
    int powerDelta = (int)(error * Kp);
    int currentPower = (int)Heater::getPower();
    int newPower = currentPower + powerDelta;
    if (newPower > 100) newPower = 100;
    if (newPower < 0) newPower = 0;
    Heater::setPower((uint8_t)newPower);
    
    // Проверка завершения текущей паузы
    if (state.mashing.currentStep < currentProfile->stepCount) {
        const bool tempReached = fabs(currentTemp - targetTemp) < 1.0f; // Допуск ±1°C

        // Запускаем таймер выдержки только когда температура в допуске
        if (tempReached) {
            if (!state.mashing.tempInRange) {
                state.mashing.tempInRange = true;
                state.mashing.inRangeStartTime = now;
            }
        } else {
            state.mashing.tempInRange = false;
            state.mashing.inRangeStartTime = 0;
        }

        const bool timeElapsed =
            state.mashing.tempInRange &&
            state.mashing.inRangeStartTime > 0 &&
            (now - state.mashing.inRangeStartTime) >= (state.mashing.stepDuration * 1000UL);

        if (timeElapsed) {
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
        state.mashing.tempInRange = false;
        state.mashing.inRangeStartTime = 0;
        state.mashing.stepCount = currentProfile->stepCount;
        strncpy(state.mashing.stepName,
                currentProfile->steps[state.mashing.currentStep].name,
                sizeof(state.mashing.stepName) - 1);
        state.mashing.stepName[sizeof(state.mashing.stepName) - 1] = '\0';

        // Обновить фазу (простое сопоставление по номеру шага)
        switch (state.mashing.currentStep) {
          case 0: state.mashing.phase = MashPhase::ACID_REST; break;
          case 1: state.mashing.phase = MashPhase::PROTEIN_REST; break;
          case 2: state.mashing.phase = MashPhase::BETA_AMYLASE; break;
          case 3: state.mashing.phase = MashPhase::ALPHA_AMYLASE; break;
          case 4: state.mashing.phase = MashPhase::MASH_OUT; break;
          default: break;
        }
        
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
    
    // Безопасное состояние исполнительных механизмов для затирки
    Pump::stop();
    Valves::closeAll();

    setProfile(profile);
    state.mashing.active = true;
    state.mashing.stepCount = profile->stepCount;
    state.mashing.currentStep = 0;
    state.mashing.targetTemp = profile->steps[0].temperature;
    state.mashing.stepDuration = profile->steps[0].duration * 60; // минуты -> секунды
    state.mashing.stepStartTime = millis();
    state.mashing.tempInRange = false;
    state.mashing.inRangeStartTime = 0;
    state.mashing.phase = MashPhase::ACID_REST;
    strncpy(state.mashing.stepName, profile->steps[0].name,
            sizeof(state.mashing.stepName) - 1);
    state.mashing.stepName[sizeof(state.mashing.stepName) - 1] = '\0';
    state.mode = Mode::MASHING;
    state.rectPhase = RectPhase::IDLE;
    
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

    // Hold не использует насос/клапаны отбора
    Pump::stop();
    Valves::setHeads(false);
    Valves::setUno(false);
    Valves::setStartStop(0);
    
    if (state.hold.currentStep < state.hold.stepCount) {
        float targetTemp = state.hold.targetTemp;
        float error = targetTemp - currentTemp;
        
        // Пропорциональное управление мощностью
        float Kp = 2.0f;
        int powerDelta = (int)(error * Kp);
        int currentPower = (int)Heater::getPower();
        int newPower = currentPower + powerDelta;
        if (newPower > 100) newPower = 100;
        if (newPower < 0) newPower = 0;
        Heater::setPower((uint8_t)newPower);
        
        // Проверка завершения текущего шага
        const bool tempReached = fabs(currentTemp - targetTemp) < 1.0f; // Допуск ±1°C
        if (tempReached) {
            if (!state.hold.tempInRange) {
                state.hold.tempInRange = true;
                state.hold.inRangeStartTime = now;
            }
        } else {
            state.hold.tempInRange = false;
            state.hold.inRangeStartTime = 0;
        }

        const uint32_t stepDurationSec =
            (uint32_t)state.hold.steps[state.hold.currentStep].duration * 60UL;
        const bool timeElapsed =
            state.hold.tempInRange &&
            state.hold.inRangeStartTime > 0 &&
            (now - state.hold.inRangeStartTime) >= (stepDurationSec * 1000UL);

        if (timeElapsed) {
            // Переход к следующему шагу
            state.hold.currentStep++;
            
            if (state.hold.currentStep < state.hold.stepCount) {
                state.hold.targetTemp = state.hold.steps[state.hold.currentStep].temperature;
                state.hold.stepStartTime = now;
                state.hold.tempInRange = false;
                state.hold.inRangeStartTime = 0;
                
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
    
    // Безопасное состояние исполнительных механизмов для Hold
    Pump::stop();
    Valves::closeAll();

    // Копировать шаги в состояние
    for (uint8_t i = 0; i < count; i++) {
        state.hold.steps[i] = steps[i];
    }
    
    state.hold.stepCount = count;
    state.hold.currentStep = 0;
    state.hold.targetTemp = steps[0].temperature;
    state.hold.stepStartTime = millis();
    state.hold.tempInRange = false;
    state.hold.inRangeStartTime = 0;
    state.hold.active = true;
    state.mode = Mode::HOLD;
    state.rectPhase = RectPhase::IDLE;
    
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
void setParams(float speedMlH, float headsVolumeMl, float targetVolumeMl,
               float endTempC) {
  if (speedMlH > 0) {
    g_distParams.speedMlH = speedMlH;
  }
  if (headsVolumeMl >= 0) {
    g_distParams.headsVolumeMl = headsVolumeMl;
  }
  if (targetVolumeMl >= 0) {
    g_distParams.targetVolumeMl = targetVolumeMl;
  }
  if (endTempC > 0) {
    g_distParams.endTempC = endTempC;
  }

  LOG_I("Distillation params: speed=%.0f ml/h, heads=%.0f ml, target=%.0f ml, "
        "endTemp=%.1fC",
        g_distParams.speedMlH, g_distParams.headsVolumeMl,
        g_distParams.targetVolumeMl, g_distParams.endTempC);
}

void update(SystemState &state, const Settings &settings) {
  uint32_t now = millis();

  // Базовые действия для дистилляции
  if (state.temps.cube > 45.0f) {
    Valves::setWater(true);
  }

  // Используем rectPhase как "этап" для UI (heating/heads/body/finish)
  switch (state.rectPhase) {
  case RectPhase::HEATING: {
    Heater::setPower(100);

    // Переход к отбору при приближении к кипению
    if (state.temps.valid[TEMP_CUBE] && state.temps.cube >= 78.0f) {
      phaseStartTime = now;
      phaseStartVolumeMl = state.pump.totalVolumeMl;

      if (g_distParams.headsVolumeMl > 0.0f) {
        state.rectPhase = RectPhase::HEADS;
        LOG_I("Distillation: HEATING → HEADS");
      } else {
        state.rectPhase = RectPhase::BODY;
        LOG_I("Distillation: HEATING → BODY");
      }
    }
    break;
  }

  case RectPhase::HEADS: {
    Heater::setPower(100);
    Pump::start(g_distParams.speedMlH);
    Valves::setHeads(true);

    const float collected = state.pump.totalVolumeMl - phaseStartVolumeMl;
    state.stats.headsVolume = collected;

    if (collected >= g_distParams.headsVolumeMl) {
      Valves::setHeads(false);
      phaseStartTime = now;
      phaseStartVolumeMl = state.pump.totalVolumeMl;
      state.rectPhase = RectPhase::BODY;
      LOG_I("Distillation: HEADS → BODY");
    }
    break;
  }

  case RectPhase::BODY: {
    Heater::setPower(100);
    Valves::setHeads(false);
    Pump::start(g_distParams.speedMlH);

    const float collectedBody = state.pump.totalVolumeMl - phaseStartVolumeMl;
    state.stats.bodyVolume = collectedBody;

    const bool endByTemp =
        (g_distParams.endTempC > 0.0f && state.temps.valid[TEMP_CUBE] &&
         state.temps.cube >= g_distParams.endTempC);
    const bool endByVolume =
        (g_distParams.targetVolumeMl > 0.0f &&
         state.pump.totalVolumeMl >= g_distParams.targetVolumeMl);

    if (endByTemp || endByVolume) {
      phaseStartTime = now;
      state.rectPhase = RectPhase::FINISH;
      LOG_I("Distillation: BODY → FINISH (%s%s)", endByTemp ? "temp" : "",
            endByVolume ? " volume" : "");
    }
    break;
  }

  case RectPhase::FINISH: {
    uint32_t elapsed = now - phaseStartTime;
    Heater::setPower(0);
    Pump::stop();
    Valves::setHeads(false);
    Valves::setWater(true);

    if (elapsed > 5 * 60 * 1000UL) {
      Valves::closeAll();
      state.rectPhase = RectPhase::IDLE;
      state.mode = Mode::IDLE;
      LOG_I("Distillation: Process complete!");
    }
    break;
  }

  default:
    // Если попали в "не тот" rectPhase — аккуратно приводим к BODY
    state.rectPhase = RectPhase::BODY;
    phaseStartTime = now;
    phaseStartVolumeMl = state.pump.totalVolumeMl;
    break;
  }
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
