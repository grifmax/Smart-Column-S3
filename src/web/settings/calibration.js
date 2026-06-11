import { addLog } from '../core/logs.js';
import { initEquipmentNumberSteppers } from './number-stepper.js';

const API_BASE = '/api/calibration';
const HYDROMETER_POINT_SLOTS = 5;

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

function setValue(id, value) {
    const el = byId(id);
    if (!el) return;
    el.value = value === undefined || value === null ? '' : String(value);
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

function formatHydrometerNumber(value, digits = 3, suffix = '') {
    return Number.isFinite(Number(value)) ? `${Number(value).toFixed(digits)}${suffix}` : '—';
}

function populateHydrometerCalibration(hydrometer = {}) {
    const currentPressure = Number(hydrometer.currentPressure);
    const currentDensity = Number(hydrometer.currentDensity);
    const currentABV = Number(hydrometer.currentABV);
    const pointCount = Number(hydrometer.pointCount || 0);
    const abvPoints = Array.isArray(hydrometer.abvPoints) ? hydrometer.abvPoints : [];
    const pressurePoints = Array.isArray(hydrometer.pressurePoints) ? hydrometer.pressurePoints : [];
    const densityOffset = Number(hydrometer.densityOffset);

    const badge = byId('hydrometer-current-status');
    if (badge) {
        badge.textContent = hydrometer.valid ? 'Есть сигнал' : 'Нет сигнала';
        badge.className = `equipment-status-badge ${hydrometer.valid ? 'success' : 'muted'}`;
    }

    const currentPressureEl = byId('hydrometer-current-pressure');
    if (currentPressureEl) {
        currentPressureEl.textContent = formatHydrometerNumber(currentPressure, 3, ' кПа');
    }

    const currentDensityEl = byId('hydrometer-current-density');
    if (currentDensityEl) {
        currentDensityEl.textContent = formatHydrometerNumber(currentDensity, 4, '');
        currentDensityEl.dataset.value = Number.isFinite(currentDensity) ? currentDensity.toFixed(4) : '';
    }

    const currentAbvEl = byId('hydrometer-current-abv');
    if (currentAbvEl) {
        currentAbvEl.textContent = formatHydrometerNumber(currentABV, 1, ' %');
    }

    const summaryEl = byId('hydrometerCurrent');
    if (summaryEl) {
        summaryEl.textContent = pointCount >= 2
            ? `Активна таблица: ${pointCount} точк. Смещение плотности ${formatHydrometerNumber(densityOffset, 4)}`
            : 'Таблица калибровки пока не задана. Будет использоваться грубая формула.';
    }

    setValue('hydrometer-density-offset', Number.isFinite(densityOffset) ? densityOffset.toFixed(4) : '0.0000');

    for (let i = 0; i < HYDROMETER_POINT_SLOTS; i += 1) {
        const abv = Number(abvPoints[i]);
        const pressure = Number(pressurePoints[i]);
        setValue(`hydrometer-abv-${i}`, Number.isFinite(abv) ? abv.toFixed(1) : '');
        setValue(`hydrometer-pressure-${i}`, Number.isFinite(pressure) ? pressure.toFixed(4) : '');
    }

    const rowsHost = byId('hydrometer-points');
    if (rowsHost) {
        initEquipmentNumberSteppers(rowsHost);
    }
}

function collectHydrometerCalibrationPayload() {
    const abvPoints = [];
    const pressurePoints = [];

    for (let i = 0; i < HYDROMETER_POINT_SLOTS; i += 1) {
        const abvRaw = String(byId(`hydrometer-abv-${i}`)?.value || '').trim().replace(',', '.');
        const pressureRaw = String(byId(`hydrometer-pressure-${i}`)?.value || '').trim().replace(',', '.');

        if (!abvRaw && !pressureRaw) {
            continue;
        }
        if (!abvRaw || !pressureRaw) {
            throw new Error(`Точка ${i + 1}: заполните и ABV, и сигнал/плотность`);
        }

        const abv = Number(abvRaw);
        const pressure = Number(pressureRaw);
        if (!Number.isFinite(abv) || abv < 0 || abv > 100) {
            throw new Error(`Точка ${i + 1}: ABV должен быть в диапазоне 0..100%`);
        }
        if (!Number.isFinite(pressure) || pressure < 0.5 || pressure > 1.2) {
            throw new Error(`Точка ${i + 1}: сигнал/плотность должен быть в диапазоне 0.500..1.200`);
        }

        abvPoints.push(abv);
        pressurePoints.push(pressure);
    }

    const densityOffset = Number(String(byId('hydrometer-density-offset')?.value || '0').trim().replace(',', '.'));
    if (!Number.isFinite(densityOffset) || densityOffset < -0.25 || densityOffset > 0.25) {
        throw new Error('Смещение плотности должно быть в диапазоне -0.250..0.250');
    }

    return {
        densityOffset,
        abvPoints,
        pressurePoints
    };
}

export function fillHydrometerPointFromCurrent(index) {
    const densityValue = Number(byId('hydrometer-current-density')?.dataset?.value);
    if (!Number.isFinite(densityValue)) {
        setMessage('hydrometerResult', 'Нет текущего сигнала ареометра для подстановки', 'error');
        return;
    }
    setValue(`hydrometer-pressure-${index}`, densityValue.toFixed(4));
}

export async function saveHydrometerCalibration() {
    let payload;
    try {
        payload = collectHydrometerCalibrationPayload();
    } catch (error) {
        setMessage('hydrometerResult', error.message, 'error');
        return;
    }

    if (payload.abvPoints.length === 1) {
        setMessage('hydrometerResult', 'Для рабочей таблицы нужны минимум 2 точки', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/hydrometer`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage(
            'hydrometerResult',
            payload.abvPoints.length >= 2
                ? `Таблица ареометра сохранена: ${Number(data.pointCount || payload.abvPoints.length)} точк.`
                : 'Смещение ареометра сохранено, таблица очищена',
            'success'
        );
        await loadCalibrationData();
    } catch (error) {
        setMessage('hydrometerResult', `Ошибка сохранения ареометра: ${error.message}`, 'error');
    }
}

export async function clearHydrometerCalibration() {
    try {
        const densityOffset = Number(String(byId('hydrometer-density-offset')?.value || '0').trim().replace(',', '.'));
        const response = await fetch(`${API_BASE}/hydrometer`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                densityOffset: Number.isFinite(densityOffset) ? densityOffset : 0,
                abvPoints: [],
                pressurePoints: []
            })
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage('hydrometerResult', 'Таблица ареометра очищена', 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('hydrometerResult', `Ошибка очистки ареометра: ${error.message}`, 'error');
    }
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
    setMessage('pumpResult', '', '');
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
        const hydrometer = data?.hydrometer || {};

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
                    li.className = 'equipment-sensor-item';
                    li.innerHTML = `
                        <div class="equipment-sensor-head">
                            <strong>${name}</strong>
                            <span class="info-text">${validBadge}</span>
                        </div>
                        <div class="equipment-sensor-meta">Адрес: ${temp.address || '—'} | Текущее: ${Number.isFinite(current) ? current.toFixed(2) : '--'} °C | Смещение: ${Number.isFinite(offset) ? offset.toFixed(2) : '0.00'} °C</div>
                        <div class="equipment-sensor-actions">
                            <div class="form-group equipment-sensor-action">
                                <label for="offset_${i}">Смещение, °C</label>
                                <div class="equipment-sensor-action-row">
                                    <input type="number" id="offset_${i}" value="${Number.isFinite(offset) ? offset.toFixed(2) : '0.00'}" step="0.1" data-stepper-mode="pair" data-stepper-step="0.1">
                                    <button class="btn btn-sm" onclick="calibrateTempOffset(${i})">Применить</button>
                                </div>
                            </div>
                            <div class="form-group equipment-sensor-action">
                                <label for="ref_${i}">Эталон, °C</label>
                                <div class="equipment-sensor-action-row">
                                    <input type="number" id="ref_${i}" step="0.1" placeholder="Эталон °C" data-stepper-mode="pair" data-stepper-step="0.1">
                                    <button class="btn btn-sm btn-secondary" onclick="calibrateTempReference(${i})">По эталону</button>
                                </div>
                            </div>
                        </div>
                    `;
                    sensorList.appendChild(li);
                    initEquipmentNumberSteppers(li);
                });
            }
        }

        populateHydrometerCalibration(hydrometer);
    } catch (error) {
        console.error('loadCalibrationData error:', error);
        setMessage('tempResult', 'Ошибка загрузки данных калибровки', 'error');
        setMessage('hydrometerResult', 'Ошибка загрузки данных ареометра', 'error');
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
        // Инициируем сессию калибровки на сервере (фиксирует начальные шаги)
        const calStartResp = await fetch(`/api/pump/calibrate/start?speed=${speed}`, { method: 'POST' });
        if (!calStartResp.ok) {
            const err = await calStartResp.json().catch(() => ({}));
            throw new Error(err.message || `Старт сессии: HTTP ${calStartResp.status}`);
        }

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
        // Останавливаем насос и фиксируем конечные шаги в сессии калибровки
        await fetch('/api/pump/calibrate/stop', { method: 'POST' });
    } catch {
        // ignore
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
    if (manualWrap) manualWrap.style.display = '';

    if (autoStop) {
        if (actualVolume) actualVolume.value = String(calibrationState.targetVolume);
        setMessage('pumpResult', 'Калибровка завершена. Проверьте фактически налитый объём и нажмите «Применить».', 'success');
    } else {
        setMessage('pumpResult', 'Калибровка остановлена. Укажите фактический объём и примените.', 'info');
    }
}

export async function applyCalibration() {
    const actualVolume = Number(byId('actual-volume')?.value);
    if (!Number.isFinite(actualVolume) || actualVolume <= 0) {
        setMessage('pumpResult', 'Введите фактически налитый объём', 'error');
        return;
    }

    try {
        // Используем /api/pump/calibrate/finish — он знает количество шагов из сессии
        const response = await fetch('/api/pump/calibrate/finish', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ volume: actualVolume })
        });

        if (!response.ok) {
            const errData = await response.json().catch(() => ({}));
            throw new Error(errData.message || `HTTP ${response.status}`);
        }
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
    // Используем эндпоинт отмены, который гарантированно сбрасывает флаг active на сервере
    try {
        await fetch('/api/pump/calibrate/cancel', { method: 'POST' });
    } catch { /* ignore */ }
    try {
        await fetch('/api/pump/stop', { method: 'POST' });
    } catch { /* ignore */ }

    if (calibrationState.interval) {
        clearInterval(calibrationState.interval);
        calibrationState.interval = null;
    }

    resetCalibrationUi();
    setMessage('pumpResult', 'Калибровка отменена', 'info');
}

export function initCalibrationTab() {
    if (!byId('equipment')) return;
    initEquipmentNumberSteppers();
    updateCalibrationTime();
    loadCalibrationData();
}

