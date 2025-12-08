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
    // Обрабатываем только режим авто-ректификации
    if (state.mode != Mode::RECTIFICATION) {
        return;
    }

    uint32_t now = millis();
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

        case RectPhase::HEADS:
            // Отбор голов
            float headsSpeed = settings.rectParams.headsSpeedMlHKw *
                               (settings.equipment.heaterPowerW / 1000.0f);
            Pump::start(headsSpeed);
            Valves::setHeads(true);

            // TODO: Проверка завершения по объёму или T_column
            break;

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

void start(SystemState& state, Mode mode) {
    LOG_I("FSM: Starting mode %d", static_cast<int>(mode));

    state.mode = mode;

    if (mode == Mode::RECTIFICATION) {
        state.rectPhase = RectPhase::HEATING;
        phaseStartTime = millis();

        // Отправка уведомления о старте
        MQTT::publishNotification(
            "Процесс запущен",
            "Начат процесс ректификации - фаза разогрева",
            "info"
        );
    }
}

void stop(SystemState& state) {
    LOG_I("FSM: Stopping");

    Heater::setPower(0);
    Pump::stop();
    Valves::closeAll();

    state.mode = Mode::IDLE;
    state.rectPhase = RectPhase::IDLE;

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

} // namespace FSM
