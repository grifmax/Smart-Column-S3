/**
 * Модальное окно смены тары для ручной ректификации
 */

let containerChangeModal = null;
let soundPlayed = false;

export function showContainerChangeModal(pendingPhaseStr) {
    // Если окно уже показано, не показываем повторно
    if (containerChangeModal) {
        return;
    }

    const modal = document.createElement('div');
    modal.className = 'modal-overlay container-change-modal';
    modal.innerHTML = `
        <div class="modal-dialog">
            <div class="modal-header">
                <h3>🚨 Смена тары</h3>
            </div>
            <div class="modal-body">
                <p class="modal-alert"><strong>⚠️ Отбор остановлен!</strong></p>
                <p class="modal-text">
                    Целевой объем фракции достигнут.<br>
                    Смените тару для следующей фракции: <strong>${pendingPhaseStr}</strong>
                </p>
                <p class="modal-instruction">
                    После смены тары нажмите кнопку "Подтвердить" для продолжения отбора.
                </p>
            </div>
            <div class="modal-footer">
                <button class="btn btn-primary" id="confirm-container-btn">
                    ✅ Подтвердить смену тары
                </button>
            </div>
        </div>
    `;

    document.body.appendChild(modal);
    containerChangeModal = modal;

    // Обработчик кнопки подтверждения
    const confirmBtn = modal.querySelector('#confirm-container-btn');
    confirmBtn.addEventListener('click', confirmContainerChange);

    // Звуковой сигнал
    if (!soundPlayed) {
        playContainerChangeSound();
        soundPlayed = true;
    }
}

export function hideContainerChangeModal() {
    if (containerChangeModal) {
        containerChangeModal.remove();
        containerChangeModal = null;
        soundPlayed = false;
    }
}

async function confirmContainerChange() {
    const confirmBtn = document.querySelector('#confirm-container-btn');
    if (confirmBtn) {
        confirmBtn.disabled = true;
        confirmBtn.textContent = '⏳ Подтверждение...';
    }

    try {
        const response = await fetch('/api/process/manual-rect/confirm-container', {
            method: 'POST'
        });

        const result = await response.json();

        if (response.ok && result.success) {
            hideContainerChangeModal();

            // Показываем уведомление
            if (window.addLog) {
                window.addLog('✅ Смена тары подтверждена. Отбор возобновлён.', 'info');
            } else {
                alert('✅ Смена тары подтверждена. Отбор возобновлён.');
            }

            // Обновить статус
            if (window.loadStatus) {
                setTimeout(() => window.loadStatus(), 500);
            }
        } else {
            throw new Error(result.message || 'Unknown error');
        }
    } catch (e) {
        console.error('Failed to confirm container change:', e);
        alert('❌ Ошибка подтверждения: ' + e.message);

        // Восстановить кнопку
        if (confirmBtn) {
            confirmBtn.disabled = false;
            confirmBtn.textContent = '✅ Подтвердить смену тары';
        }
    }
}

function playContainerChangeSound() {
    try {
        // Простой звуковой сигнал через Web Audio API
        const audioContext = new (window.AudioContext || window.webkitAudioContext)();
        const oscillator = audioContext.createOscillator();
        const gainNode = audioContext.createGain();

        oscillator.connect(gainNode);
        gainNode.connect(audioContext.destination);

        oscillator.frequency.value = 800; // Частота 800 Hz
        oscillator.type = 'sine';

        gainNode.gain.setValueAtTime(0.3, audioContext.currentTime);
        gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.5);

        oscillator.start(audioContext.currentTime);
        oscillator.stop(audioContext.currentTime + 0.5);

        // Второй звуковой сигнал через 0.6 секунды
        setTimeout(() => {
            const oscillator2 = audioContext.createOscillator();
            const gainNode2 = audioContext.createGain();

            oscillator2.connect(gainNode2);
            gainNode2.connect(audioContext.destination);

            oscillator2.frequency.value = 1000; // Частота 1000 Hz
            oscillator2.type = 'sine';

            gainNode2.gain.setValueAtTime(0.3, audioContext.currentTime);
            gainNode2.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.5);

            oscillator2.start(audioContext.currentTime);
            oscillator2.stop(audioContext.currentTime + 0.5);
        }, 600);

    } catch (e) {
        console.warn('Failed to play sound:', e);
    }
}

// Экспорт для использования из других модулей
window.showContainerChangeModal = showContainerChangeModal;
window.hideContainerChangeModal = hideContainerChangeModal;
