// Модуль управления окном безопасности (Soft Failures)

import { runtimeMonitorState } from '../globals.js';
import { updateRuntimeStateFromStatus } from './state.js';
import { deriveSafetyUiState } from './safety-state.js';

function getSafetyActionButton(modal) {
    return modal?.querySelector('.btn.btn-primary');
}

export function updateSafetyModal(state) {
    const modal = document.getElementById('safety-modal');
    const msgElem = document.getElementById('safety-modal-msg');
    
    if (!modal || !msgElem) return;

    const safetyState = deriveSafetyUiState(state);
    const actionButton = getSafetyActionButton(modal);
    if (actionButton) {
        actionButton.textContent = safetyState.primaryActionLabel;
        actionButton.style.background = safetyState.primaryActionBackground;
    }

    // Показываем окно только если safetyOk == false и есть активная тревога, 
    // которая еще не была подтверждена (acknowledged)
    if (safetyState.shouldShowModal) {
        // Если тревога критическая (level >= 3), контроллер сам все выключил, 
        // но мы все равно показываем уведомление.
        // Если это Soft Failure (level < 3), процесс идет, и нам нужно решение.
        
        let alarmMsg = safetyState.message || "Неизвестная ошибка датчика";
        if (safetyState.severity === 'recovery' || safetyState.resetAvailable) {
            alarmMsg += "\n\nУсловия безопасности восстановлены. Теперь можно выполнить сброс аварии.";
        } else if (safetyState.resetBlockedReason) {
            alarmMsg += `\n\nСброс пока недоступен: ${safetyState.resetBlockedReason}`;
        } else if (safetyState.acknowledged) {
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
        const currentSafety = deriveSafetyUiState(runtimeMonitorState);
        const endpoint = currentSafety.resetAvailable
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
        const nextSafety = deriveSafetyUiState(runtimeMonitorState);
        
        if (response.ok) {
            if (endpoint === '/api/safety/reset') {
                closeSafetyModal();
                console.log("Safety alarm reset by user");
            } else if (nextSafety.acknowledged) {
                closeSafetyModal();
                console.log("Safety alarm acknowledged by user");
            }
        } else {
            const reason = payload?.reason || nextSafety.resetBlockedReason;
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
