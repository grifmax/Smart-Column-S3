// Модуль управления окном безопасности (Soft Failures)

import { runtimeMonitorState } from '../globals.js';
import { updateRuntimeStateFromStatus } from './state.js';

function getSafetyActionButton(modal) {
    return modal?.querySelector('.btn.btn-primary');
}

function getAlarmState(state) {
    return state?.currentAlarm && typeof state.currentAlarm === 'object'
        ? state.currentAlarm
        : ((state?.alarm && typeof state.alarm === 'object') ? state.alarm : null);
}

export function updateSafetyModal(state) {
    const modal = document.getElementById('safety-modal');
    const msgElem = document.getElementById('safety-modal-msg');
    
    if (!modal || !msgElem) return;

    const alarm = getAlarmState(state);
    const alarmTypeCode = Number(alarm?.typeCode ?? alarm?.type ?? 0);
    const v2Safety = state?.v2?.safety && typeof state.v2.safety === 'object' ? state.v2.safety : null;
    const actionButton = getSafetyActionButton(modal);
    if (actionButton) {
        actionButton.textContent = alarm?.resetAvailable ? '♻ СБРОСИТЬ АВАРИЮ' : '✅ ИГНОРИРОВАТЬ И ПРОДОЛЖИТЬ';
        actionButton.style.background = alarm?.resetAvailable ? '#0d6efd' : '#6c757d';
    }

    // Показываем окно только если safetyOk == false и есть активная тревога, 
    // которая еще не была подтверждена (acknowledged)
    if (state.safetyOk === false && alarm && alarmTypeCode !== 0 && (!alarm.acknowledged || alarm.resetAvailable)) {
        // Если тревога критическая (level >= 3), контроллер сам все выключил, 
        // но мы все равно показываем уведомление.
        // Если это Soft Failure (level < 3), процесс идет, и нам нужно решение.
        
        let alarmMsg = alarm.message || "Неизвестная ошибка датчика";
        if (v2Safety?.severity === 'recovery' || alarm.resetAvailable) {
            alarmMsg += "\n\nУсловия безопасности восстановлены. Теперь можно выполнить сброс аварии.";
        } else if (alarm.resetBlockedReason) {
            alarmMsg += `\n\nСброс пока недоступен: ${alarm.resetBlockedReason}`;
        } else if (alarm.acknowledged) {
            alarmMsg += "\n\nАвария подтверждена оператором. Ожидаем восстановления условий безопасности.";
        }
        msgElem.textContent = alarmMsg;
        
        if (modal.style.display === 'none') {
            modal.style.display = 'flex';
            // Можно добавить звуковой сигнал в браузере
            console.warn("Safety Alarm active:", alarmMsg);
        }
    } else {
        // Скрываем, если все ок
        if (modal.style.display !== 'none') {
            modal.style.display = 'none';
        }
    }
}

export async function acknowledgeSafety() {
    try {
        const endpoint = runtimeMonitorState?.currentAlarm?.resetAvailable
            ? '/api/safety/reset'
            : '/api/safety/ack';
        const response = await fetch(endpoint, {
            method: 'POST'
        });
        const payload = await response.json().catch(() => null);
        if (payload) {
            updateRuntimeStateFromStatus(payload);
            updateSafetyModal(runtimeMonitorState);
        }
        
        if (response.ok) {
            if (endpoint === '/api/safety/reset') {
                closeSafetyModal();
                console.log("Safety alarm reset by user");
            } else if (runtimeMonitorState?.currentAlarm?.acknowledged) {
                closeSafetyModal();
                console.log("Safety alarm acknowledged by user");
            }
        } else {
            const reason = payload?.reason || payload?.v2?.safety?.resetBlockedReason;
            alert(reason || "Операция безопасности отклонена. Проверьте состояние датчиков.");
        }
    } catch (e) {
        console.error("Error acknowledging safety:", e);
    }
}

export function closeSafetyModal() {
    const modal = document.getElementById('safety-modal');
    if (modal) modal.style.display = 'none';
}

// Делаем функции доступными глобально для кнопок в HTML (если esbuild не обернет их слишком сильно)
window.acknowledgeSafety = acknowledgeSafety;
window.closeSafetyModal = closeSafetyModal;
