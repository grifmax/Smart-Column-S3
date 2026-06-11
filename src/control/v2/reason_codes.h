#ifndef CONTROL_V2_REASON_CODES_H
#define CONTROL_V2_REASON_CODES_H

#include <Arduino.h>

namespace ControlV2 {

struct ReasonInsightTextV2 {
    const char* tone;
    const char* title;
    const char* detail;
    const char* action;
};

enum class ReasonCodeV2 : uint16_t {
    NONE = 0,
    RC_MODE_START_REQUEST,
    RC_MODE_STOP_REQUEST,
    RC_PRECHECK_OK,
    RC_PRECHECK_FAIL_SENSOR,
    RC_PRECHECK_FAIL_SAFETY_LATCH,
    RC_HEATING_COMPLETE,
    RC_STABILIZATION_TIMER_OK,
    RC_STABILITY_WINDOW_REACHED,
    RC_HEADS_VOLUME_REACHED,
    RC_HEADS_SCORE_REACHED,
    RC_POST_HEADS_STABILIZATION_COMPLETE,
    RC_PURGE_COMPLETE,
    RC_BODY_TARGET_VOLUME_REACHED,
    RC_BODY_END_DETECTED,
    RC_TAILS_TARGET_REACHED,
    RC_FINISH_COOLDOWN_COMPLETE,
    RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED,
    RC_DISTILLATION_END_TEMP_REACHED,
    RC_DISTILLATION_TARGET_VOLUME_REACHED,
    RC_NBK_STEAM_READY,
    RC_NBK_STABILIZATION_COMPLETE,
    RC_NBK_FEED_ENABLED,
    RC_NBK_FINISH_LIKELY,
    RC_TEMP_STEP_REACHED,
    RC_TEMP_STEP_HOLD_COMPLETE,
    RC_TEMP_STEP_TIMEOUT,
    RC_FERM_TARGET_REACHED,
    RC_SAFETY_LIMIT_POWER,
    RC_SAFETY_LIMIT_TAKEOFF,
    RC_SAFETY_PHASE_BLOCKED,
    RC_SAFETY_ANTI_OSCILLATION_GUARD,
    RC_SAFETY_RECOVERY_ENTERED,
    RC_SAFETY_RECOVERY_EXITED,
    RC_SAFETY_TRIP_PRESSURE,
    RC_SAFETY_TRIP_SENSOR,
    RC_SAFETY_TRIP_OVERHEAT,
    RC_SAFETY_TRIP_POWER,
    RC_SAFETY_TRIP_GENERIC,
    RC_SAFETY_ACKNOWLEDGED,
    RC_SAFETY_RESET_COMPLETED,
    RC_OPERATOR_SERVICE_ACTION,
    RC_MANUAL_OPERATOR_SWITCH,
    RC_MANUAL_OPERATOR_STOP,
    RC_PHASE_RECOVERY_APPLIED,
    RC_PHASE_TRANSITION_INFERRED,
    RC_UNSPECIFIED
};

enum class SafetySeverityV2 : uint8_t {
    NONE = 0,
    INFO,
    WARNING,
    LIMITED,
    RECOVERY,
    TRIP,
    LATCHED_TRIP
};

enum class SafetyEventTypeV2 : uint8_t {
    NONE = 0,
    SENSOR_STALE,
    SENSOR_FAILURE,
    PRESSURE_HIGH,
    PRESSURE_RISE_FAST,
    COOLING_OVERHEAT,
    COOLING_MARGIN_LOW,
    COLUMN_FLOOD_RISK,
    OVERHEAT,
    POWER_FAILURE,
    EMERGENCY_STOP,
    POWER_LIMIT_APPLIED,
    TAKEOFF_LIMIT_APPLIED,
    PHASE_ADVANCE_BLOCKED,
    ANTI_OSCILLATION_GUARD
};

inline const char* reasonCodeToString(ReasonCodeV2 code) {
    switch (code) {
        case ReasonCodeV2::NONE: return "RC_NONE";
        case ReasonCodeV2::RC_MODE_START_REQUEST: return "RC_MODE_START_REQUEST";
        case ReasonCodeV2::RC_MODE_STOP_REQUEST: return "RC_MODE_STOP_REQUEST";
        case ReasonCodeV2::RC_PRECHECK_OK: return "RC_PRECHECK_OK";
        case ReasonCodeV2::RC_PRECHECK_FAIL_SENSOR: return "RC_PRECHECK_FAIL_SENSOR";
        case ReasonCodeV2::RC_PRECHECK_FAIL_SAFETY_LATCH: return "RC_PRECHECK_FAIL_SAFETY_LATCH";
        case ReasonCodeV2::RC_HEATING_COMPLETE: return "RC_HEATING_COMPLETE";
        case ReasonCodeV2::RC_STABILIZATION_TIMER_OK: return "RC_STABILIZATION_TIMER_OK";
        case ReasonCodeV2::RC_STABILITY_WINDOW_REACHED: return "RC_STABILITY_WINDOW_REACHED";
        case ReasonCodeV2::RC_HEADS_VOLUME_REACHED: return "RC_HEADS_VOLUME_REACHED";
        case ReasonCodeV2::RC_HEADS_SCORE_REACHED: return "RC_HEADS_SCORE_REACHED";
        case ReasonCodeV2::RC_POST_HEADS_STABILIZATION_COMPLETE: return "RC_POST_HEADS_STABILIZATION_COMPLETE";
        case ReasonCodeV2::RC_PURGE_COMPLETE: return "RC_PURGE_COMPLETE";
        case ReasonCodeV2::RC_BODY_TARGET_VOLUME_REACHED: return "RC_BODY_TARGET_VOLUME_REACHED";
        case ReasonCodeV2::RC_BODY_END_DETECTED: return "RC_BODY_END_DETECTED";
        case ReasonCodeV2::RC_TAILS_TARGET_REACHED: return "RC_TAILS_TARGET_REACHED";
        case ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE: return "RC_FINISH_COOLDOWN_COMPLETE";
        case ReasonCodeV2::RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED: return "RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED";
        case ReasonCodeV2::RC_DISTILLATION_END_TEMP_REACHED: return "RC_DISTILLATION_END_TEMP_REACHED";
        case ReasonCodeV2::RC_DISTILLATION_TARGET_VOLUME_REACHED: return "RC_DISTILLATION_TARGET_VOLUME_REACHED";
        case ReasonCodeV2::RC_NBK_STEAM_READY: return "RC_NBK_STEAM_READY";
        case ReasonCodeV2::RC_NBK_STABILIZATION_COMPLETE: return "RC_NBK_STABILIZATION_COMPLETE";
        case ReasonCodeV2::RC_NBK_FEED_ENABLED: return "RC_NBK_FEED_ENABLED";
        case ReasonCodeV2::RC_NBK_FINISH_LIKELY: return "RC_NBK_FINISH_LIKELY";
        case ReasonCodeV2::RC_TEMP_STEP_REACHED: return "RC_TEMP_STEP_REACHED";
        case ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE: return "RC_TEMP_STEP_HOLD_COMPLETE";
        case ReasonCodeV2::RC_TEMP_STEP_TIMEOUT: return "RC_TEMP_STEP_TIMEOUT";
        case ReasonCodeV2::RC_FERM_TARGET_REACHED: return "RC_FERM_TARGET_REACHED";
        case ReasonCodeV2::RC_SAFETY_LIMIT_POWER: return "RC_SAFETY_LIMIT_POWER";
        case ReasonCodeV2::RC_SAFETY_LIMIT_TAKEOFF: return "RC_SAFETY_LIMIT_TAKEOFF";
        case ReasonCodeV2::RC_SAFETY_PHASE_BLOCKED: return "RC_SAFETY_PHASE_BLOCKED";
        case ReasonCodeV2::RC_SAFETY_ANTI_OSCILLATION_GUARD: return "RC_SAFETY_ANTI_OSCILLATION_GUARD";
        case ReasonCodeV2::RC_SAFETY_RECOVERY_ENTERED: return "RC_SAFETY_RECOVERY_ENTERED";
        case ReasonCodeV2::RC_SAFETY_RECOVERY_EXITED: return "RC_SAFETY_RECOVERY_EXITED";
        case ReasonCodeV2::RC_SAFETY_TRIP_PRESSURE: return "RC_SAFETY_TRIP_PRESSURE";
        case ReasonCodeV2::RC_SAFETY_TRIP_SENSOR: return "RC_SAFETY_TRIP_SENSOR";
        case ReasonCodeV2::RC_SAFETY_TRIP_OVERHEAT: return "RC_SAFETY_TRIP_OVERHEAT";
        case ReasonCodeV2::RC_SAFETY_TRIP_POWER: return "RC_SAFETY_TRIP_POWER";
        case ReasonCodeV2::RC_SAFETY_TRIP_GENERIC: return "RC_SAFETY_TRIP_GENERIC";
        case ReasonCodeV2::RC_SAFETY_ACKNOWLEDGED: return "RC_SAFETY_ACKNOWLEDGED";
        case ReasonCodeV2::RC_SAFETY_RESET_COMPLETED: return "RC_SAFETY_RESET_COMPLETED";
        case ReasonCodeV2::RC_OPERATOR_SERVICE_ACTION: return "RC_OPERATOR_SERVICE_ACTION";
        case ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH: return "RC_MANUAL_OPERATOR_SWITCH";
        case ReasonCodeV2::RC_MANUAL_OPERATOR_STOP: return "RC_MANUAL_OPERATOR_STOP";
        case ReasonCodeV2::RC_PHASE_RECOVERY_APPLIED: return "RC_PHASE_RECOVERY_APPLIED";
        case ReasonCodeV2::RC_PHASE_TRANSITION_INFERRED: return "RC_PHASE_TRANSITION_INFERRED";
        case ReasonCodeV2::RC_UNSPECIFIED: return "RC_UNSPECIFIED";
        default: return "RC_UNKNOWN";
    }
}

inline const char* safetySeverityToString(SafetySeverityV2 severity) {
    switch (severity) {
        case SafetySeverityV2::NONE: return "none";
        case SafetySeverityV2::INFO: return "info";
        case SafetySeverityV2::WARNING: return "warning";
        case SafetySeverityV2::LIMITED: return "limited";
        case SafetySeverityV2::RECOVERY: return "recovery";
        case SafetySeverityV2::TRIP: return "trip";
        case SafetySeverityV2::LATCHED_TRIP: return "latched_trip";
        default: return "unknown";
    }
}

inline const char* safetyEventTypeToString(SafetyEventTypeV2 type) {
    switch (type) {
        case SafetyEventTypeV2::NONE: return "none";
        case SafetyEventTypeV2::SENSOR_STALE: return "sensor_stale";
        case SafetyEventTypeV2::SENSOR_FAILURE: return "sensor_failure";
        case SafetyEventTypeV2::PRESSURE_HIGH: return "pressure_high";
        case SafetyEventTypeV2::PRESSURE_RISE_FAST: return "pressure_rise_fast";
        case SafetyEventTypeV2::COOLING_OVERHEAT: return "cooling_overheat";
        case SafetyEventTypeV2::COOLING_MARGIN_LOW: return "cooling_margin_low";
        case SafetyEventTypeV2::COLUMN_FLOOD_RISK: return "column_flood_risk";
        case SafetyEventTypeV2::OVERHEAT: return "overheat";
        case SafetyEventTypeV2::POWER_FAILURE: return "power_failure";
        case SafetyEventTypeV2::EMERGENCY_STOP: return "emergency_stop";
        case SafetyEventTypeV2::POWER_LIMIT_APPLIED: return "power_limit_applied";
        case SafetyEventTypeV2::TAKEOFF_LIMIT_APPLIED: return "takeoff_limit_applied";
        case SafetyEventTypeV2::PHASE_ADVANCE_BLOCKED: return "phase_advance_blocked";
        case SafetyEventTypeV2::ANTI_OSCILLATION_GUARD: return "anti_oscillation_guard";
        default: return "unknown";
    }
}

inline const char* reasonCodeLabel(ReasonCodeV2 code) {
    switch (code) {
        case ReasonCodeV2::NONE: return "Нет активной причины";
        case ReasonCodeV2::RC_MODE_START_REQUEST: return "Запуск режима";
        case ReasonCodeV2::RC_MODE_STOP_REQUEST: return "Остановка режима";
        case ReasonCodeV2::RC_PRECHECK_OK: return "Предпусковые проверки пройдены";
        case ReasonCodeV2::RC_PRECHECK_FAIL_SENSOR: return "Проблема с датчиками";
        case ReasonCodeV2::RC_PRECHECK_FAIL_SAFETY_LATCH: return "Активен safety latch";
        case ReasonCodeV2::RC_HEATING_COMPLETE: return "Разгон завершён";
        case ReasonCodeV2::RC_STABILIZATION_TIMER_OK: return "Стабилизация завершена";
        case ReasonCodeV2::RC_STABILITY_WINDOW_REACHED: return "Окно стабильности достигнуто";
        case ReasonCodeV2::RC_HEADS_VOLUME_REACHED: return "Головы завершены по объёму";
        case ReasonCodeV2::RC_HEADS_SCORE_REACHED: return "Головы завершены по score";
        case ReasonCodeV2::RC_POST_HEADS_STABILIZATION_COMPLETE: return "Постстабилизация завершена";
        case ReasonCodeV2::RC_PURGE_COMPLETE: return "Продувка завершена";
        case ReasonCodeV2::RC_BODY_TARGET_VOLUME_REACHED: return "Тело завершено по объёму";
        case ReasonCodeV2::RC_BODY_END_DETECTED: return "Обнаружен конец тела";
        case ReasonCodeV2::RC_TAILS_TARGET_REACHED: return "Хвосты завершены";
        case ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE: return "Финишное охлаждение завершено";
        case ReasonCodeV2::RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED: return "Головы пропущены";
        case ReasonCodeV2::RC_DISTILLATION_END_TEMP_REACHED: return "Достигнута стоп-температура";
        case ReasonCodeV2::RC_DISTILLATION_TARGET_VOLUME_REACHED: return "Достигнут целевой объём";
        case ReasonCodeV2::RC_NBK_STEAM_READY: return "Пар готов";
        case ReasonCodeV2::RC_NBK_STABILIZATION_COMPLETE: return "НБК стабилизирована";
        case ReasonCodeV2::RC_NBK_FEED_ENABLED: return "Подача разрешена";
        case ReasonCodeV2::RC_NBK_FINISH_LIKELY: return "Вероятен финал НБК";
        case ReasonCodeV2::RC_TEMP_STEP_REACHED: return "Температурный шаг достигнут";
        case ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE: return "Выдержка завершена";
        case ReasonCodeV2::RC_TEMP_STEP_TIMEOUT: return "Таймаут температурного шага";
        case ReasonCodeV2::RC_FERM_TARGET_REACHED: return "Цель ферментации достигнута";
        case ReasonCodeV2::RC_SAFETY_LIMIT_POWER: return "Ограничение мощности";
        case ReasonCodeV2::RC_SAFETY_LIMIT_TAKEOFF: return "Ограничение отбора";
        case ReasonCodeV2::RC_SAFETY_PHASE_BLOCKED: return "Переход фазы заблокирован";
        case ReasonCodeV2::RC_SAFETY_ANTI_OSCILLATION_GUARD: return "Anti-oscillation guard";
        case ReasonCodeV2::RC_SAFETY_RECOVERY_ENTERED: return "Вход в recovery";
        case ReasonCodeV2::RC_SAFETY_RECOVERY_EXITED: return "Выход из recovery";
        case ReasonCodeV2::RC_SAFETY_TRIP_PRESSURE: return "Авария по давлению";
        case ReasonCodeV2::RC_SAFETY_TRIP_SENSOR: return "Авария по датчикам";
        case ReasonCodeV2::RC_SAFETY_TRIP_OVERHEAT: return "Авария по перегреву";
        case ReasonCodeV2::RC_SAFETY_TRIP_POWER: return "Авария по питанию";
        case ReasonCodeV2::RC_SAFETY_TRIP_GENERIC: return "Общая safety-авария";
        case ReasonCodeV2::RC_SAFETY_ACKNOWLEDGED: return "Авария подтверждена";
        case ReasonCodeV2::RC_SAFETY_RESET_COMPLETED: return "Safety reset выполнен";
        case ReasonCodeV2::RC_OPERATOR_SERVICE_ACTION: return "Сервисное действие оператора";
        case ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH: return "Ручное переключение";
        case ReasonCodeV2::RC_MANUAL_OPERATOR_STOP: return "Ручной останов";
        case ReasonCodeV2::RC_PHASE_RECOVERY_APPLIED: return "Восстановление фазы применено";
        case ReasonCodeV2::RC_PHASE_TRANSITION_INFERRED: return "Переход фазы восстановлен";
        case ReasonCodeV2::RC_UNSPECIFIED:
        default:
            return "Без уточнения";
    }
}

inline ReasonInsightTextV2 getReasonInsightText(ReasonCodeV2 code,
                                                const char* operatorMessage = nullptr,
                                                bool completedSuccessfully = false) {
    if (operatorMessage != nullptr && operatorMessage[0] != '\0') {
        return {
            (code >= ReasonCodeV2::RC_SAFETY_LIMIT_POWER &&
             code <= ReasonCodeV2::RC_SAFETY_RESET_COMPLETED)
                ? "danger"
                : (completedSuccessfully ? "good" : "warn"),
            reasonCodeLabel(code),
            operatorMessage,
            (code >= ReasonCodeV2::RC_SAFETY_LIMIT_POWER &&
             code <= ReasonCodeV2::RC_SAFETY_RESET_COMPLETED)
                ? "Сначала устраните причину safety-события и только потом повторяйте запуск."
                : "Используйте это сообщение как главный контекст при разборе текущего состояния процесса."
        };
    }

    switch (code) {
        case ReasonCodeV2::NONE:
            return {
                completedSuccessfully ? "good" : "muted",
                completedSuccessfully ? "Цикл завершён" : "Причина ещё не зафиксирована",
                completedSuccessfully
                    ? "Процесс завершился без отдельного комментария оператора и без аварийного финала."
                    : "Автоматика ещё не публиковала осмысленную последнюю причину или переход фазы.",
                completedSuccessfully
                    ? "Ориентируйтесь на фазы, графики и итоговые показатели качества."
                    : "После первого значимого события здесь появится человекочитаемая расшифровка."
            };
        case ReasonCodeV2::RC_MODE_START_REQUEST:
            return {"good", reasonCodeLabel(code),
                    "Режим принят в работу и автоматика начала штатный сценарий запуска.",
                    "Следите за lifecycle, стабилизацией и тем, что контур выходит в рабочее окно без ограничений."};
        case ReasonCodeV2::RC_MODE_STOP_REQUEST:
        case ReasonCodeV2::RC_MANUAL_OPERATOR_STOP:
            return {"warn", reasonCodeLabel(code),
                    "Текущий сценарий был остановлен оператором или переведён к завершению вручную.",
                    "Проверьте, что нагрев, насос и отбор действительно свернулись в безопасное состояние."};
        case ReasonCodeV2::RC_PRECHECK_OK:
            return {"good", reasonCodeLabel(code),
                    "Предпусковые условия были валидны: датчики, safety и базовая телеметрия выглядели рабочими.",
                    "Можно использовать это как ориентир для следующего старта аналогичного режима."};
        case ReasonCodeV2::RC_PRECHECK_FAIL_SENSOR:
        case ReasonCodeV2::RC_SAFETY_TRIP_SENSOR:
            return {"danger", reasonCodeLabel(code),
                    "Автоматика потеряла доверие к температурным данным или свежести телеметрии.",
                    "Проверьте датчики, шины 1-Wire/I2C и не продолжайте процесс вслепую."};
        case ReasonCodeV2::RC_PRECHECK_FAIL_SAFETY_LATCH:
            return {"danger", reasonCodeLabel(code),
                    "Перед запуском уже был активен safety latch, поэтому старт заблокирован на стороне контроллера.",
                    "Сначала разберите причину trip, затем подтверждайте или сбрасывайте аварийное состояние."};
        case ReasonCodeV2::RC_HEATING_COMPLETE:
        case ReasonCodeV2::RC_STABILIZATION_TIMER_OK:
        case ReasonCodeV2::RC_POST_HEADS_STABILIZATION_COMPLETE:
        case ReasonCodeV2::RC_PURGE_COMPLETE:
        case ReasonCodeV2::RC_NBK_STEAM_READY:
        case ReasonCodeV2::RC_NBK_STABILIZATION_COMPLETE:
        case ReasonCodeV2::RC_NBK_FEED_ENABLED:
        case ReasonCodeV2::RC_TEMP_STEP_REACHED:
        case ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE:
        case ReasonCodeV2::RC_FERM_TARGET_REACHED:
            return {"good", reasonCodeLabel(code),
                    "Процесс дошёл до ожидаемой технологической точки перехода по правилам автоматики.",
                    "Используйте это как нормальный ориентир для сравнения фаз, длительности и энергопотребления."};
        case ReasonCodeV2::RC_STABILITY_WINDOW_REACHED:
            return {"good", reasonCodeLabel(code),
                    "Колонна или контур вошли в рабочее окно стабильности, достаточное для следующего шага.",
                    "Хороший момент сравнить текущий прогон с успешным baseline по времени и запасам устойчивости."};
        case ReasonCodeV2::RC_HEADS_VOLUME_REACHED:
        case ReasonCodeV2::RC_HEADS_SCORE_REACHED:
        case ReasonCodeV2::RC_BODY_TARGET_VOLUME_REACHED:
        case ReasonCodeV2::RC_BODY_END_DETECTED:
        case ReasonCodeV2::RC_TAILS_TARGET_REACHED:
        case ReasonCodeV2::RC_FINISH_COOLDOWN_COMPLETE:
        case ReasonCodeV2::RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED:
        case ReasonCodeV2::RC_DISTILLATION_END_TEMP_REACHED:
        case ReasonCodeV2::RC_DISTILLATION_TARGET_VOLUME_REACHED:
        case ReasonCodeV2::RC_NBK_FINISH_LIKELY:
            return {completedSuccessfully ? "good" : "warn", reasonCodeLabel(code),
                    "Автоматика завершила этап по объёму, score, температуре или признакам окончания продукта.",
                    "Сверьте качество фракций, фактический выход и то, насколько этот переход совпал с вашими ожиданиями."};
        case ReasonCodeV2::RC_TEMP_STEP_TIMEOUT:
            return {"warn", reasonCodeLabel(code),
                    "Температурный шаг не уложился в ожидаемое окно и был завершён по таймауту.",
                    "Проверьте мощность, теплопотери, объём загрузки и корректность температурного профиля."};
        case ReasonCodeV2::RC_SAFETY_LIMIT_POWER:
            return {"warn", reasonCodeLabel(code),
                    "Safety supervisor уже ограничивает нагрев из-за риска по процессу.",
                    "Проверьте охлаждение, давление и не наращивайте мощность, пока ограничение не исчезнет."};
        case ReasonCodeV2::RC_SAFETY_LIMIT_TAKEOFF:
            return {"warn", reasonCodeLabel(code),
                    "Автоматика временно запрещает или душит отбор, потому что процесс ещё не выглядит достаточно устойчивым.",
                    "Дождитесь рабочего окна по stability, pressure и cooling margin, не открывайте отбор вручную."};
        case ReasonCodeV2::RC_SAFETY_PHASE_BLOCKED:
            return {"warn", reasonCodeLabel(code),
                    "Переход к следующей фазе был задержан защитной логикой, потому что условия ещё неубедительны.",
                    "Сначала снимите ограничение по датчикам, flooding или охлаждению, а не форсируйте фазу."};
        case ReasonCodeV2::RC_SAFETY_ANTI_OSCILLATION_GUARD:
            return {"warn", reasonCodeLabel(code),
                    "Safety supervisor увидел дрожание indicators и временно заморозил переключения, чтобы контур снова стабилизировался.",
                    "Не форсируйте насос, мощность или смену фазы, пока не исчезнут jitter и ограничения."};
        case ReasonCodeV2::RC_SAFETY_RECOVERY_ENTERED:
        case ReasonCodeV2::RC_PHASE_RECOVERY_APPLIED:
            return {"warn", reasonCodeLabel(code),
                    "Система вошла в recovery или восстановила фазу после нестабильного участка процесса.",
                    "Дайте контуру заново стабилизироваться и не делайте резких изменений нагрева, воды и отбора."};
        case ReasonCodeV2::RC_SAFETY_RECOVERY_EXITED:
            return {"good", reasonCodeLabel(code),
                    "Recovery завершён, автоматика считает, что система вернулась в рабочее состояние.",
                    "Проверьте, что показатели действительно ровные, и только потом возвращайтесь к обычной нагрузке."};
        case ReasonCodeV2::RC_SAFETY_TRIP_PRESSURE:
            return {"danger", reasonCodeLabel(code),
                    "Процесс был аварийно остановлен из-за опасного давления.",
                    "Проверьте засоры, захлёб, клапаны и холодильник до повторного запуска."};
        case ReasonCodeV2::RC_SAFETY_TRIP_OVERHEAT:
            return {"danger", reasonCodeLabel(code),
                    "Safety логика остановила процесс из-за перегрева или опасного теплового режима.",
                    "Проверьте воду, охлаждение, нагрев и датчики температуры перед повторением сценария."};
        case ReasonCodeV2::RC_SAFETY_TRIP_POWER:
            return {"danger", reasonCodeLabel(code),
                    "Автоматика зафиксировала проблему по питанию и перевела установку в аварийный режим.",
                    "Проверьте сеть, PZEM, силовую часть и стабильность питания контроллера и нагрева."};
        case ReasonCodeV2::RC_SAFETY_TRIP_GENERIC:
            return {"danger", reasonCodeLabel(code),
                    "Процесс завершился общим safety-событием без более узкой классификации.",
                    "Смотрите alarm, журнал и состояние датчиков, чтобы установить первопричину перед следующим запуском."};
        case ReasonCodeV2::RC_SAFETY_ACKNOWLEDGED:
            return {"warn", reasonCodeLabel(code),
                    "Оператор подтвердил аварийное состояние, но это ещё не означает, что первопричина устранена.",
                    "Сверьте подтверждение с реальным состоянием датчиков и оборудования перед сбросом или рестартом."};
        case ReasonCodeV2::RC_SAFETY_RESET_COMPLETED:
            return {"good", reasonCodeLabel(code),
                    "Safety reset выполнен и контроллер снял аварийное удержание.",
                    "Перед новым стартом всё равно убедитесь, что исходная проблема действительно устранена."};
        case ReasonCodeV2::RC_OPERATOR_SERVICE_ACTION:
        case ReasonCodeV2::RC_MANUAL_OPERATOR_SWITCH:
            return {"warn", reasonCodeLabel(code),
                    "Оператор вручную вмешался в сценарий, поэтому часть дальнейшего поведения уже не является чисто автоматическим эталоном.",
                    "Учитывайте это вмешательство при сравнении прогона с успешными baseline-запусками."};
        case ReasonCodeV2::RC_PHASE_TRANSITION_INFERRED:
            return {"muted", reasonCodeLabel(code),
                    "Адаптер восстановил переход фазы по фактическому состоянию runtime, а не получил его как явное событие FSM.",
                    "Используйте это как служебный сигнал и при необходимости сверяйте фазу с history timeline."};
        case ReasonCodeV2::RC_UNSPECIFIED:
        default:
            return {completedSuccessfully ? "good" : "muted",
                    reasonCodeLabel(code),
                    "Для этой причины пока нет отдельной расширенной расшифровки, но код уже зафиксирован в v2 timeline.",
                    "Ориентируйтесь на последние фазы, diagnostics, guidance и safety timeline."};
    }
}

} // namespace ControlV2

#endif // CONTROL_V2_REASON_CODES_H
