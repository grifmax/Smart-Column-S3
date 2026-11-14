/**
 * Интеграция функционала ручной ректификации
 * Подключается после app.js
 */

// Импорт модулей (ES6 modules не поддерживаются в текущем билде, используем глобальные функции)

// Инициализация при загрузке страницы
document.addEventListener('DOMContentLoaded', function() {
    console.log('[Manual Rect] Initializing...');

    // Инициализация настроек ручной ректификации
    if (typeof initManualRectSettings === 'function') {
        initManualRectSettings();
        console.log('[Manual Rect] Settings initialized');
    }

    // Запуск мониторинга состояния
    startContainerChangeMonitoring();
});

// Глобальная переменная для отслеживания состояния
let lastContainerChangeState = null;

/**
 * Запуск мониторинга состояния смены тары
 */
function startContainerChangeMonitoring() {
    console.log('[Manual Rect] Container change monitoring started');

    // Проверяем состояние каждые 2 секунды
    setInterval(checkContainerChangeState, 2000);
}

/**
 * Проверка состояния смены тары через API статуса
 */
async function checkContainerChangeState() {
    try {
        const response = await fetch('/api/status');
        if (!response.ok) {
            return;
        }

        const status = await response.json();

        // Проверяем, находимся ли мы в режиме MANUAL_RECT
        if (status.mode !== 'MANUAL_RECT' && status.modeStr !== 'MANUAL_RECT') {
            // Если модальное окно открыто, закрываем его
            if (lastContainerChangeState) {
                hideContainerChangeModal();
                lastContainerChangeState = null;
            }
            return;
        }

        // Проверяем флаг manualRectContainerChangePending
        const isPending = status.manualRectContainerChangePending === true;
        const pendingPhaseStr = status.manualRectPendingPhaseStr ||
                                getPhaseName(status.manualRectPendingPhase);

        // Если состояние изменилось с false на true - показываем модальное окно
        if (isPending && !lastContainerChangeState) {
            console.log('[Manual Rect] Container change pending:', pendingPhaseStr);
            showContainerChangeModal(pendingPhaseStr);
            lastContainerChangeState = true;
        }

        // Если состояние изменилось с true на false - скрываем модальное окно
        if (!isPending && lastContainerChangeState) {
            console.log('[Manual Rect] Container change completed');
            hideContainerChangeModal();
            lastContainerChangeState = false;
        }

    } catch (e) {
        console.error('[Manual Rect] Failed to check container change state:', e);
    }
}

/**
 * Получить название фазы по номеру
 */
function getPhaseName(phase) {
    const phaseNames = {
        0: 'IDLE',
        1: 'Головы',
        2: 'Тело',
        3: 'Хвосты'
    };
    return phaseNames[phase] || 'Неизвестная фракция';
}

/**
 * Показать модальное окно смены тары
 */
function showContainerChangeModal(pendingPhaseStr) {
    // Если функция из модуля доступна, используем её
    if (typeof window.showContainerChangeModal === 'function') {
        window.showContainerChangeModal(pendingPhaseStr);
        return;
    }

    // Иначе создаём модальное окно вручную
    let modal = document.querySelector('.container-change-modal');
    if (modal) {
        return; // Уже показано
    }

    modal = document.createElement('div');
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

    // Обработчик кнопки подтверждения
    const confirmBtn = modal.querySelector('#confirm-container-btn');
    if (confirmBtn) {
        confirmBtn.addEventListener('click', confirmContainerChange);
    }

    // Звуковой сигнал
    playContainerChangeSound();
}

/**
 * Скрыть модальное окно смены тары
 */
function hideContainerChangeModal() {
    // Если функция из модуля доступна, используем её
    if (typeof window.hideContainerChangeModal === 'function') {
        window.hideContainerChangeModal();
        return;
    }

    const modal = document.querySelector('.container-change-modal');
    if (modal) {
        modal.remove();
    }
}

/**
 * Подтвердить смену тары
 */
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
            if (typeof addLog === 'function') {
                addLog('✅ Смена тары подтверждена. Отбор возобновлён.', 'info');
            } else {
                alert('✅ Смена тары подтверждена. Отбор возобновлён.');
            }

            // Обновить статус
            if (typeof loadStatus === 'function') {
                setTimeout(() => loadStatus(), 500);
            }
        } else {
            throw new Error(result.message || 'Unknown error');
        }
    } catch (e) {
        console.error('[Manual Rect] Failed to confirm container change:', e);
        alert('❌ Ошибка подтверждения: ' + e.message);

        // Восстановить кнопку
        if (confirmBtn) {
            confirmBtn.disabled = false;
            confirmBtn.textContent = '✅ Подтвердить смену тары';
        }
    }
}

/**
 * Воспроизвести звуковой сигнал
 */
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
        console.warn('[Manual Rect] Failed to play sound:', e);
    }
}

/**
 * Инициализация настроек ручной ректификации
 */
async function initManualRectSettings() {
    try {
        await loadManualRectSettings();
        attachManualRectEventListeners();
    } catch (e) {
        console.error('[Manual Rect] Failed to initialize settings:', e);
    }
}

/**
 * Загрузка настроек ручной ректификации
 */
async function loadManualRectSettings() {
    try {
        const response = await fetch('/api/settings/manual-rect');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        const settings = await response.json();
        updateManualRectUI(settings);
    } catch (e) {
        console.error('[Manual Rect] Failed to load settings:', e);
    }
}

/**
 * Обновление UI настроек
 */
function updateManualRectUI(settings) {
    const fields = {
        'mrect-heads-power': settings.headsPowerW || 2500,
        'mrect-body-power': settings.bodyPowerW || 2000,
        'mrect-tails-power': settings.tailsPowerW || 1500,
        'mrect-heads-speed': settings.headsSpeedMlH || 300,
        'mrect-body-speed': settings.bodySpeedMlH || 600,
        'mrect-heads-volume': settings.headsTargetVolumeMl || 0,
        'mrect-body-volume': settings.bodyTargetVolumeMl || 0,
        'mrect-tails-volume': settings.tailsTargetVolumeMl || 0
    };

    for (const [id, value] of Object.entries(fields)) {
        const el = document.getElementById(id);
        if (el) {
            el.value = value;
        }
    }

    const soundCheckbox = document.getElementById('mrect-sound-enabled');
    if (soundCheckbox) {
        soundCheckbox.checked = settings.fractionChangeSoundEnabled !== false;
    }
}

/**
 * Подключение обработчиков событий
 */
function attachManualRectEventListeners() {
    const saveBtn = document.getElementById('mrect-save-btn');
    if (saveBtn) {
        saveBtn.addEventListener('click', saveManualRectSettings);
    }

    const resetBtn = document.getElementById('mrect-reset-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', () => {
            if (confirm('Сбросить настройки к значениям по умолчанию?')) {
                updateManualRectUI({
                    headsPowerW: 2500,
                    bodyPowerW: 2000,
                    tailsPowerW: 1500,
                    headsSpeedMlH: 300,
                    bodySpeedMlH: 600,
                    headsTargetVolumeMl: 0,
                    bodyTargetVolumeMl: 0,
                    tailsTargetVolumeMl: 0,
                    fractionChangeSoundEnabled: true
                });
            }
        });
    }
}

/**
 * Сохранение настроек ручной ректификации
 */
async function saveManualRectSettings() {
    const settings = {
        headsPowerW: parseFloat(document.getElementById('mrect-heads-power').value),
        bodyPowerW: parseFloat(document.getElementById('mrect-body-power').value),
        tailsPowerW: parseFloat(document.getElementById('mrect-tails-power').value),
        headsSpeedMlH: parseFloat(document.getElementById('mrect-heads-speed').value),
        bodySpeedMlH: parseFloat(document.getElementById('mrect-body-speed').value),
        headsTargetVolumeMl: parseFloat(document.getElementById('mrect-heads-volume').value),
        bodyTargetVolumeMl: parseFloat(document.getElementById('mrect-body-volume').value),
        tailsTargetVolumeMl: parseFloat(document.getElementById('mrect-tails-volume').value),
        fractionChangeSoundEnabled: document.getElementById('mrect-sound-enabled').checked
    };

    // Валидация
    if (settings.headsPowerW < 100 || settings.headsPowerW > 10000) {
        alert('❌ Мощность голов должна быть от 100 до 10000 Вт');
        return;
    }
    if (settings.bodyPowerW < 100 || settings.bodyPowerW > 10000) {
        alert('❌ Мощность тела должна быть от 100 до 10000 Вт');
        return;
    }
    if (settings.tailsPowerW < 100 || settings.tailsPowerW > 10000) {
        alert('❌ Мощность хвостов должна быть от 100 до 10000 Вт');
        return;
    }
    if (settings.headsSpeedMlH < 10 || settings.headsSpeedMlH > 5000) {
        alert('❌ Скорость голов должна быть от 10 до 5000 мл/ч');
        return;
    }
    if (settings.bodySpeedMlH < 10 || settings.bodySpeedMlH > 5000) {
        alert('❌ Скорость тела должна быть от 10 до 5000 мл/ч');
        return;
    }

    try {
        const response = await fetch('/api/settings/manual-rect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(settings)
        });

        const result = await response.json();

        if (response.ok && result.success) {
            alert('✅ Настройки сохранены');

            // Логируем в системный журнал
            if (typeof addLog === 'function') {
                addLog('Настройки ручной ректификации сохранены', 'info');
            }
        } else {
            throw new Error(result.error || 'Unknown error');
        }
    } catch (e) {
        console.error('[Manual Rect] Failed to save settings:', e);
        alert('❌ Ошибка сохранения: ' + e.message);
    }
}

// Экспорт функций в глобальную область
window.initManualRectSettings = initManualRectSettings;
window.showContainerChangeModal = showContainerChangeModal;
window.hideContainerChangeModal = hideContainerChangeModal;
window.confirmContainerChange = confirmContainerChange;

console.log('[Manual Rect] Integration module loaded');
