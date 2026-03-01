import { addLog } from '../core/logs.js';

const API_BASE = '/api/calibration';

let calibrationState = {
    running: false,
    startTime: null,
    targetTime: 0,
    targetVolume: 0,
    speed: 0,
    interval: null
};

function byId(id) {
    return document.getElementById(id);
}

function setMessage(id, message, type = 'info') {
    const el = byId(id);
    if (!el) return;
    const color = type === 'error'
        ? 'var(--danger, #dc3545)'
        : type === 'success'
            ? 'var(--success, #28a745)'
            : 'var(--text-secondary)';
    el.style.color = color;
    el.textContent = message;
}

function resetCalibrationUi() {
    calibrationState = {
        running: false,
        startTime: null,
        targetTime: 0,
        targetVolume: 0,
        speed: 0,
        interval: null
    };

    const startBtn = byId('btn-start-cal');
    const stopBtn = byId('btn-stop-cal');
    const applyBtn = byId('btn-apply-cal');
    const cancelBtn = byId('btn-cancel-cal');
    const progressWrap = byId('cal-progress-container');
    const manualWrap = byId('cal-manual-volume');
    const progressBar = byId('cal-progress-bar');
    const actualVolume = byId('actual-volume');

    if (startBtn) startBtn.style.display = '';
    if (stopBtn) stopBtn.style.display = 'none';
    if (applyBtn) applyBtn.style.display = 'none';
    if (cancelBtn) cancelBtn.style.display = 'none';
    if (progressWrap) progressWrap.style.display = 'none';
    if (manualWrap) manualWrap.style.display = 'none';
    if (progressBar) {
        progressBar.style.width = '0%';
        progressBar.textContent = '';
    }
    if (actualVolume) actualVolume.value = '';
}

export function updateCalibrationTime() {
    const speed = Number(byId('cal-speed')?.value);
    const volume = Number(byId('cal-volume')?.value);
    if (!Number.isFinite(speed) || speed <= 0 || !Number.isFinite(volume) || volume <= 0) return;

    const timeMinutes = (volume / speed) * 60;
    const minutes = Math.floor(timeMinutes);
    const seconds = Math.round((timeMinutes % 1) * 60);

    const out = byId('cal-time');
    if (!out) return;
    out.textContent = minutes >= 1
        ? `${minutes} мин ${seconds} сек`
        : `${Math.max(1, Math.round(timeMinutes * 60))} сек`;
}

export async function loadCalibrationData() {
    const tab = byId('equipment');
    if (!tab) return;

    try {
        const response = await fetch(API_BASE);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const data = await response.json();
        const pump = data?.pump || {};

        const pumpCurrent = byId('pumpCurrent');
        if (pumpCurrent) {
            const mlPerRev = Number(pump.mlPerRev);
            const stepsPerRev = Number(pump.stepsPerRev);
            const microsteps = Number(pump.microsteps || 1);
            if (Number.isFinite(mlPerRev)) {
                pumpCurrent.textContent = `${mlPerRev.toFixed(3)} мл/оборот (${stepsPerRev || 0} шагов/об × ${microsteps})`;
            } else {
                pumpCurrent.textContent = 'Нет данных';
            }
        }

        const mlPerRevEl = byId('pump-ml-per-rev');
        if (mlPerRevEl && Number.isFinite(Number(pump.mlPerRev))) {
            mlPerRevEl.value = Number(pump.mlPerRev).toFixed(3);
        }

        const stepsPerRevEl = byId('pump-steps-per-rev');
        if (stepsPerRevEl) {
            const totalSteps = Number(pump.stepsPerRev || 0) * Number(pump.microsteps || 1);
            if (Number.isFinite(totalSteps) && totalSteps > 0) stepsPerRevEl.value = String(totalSteps);
        }

        const sensorList = byId('sensorList');
        if (sensorList) {
            sensorList.innerHTML = '';
            const sensors = Array.isArray(data?.temperatures) ? data.temperatures : [];
            const names = ['Куб', 'Царга низ', 'Царга верх', 'Дефлегматор', 'ТСА', 'Вода вход', 'Вода выход'];

            if (!sensors.length) {
                sensorList.innerHTML = '<li class="info-text">Датчики не найдены</li>';
            } else {
                sensors.forEach((temp, i) => {
                    const name = names[temp.index] || `Датчик ${temp.index}`;
                    const current = Number(temp.current);
                    const offset = Number(temp.offset);
                    const validBadge = temp.valid ? '✓ OK' : '✗ Ошибка';

                    const li = document.createElement('li');
                    li.className = 'card';
                    li.style.marginBottom = '10px';
                    li.innerHTML = `
                        <div style="display:flex;justify-content:space-between;gap:10px;align-items:center;flex-wrap:wrap;">
                            <strong>${name}</strong>
                            <span class="info-text">${validBadge}</span>
                        </div>
                        <div class="info-text" style="margin:6px 0;">Адрес: ${temp.address || '—'} | Текущее: ${Number.isFinite(current) ? current.toFixed(2) : '--'} °C | Смещение: ${Number.isFinite(offset) ? offset.toFixed(2) : '0.00'} °C</div>
                        <div style="display:flex;gap:8px;flex-wrap:wrap;align-items:center;">
                            <input type="number" id="offset_${i}" value="${Number.isFinite(offset) ? offset.toFixed(2) : '0.00'}" step="0.01" style="width:110px;">
                            <button class="btn btn-sm" onclick="calibrateTempOffset(${i})">Смещение</button>
                            <input type="number" id="ref_${i}" step="0.1" placeholder="Эталон °C" style="width:120px;">
                            <button class="btn btn-sm btn-secondary" onclick="calibrateTempReference(${i})">По эталону</button>
                        </div>
                    `;
                    sensorList.appendChild(li);
                });
            }
        }
    } catch (error) {
        console.error('loadCalibrationData error:', error);
        setMessage('tempResult', 'Ошибка загрузки данных калибровки', 'error');
    }
}

export async function scanCalibrationSensors() {
    try {
        const response = await fetch(`${API_BASE}/scan`);
        const data = await response.json();
        if (!response.ok) throw new Error(data?.error || `HTTP ${response.status}`);
        setMessage('tempResult', `Найдено датчиков: ${Number(data.count) || 0}`, 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('tempResult', `Ошибка сканирования: ${error.message}`, 'error');
    }
}

export async function calibrateTempOffset(index) {
    const offset = Number(byId(`offset_${index}`)?.value);
    if (!Number.isFinite(offset)) {
        setMessage('tempResult', 'Введите корректное смещение', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/temp`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ index, offset })
        });

        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        setMessage('tempResult', `Смещение датчика ${index} сохранено`, 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('tempResult', `Ошибка калибровки: ${error.message}`, 'error');
    }
}

export async function calibrateTempReference(index) {
    const reference = Number(byId(`ref_${index}`)?.value);
    if (!Number.isFinite(reference)) {
        setMessage('tempResult', 'Введите эталонную температуру', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/temp`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ index, reference })
        });

        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const data = await response.json();
        setMessage('tempResult', `Датчик ${index}: смещение ${Number(data.offset || 0).toFixed(2)} °C`, 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('tempResult', `Ошибка калибровки: ${error.message}`, 'error');
    }
}

export async function startCalibration() {
    const speed = Number(byId('cal-speed')?.value);
    const volume = Number(byId('cal-volume')?.value);

    if (!Number.isFinite(speed) || speed <= 0 || !Number.isFinite(volume) || volume <= 0) {
        setMessage('pumpResult', 'Проверьте скорость и объём калибровки', 'error');
        return;
    }

    calibrationState = {
        running: true,
        startTime: Date.now(),
        targetTime: (volume / speed) * 3600000,
        targetVolume: volume,
        speed,
        interval: null
    };

    const startBtn = byId('btn-start-cal');
    const stopBtn = byId('btn-stop-cal');
    const applyBtn = byId('btn-apply-cal');
    const cancelBtn = byId('btn-cancel-cal');
    const progressWrap = byId('cal-progress-container');
    const totalEl = byId('cal-total');

    if (startBtn) startBtn.style.display = 'none';
    if (stopBtn) stopBtn.style.display = '';
    if (applyBtn) applyBtn.style.display = 'none';
    if (cancelBtn) cancelBtn.style.display = '';
    if (progressWrap) progressWrap.style.display = '';
    if (totalEl) totalEl.textContent = `${Math.max(1, Math.floor(calibrationState.targetTime / 60000))} мин`;

    try {
        const response = await fetch('/api/pump/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ speed })
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        setMessage('pumpResult', `Калибровка запущена: ${volume} мл @ ${speed} мл/ч`, 'success');
        addLog(`Калибровка насоса стартовала (${speed} мл/ч)`, 'info');

        calibrationState.interval = setInterval(() => {
            if (!calibrationState.running) return;
            const elapsed = Date.now() - calibrationState.startTime;
            const progress = Math.min(100, (elapsed / calibrationState.targetTime) * 100);

            const bar = byId('cal-progress-bar');
            const elapsedEl = byId('cal-elapsed');
            if (bar) {
                bar.style.width = `${progress.toFixed(0)}%`;
                bar.textContent = `${progress.toFixed(0)}%`;
            }
            if (elapsedEl) {
                const sec = Math.floor(elapsed / 1000);
                elapsedEl.textContent = `${Math.floor(sec / 60)} мин ${sec % 60} сек`;
            }

            if (elapsed >= calibrationState.targetTime) {
                stopCalibration(true);
            }
        }, 1000);
    } catch (error) {
        setMessage('pumpResult', `Ошибка запуска калибровки: ${error.message}`, 'error');
        resetCalibrationUi();
    }
}

export async function stopCalibration(autoStop = false) {
    if (!calibrationState.running) return;

    calibrationState.running = false;
    if (calibrationState.interval) {
        clearInterval(calibrationState.interval);
        calibrationState.interval = null;
    }

    try {
        await fetch('/api/pump/stop', { method: 'POST' });
    } catch {
        // ignore
    }

    const stopBtn = byId('btn-stop-cal');
    const applyBtn = byId('btn-apply-cal');
    const manualWrap = byId('cal-manual-volume');
    const actualVolume = byId('actual-volume');

    if (stopBtn) stopBtn.style.display = 'none';
    if (applyBtn) applyBtn.style.display = '';

    if (autoStop) {
        if (actualVolume) actualVolume.value = String(calibrationState.targetVolume);
        setMessage('pumpResult', 'Калибровка завершена. Нажмите «Применить».', 'success');
    } else {
        if (manualWrap) manualWrap.style.display = '';
        setMessage('pumpResult', 'Калибровка остановлена. Укажите фактический объём и примените.', 'info');
    }
}

export async function applyCalibration() {
    const actualVolume = Number(byId('actual-volume')?.value);
    if (!Number.isFinite(actualVolume) || actualVolume <= 0) {
        setMessage('pumpResult', 'Введите фактически налитый объём', 'error');
        return;
    }

    const elapsedMs = Math.max(1000, Date.now() - Number(calibrationState.startTime || Date.now()));

    try {
        const response = await fetch(`${API_BASE}/pump`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                knownVolume: actualVolume,
                elapsedMs,
                targetSpeed: calibrationState.speed
            })
        });

        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const data = await response.json();
        const mlPerRev = Number(data?.mlPerRev);

        setMessage('pumpResult', Number.isFinite(mlPerRev)
            ? `Калибровка применена: ${mlPerRev.toFixed(3)} мл/оборот`
            : 'Калибровка применена', 'success');

        addLog('Калибровка насоса применена', 'success');
        resetCalibrationUi();
        await loadCalibrationData();
    } catch (error) {
        setMessage('pumpResult', `Ошибка применения: ${error.message}`, 'error');
    }
}

export async function cancelCalibration() {
    if (calibrationState.running) {
        try {
            await fetch('/api/pump/stop', { method: 'POST' });
        } catch {
            // ignore
        }
    }

    if (calibrationState.interval) {
        clearInterval(calibrationState.interval);
        calibrationState.interval = null;
    }

    resetCalibrationUi();
    setMessage('pumpResult', 'Калибровка отменена', 'info');
}

export function initCalibrationTab() {
    if (!byId('equipment')) return;
    updateCalibrationTime();
    loadCalibrationData();
}
