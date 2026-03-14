// Модуль управления окном безопасности (Soft Failures)

export function updateSafetyModal(state) {
    const modal = document.getElementById('safety-modal');
    const msgElem = document.getElementById('safety-modal-msg');
    
    if (!modal || !msgElem) return;

    // Показываем окно только если safetyOk == false и есть активная тревога, 
    // которая еще не была подтверждена (acknowledged)
    if (state.safetyOk === false && state.currentAlarm && state.currentAlarm.type !== 0) {
        // Если тревога критическая (level >= 3), контроллер сам все выключил, 
        // но мы все равно показываем уведомление.
        // Если это Soft Failure (level < 3), процесс идет, и нам нужно решение.
        
        const alarmMsg = state.currentAlarm.message || "Неизвестная ошибка датчика";
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
        const response = await fetch('/api/safety/ack', {
            method: 'POST'
        });
        
        if (response.ok) {
            closeSafetyModal();
            console.log("Safety alarm acknowledged by user");
        } else {
            alert("Не удалось подтвердить аварию. Возможно, датчик все еще неисправен.");
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
