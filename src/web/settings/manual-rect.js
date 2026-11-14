/**
 * Настройки ручной ректификации
 */

let currentSettings = null;

export async function initManualRectSettings() {
    await loadManualRectSettings();
    attachEventListeners();
}

async function loadManualRectSettings() {
    try {
        const response = await fetch('/api/settings/manual-rect');
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }

        currentSettings = await response.json();
        updateUI(currentSettings);
    } catch (e) {
        console.error('Failed to load manual rect settings:', e);
        alert('❌ Ошибка загрузки настроек: ' + e.message);
    }
}

function updateUI(settings) {
    // Мощность
    document.getElementById('mrect-heads-power').value = settings.headsPowerW || 2500;
    document.getElementById('mrect-body-power').value = settings.bodyPowerW || 2000;
    document.getElementById('mrect-tails-power').value = settings.tailsPowerW || 1500;

    // Скорость
    document.getElementById('mrect-heads-speed').value = settings.headsSpeedMlH || 300;
    document.getElementById('mrect-body-speed').value = settings.bodySpeedMlH || 600;

    // Целевые объемы
    document.getElementById('mrect-heads-volume').value = settings.headsTargetVolumeMl || 0;
    document.getElementById('mrect-body-volume').value = settings.bodyTargetVolumeMl || 0;
    document.getElementById('mrect-tails-volume').value = settings.tailsTargetVolumeMl || 0;

    // Звуковой сигнал
    document.getElementById('mrect-sound-enabled').checked = settings.fractionChangeSoundEnabled !== false;
}

function attachEventListeners() {
    const saveBtn = document.getElementById('mrect-save-btn');
    if (saveBtn) {
        saveBtn.addEventListener('click', saveManualRectSettings);
    }

    const resetBtn = document.getElementById('mrect-reset-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', () => {
            if (confirm('Сбросить настройки к значениям по умолчанию?')) {
                updateUI({
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
            currentSettings = settings;
            alert('✅ Настройки сохранены');
        } else {
            throw new Error(result.error || 'Unknown error');
        }
    } catch (e) {
        console.error('Failed to save manual rect settings:', e);
        alert('❌ Ошибка сохранения: ' + e.message);
    }
}

// Экспорт для использования из HTML
window.initManualRectSettings = initManualRectSettings;
