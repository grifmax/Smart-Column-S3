import { runtimeMonitorState, MODE_IDLE, getModeLabel } from '../globals.js';
import { showNotification } from '../core/notifications.js';

const REASON_LABELS = {
    RC_MODE_START_REQUEST: 'Запуск подтвержден оператором',
    RC_MODE_STOP_REQUEST: 'Остановлено оператором',
    RC_PRECHECK_OK: 'Проверки перед запуском пройдены',
    RC_PRECHECK_FAIL_SENSOR: 'Предпусковая проверка датчиков не пройдена',
    RC_PRECHECK_FAIL_SAFETY_LATCH: 'Есть защелкнутая авария безопасности',
    RC_HEATING_COMPLETE: 'Нагрев завершен',
    RC_STABILIZATION_TIMER_OK: 'Выдержка стабилизации завершена',
    RC_STABILITY_WINDOW_REACHED: 'Достигнуто окно стабильности',
    RC_HEADS_VOLUME_REACHED: 'Отбор голов завершен по объему',
    RC_HEADS_SCORE_REACHED: 'Отбор голов завершен по индикаторам',
    RC_POST_HEADS_STABILIZATION_COMPLETE: 'Пост-стабилизация завершена',
    RC_PURGE_COMPLETE: 'Продувка завершена',
    RC_BODY_TARGET_VOLUME_REACHED: 'Отбор тела завершен по объему',
    RC_BODY_END_DETECTED: 'Обнаружен конец тела',
    RC_TAILS_TARGET_REACHED: 'Отбор хвостов завершен',
    RC_FINISH_COOLDOWN_COMPLETE: 'Финишное охлаждение завершено',
    RC_DISTILLATION_HEADS_OPTIONAL_SKIPPED: 'Этап голов пропущен',
    RC_DISTILLATION_END_TEMP_REACHED: 'Достигнута конечная температура',
    RC_DISTILLATION_TARGET_VOLUME_REACHED: 'Достигнут целевой объем',
    RC_NBK_STEAM_READY: 'Колонна готова к паровой работе',
    RC_NBK_STABILIZATION_COMPLETE: 'Стабилизация НБК завершена',
    RC_NBK_FEED_ENABLED: 'Подача браги разрешена',
    RC_NBK_FINISH_LIKELY: 'Вероятно достигнут финиш процесса',
    RC_TEMP_STEP_REACHED: 'Температурная ступень достигнута',
    RC_TEMP_STEP_HOLD_COMPLETE: 'Выдержка температурной ступени завершена',
    RC_TEMP_STEP_TIMEOUT: 'Ступень завершена по таймауту',
    RC_FERM_TARGET_REACHED: 'Целевая температура ферментации достигнута',
    RC_SAFETY_LIMIT_POWER: 'Ограничение мощности по безопасности',
    RC_SAFETY_LIMIT_TAKEOFF: 'Ограничение отбора по безопасности',
    RC_SAFETY_PHASE_BLOCKED: 'Переход фазы заблокирован безопасностью',
    RC_SAFETY_RECOVERY_ENTERED: 'Условия безопасности восстановлены',
    RC_SAFETY_RECOVERY_EXITED: 'Режим восстановления завершен',
    RC_SAFETY_TRIP_PRESSURE: 'Авария по давлению',
    RC_SAFETY_TRIP_SENSOR: 'Авария по датчикам',
    RC_SAFETY_TRIP_OVERHEAT: 'Авария по перегреву',
    RC_SAFETY_TRIP_POWER: 'Авария по питанию',
    RC_SAFETY_TRIP_GENERIC: 'Неидентифицированная авария',
    RC_SAFETY_ACKNOWLEDGED: 'Авария подтверждена оператором',
    RC_SAFETY_RESET_COMPLETED: 'Авария сброшена оператором',
    RC_MANUAL_OPERATOR_SWITCH: 'Ручное переключение оператором',
    RC_MANUAL_OPERATOR_STOP: 'Ручная остановка оператором',
    RC_PHASE_TRANSITION_INFERRED: 'Переход фазы восстановлен системой'
};

let prevMode = null;
let prevPhase = null;
let prevReasonCode = null;
let prevOperatorMessage = null;

function formatReasonCode(reasonCode) {
    if (!reasonCode || reasonCode === 'RC_NONE' || reasonCode === 'RC_UNSPECIFIED') {
        return '';
    }
    if (REASON_LABELS[reasonCode]) {
        return REASON_LABELS[reasonCode];
    }
    return reasonCode
        .replace(/^RC_/, '')
        .toLowerCase()
        .split('_')
        .map((chunk) => chunk ? chunk[0].toUpperCase() + chunk.slice(1) : '')
        .join(' ');
}

function buildNotificationBody(lines) {
    return lines.filter(Boolean).join('\n');
}

function getTransitionDetail() {
    const operatorMessage = String(runtimeMonitorState?.v2?.operatorMessage || '').trim();
    if (operatorMessage) {
        return operatorMessage;
    }
    return formatReasonCode(runtimeMonitorState?.v2?.lastReasonCode || 'RC_NONE');
}

export function updateProcessNotifications() {
    const mode = Number(runtimeMonitorState?.mode ?? MODE_IDLE);
    const phase = String(runtimeMonitorState?.phaseStr || '');
    const reasonCode = String(runtimeMonitorState?.v2?.lastReasonCode || 'RC_NONE');
    const operatorMessage = String(runtimeMonitorState?.v2?.operatorMessage || '');

    if (prevMode === null) {
        prevMode = mode;
        prevPhase = phase;
        prevReasonCode = reasonCode;
        prevOperatorMessage = operatorMessage;
        return;
    }

    const detail = getTransitionDetail();

    if (prevMode !== mode) {
        if (mode === MODE_IDLE) {
            showNotification('Процесс завершён', {
                body: buildNotificationBody([
                    `Режим: ${getModeLabel(prevMode)}`,
                    detail
                ])
            });
        } else if (prevMode === MODE_IDLE) {
            showNotification('Процесс запущен', {
                body: buildNotificationBody([
                    `Режим: ${getModeLabel(mode)}`,
                    detail
                ])
            });
        } else {
            showNotification('Смена режима', {
                body: buildNotificationBody([
                    `Режим: ${getModeLabel(mode)}`,
                    detail
                ])
            });
        }
    } else if (mode !== MODE_IDLE && phase && phase !== '-' && prevPhase !== phase) {
        showNotification('Смена этапа', {
            body: buildNotificationBody([
                `Новый этап: ${phase}`,
                detail
            ])
        });
    } else if (mode !== MODE_IDLE && prevReasonCode !== reasonCode && detail && detail !== prevOperatorMessage) {
        showNotification('Изменение процесса', {
            body: buildNotificationBody([
                `Режим: ${getModeLabel(mode)}`,
                detail
            ])
        });
    }

    prevMode = mode;
    prevPhase = phase;
    prevReasonCode = reasonCode;
    prevOperatorMessage = operatorMessage;
}
