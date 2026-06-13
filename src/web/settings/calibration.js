import { addLog } from '../core/logs.js';
import { initEquipmentNumberSteppers } from './number-stepper.js';

const API_BASE = '/api/calibration';
const HYDROMETER_POINT_SLOTS = 5;
const PRESSURE_POINT_SLOTS = 5;

let calibrationState = {
    running: false,
    startTime: null,
    targetTime: 0,
    targetVolume: 0,
    speed: 0,
    interval: null
};

let calibrationImportSnapshot = null;

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

function downloadJsonFile(data, filename) {
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
}

function updateCalibrationImportUi(summaryText = 'Файл не выбран', canApply = false) {
    const preview = byId('calibration-import-preview');
    if (preview) {
        preview.textContent = summaryText;
    }
    const applyBtn = byId('calibration-import-apply-btn');
    if (applyBtn) {
        applyBtn.disabled = !canApply;
    }
}

function formatHydrometerNumber(value, digits = 3, suffix = '') {
    return Number.isFinite(Number(value)) ? `${Number(value).toFixed(digits)}${suffix}` : '—';
}

function formatPressureNumber(value, digits = 3, suffix = '') {
    return Number.isFinite(Number(value)) ? `${Number(value).toFixed(digits)}${suffix}` : 'вЂ”';
}

function populatePressureCalibration(pressureSensor = {}) {
    const currentVoltage = Number(pressureSensor.currentVoltage);
    const currentAdc = Number(pressureSensor.currentAdc);
    const currentPressure = Number(pressureSensor.currentPressure);
    const pointCount = Number(pressureSensor.pointCount || 0);
    const voltagePoints = Array.isArray(pressureSensor.voltagePoints) ? pressureSensor.voltagePoints : [];
    const pressurePoints = Array.isArray(pressureSensor.pressurePoints) ? pressureSensor.pressurePoints : [];

    const badge = byId('pressure-current-status');
    if (badge) {
        badge.textContent = pressureSensor.valid ? 'Р•СЃС‚СЊ СЃРёРіРЅР°Р»' : 'РќРµС‚ СЃРёРіРЅР°Р»Р°';
        badge.className = `equipment-status-badge ${pressureSensor.valid ? 'success' : 'muted'}`;
    }

    const currentVoltageEl = byId('pressure-current-voltage');
    if (currentVoltageEl) {
        currentVoltageEl.textContent = formatPressureNumber(currentVoltage, 3, ' Р’');
        currentVoltageEl.dataset.value = Number.isFinite(currentVoltage) ? currentVoltage.toFixed(4) : '';
    }

    const currentAdcEl = byId('pressure-current-adc');
    if (currentAdcEl) {
        currentAdcEl.textContent = Number.isFinite(currentAdc) ? String(Math.round(currentAdc)) : 'вЂ”';
    }

    const currentPressureEl = byId('pressure-current-value');
    if (currentPressureEl) {
        currentPressureEl.textContent = formatPressureNumber(currentPressure, 1, ' РјРј СЂС‚.СЃС‚.');
    }

    const summaryEl = byId('pressureCurrent');
    if (summaryEl) {
        summaryEl.textContent = pointCount >= 2
            ? `РђРєС‚РёРІРЅР° С‚Р°Р±Р»РёС†Р°: ${pointCount} С‚РѕС‡Рє. РџРѕРєР°Р·Р°РЅРёРµ РєСѓР±Р° С‡РёС‚Р°РµС‚СЃСЏ РїРѕ РєР°Р»РёР±СЂРѕРІРєРµ.`
            : 'РўР°Р±Р»РёС†Р° РєР°Р»РёР±СЂРѕРІРєРё РїРѕРєР° РЅРµ Р·Р°РґР°РЅР°. Р”Р»СЏ РґР°РІР»РµРЅРёСЏ РёСЃРїРѕР»СЊР·СѓРµС‚СЃСЏ fallback-С„РѕСЂРјСѓР»Р°.';
    }

    for (let i = 0; i < PRESSURE_POINT_SLOTS; i += 1) {
        const voltage = Number(voltagePoints[i]);
        const pressure = Number(pressurePoints[i]);
        setValue(`pressure-voltage-${i}`, Number.isFinite(voltage) ? voltage.toFixed(4) : '');
        setValue(`pressure-mmhg-${i}`, Number.isFinite(pressure) ? pressure.toFixed(1) : '');
    }

    const rowsHost = byId('pressure-points');
    if (rowsHost) {
        initEquipmentNumberSteppers(rowsHost);
    }

    const pressureCard = byId('pressure-reference-mmhg')?.closest('.equipment-card');
    if (pressureCard) {
        initEquipmentNumberSteppers(pressureCard);
    }
}

function collectPressureCalibrationPayload() {
    const voltagePoints = [];
    const pressurePoints = [];

    for (let i = 0; i < PRESSURE_POINT_SLOTS; i += 1) {
        const voltageRaw = String(byId(`pressure-voltage-${i}`)?.value || '').trim().replace(',', '.');
        const pressureRaw = String(byId(`pressure-mmhg-${i}`)?.value || '').trim().replace(',', '.');

        if (!voltageRaw && !pressureRaw) {
            continue;
        }
        if (!voltageRaw || !pressureRaw) {
            throw new Error(`РўРѕС‡РєР° ${i + 1}: Р·Р°РїРѕР»РЅРёС‚Рµ Рё РЅР°РїСЂСЏР¶РµРЅРёРµ, Рё РґР°РІР»РµРЅРёРµ`);
        }

        const voltage = Number(voltageRaw);
        const pressure = Number(pressureRaw);
        if (!Number.isFinite(voltage) || voltage < 0 || voltage > 4.096) {
            throw new Error(`РўРѕС‡РєР° ${i + 1}: РЅР°РїСЂСЏР¶РµРЅРёРµ РґРѕР»Р¶РЅРѕ Р±С‹С‚СЊ РІ РґРёР°РїР°Р·РѕРЅРµ 0.0000..4.0960 Р’`);
        }
        if (!Number.isFinite(pressure) || pressure < 0 || pressure > 75) {
            throw new Error(`РўРѕС‡РєР° ${i + 1}: РґР°РІР»РµРЅРёРµ РґРѕР»Р¶РЅРѕ Р±С‹С‚СЊ РІ РґРёР°РїР°Р·РѕРЅРµ 0..75 РјРј СЂС‚.СЃС‚.`);
        }

        voltagePoints.push(voltage);
        pressurePoints.push(pressure);
    }

    return { voltagePoints, pressurePoints };
}

function formatPressureNumberV2(value, digits = 3, suffix = '') {
    return Number.isFinite(Number(value)) ? `${Number(value).toFixed(digits)}${suffix}` : '--';
}

function populatePressureCalibrationV2(pressureSensor = {}) {
    const ads1115Available = pressureSensor.ads1115Available !== false;
    const sourceLabel = String(pressureSensor.source || 'ADS1115 A1');
    const currentVoltage = Number(pressureSensor.currentVoltage);
    const currentAdc = Number(pressureSensor.currentAdc);
    const currentPressure = Number(pressureSensor.currentPressure);
    const pointCount = Number(pressureSensor.pointCount || 0);
    const zeroOffsetMmHg = Number(pressureSensor.zeroOffsetMmHg || 0);
    const voltagePoints = Array.isArray(pressureSensor.voltagePoints) ? pressureSensor.voltagePoints : [];
    const pressurePoints = Array.isArray(pressureSensor.pressurePoints) ? pressureSensor.pressurePoints : [];

    const badge = byId('pressure-current-status');
    if (badge) {
        if (!ads1115Available) {
            badge.textContent = 'ADS1115 missing';
            badge.className = 'equipment-status-badge danger';
        } else {
            badge.textContent = pressureSensor.valid ? 'Signal OK' : 'No signal';
            badge.className = `equipment-status-badge ${pressureSensor.valid ? 'success' : 'muted'}`;
        }
    }

    const sourceEl = byId('pressure-current-source');
    if (sourceEl) {
        sourceEl.textContent = ads1115Available ? sourceLabel : `${sourceLabel} offline`;
    }

    const currentVoltageEl = byId('pressure-current-voltage');
    if (currentVoltageEl) {
        currentVoltageEl.textContent = formatPressureNumberV2(currentVoltage, 3, ' V');
        currentVoltageEl.dataset.value = Number.isFinite(currentVoltage) ? currentVoltage.toFixed(4) : '';
    }

    const currentAdcEl = byId('pressure-current-adc');
    if (currentAdcEl) {
        currentAdcEl.textContent = Number.isFinite(currentAdc) ? String(Math.round(currentAdc)) : '--';
    }

    const currentPressureEl = byId('pressure-current-value');
    if (currentPressureEl) {
        currentPressureEl.textContent = formatPressureNumberV2(currentPressure, 1, ' mmHg');
        currentPressureEl.dataset.value = Number.isFinite(currentPressure) ? currentPressure.toFixed(3) : '';
    }

    const summaryEl = byId('pressureCurrent');
    if (summaryEl) {
        const tableText = pointCount >= 2
            ? `Active table: ${pointCount} points`
            : 'No calibration table yet';
        const sourceText = pointCount >= 2
            ? 'cube pressure is read from the saved interpolation table'
            : 'firmware uses the legacy fallback formula';
        const busText = ads1115Available
            ? `${sourceLabel} is visible on the I2C bus`
            : `${sourceLabel} is not visible on the I2C bus`;
        summaryEl.textContent = `${busText}. ${tableText}. ${sourceText}. Zero trim: ${formatPressureNumberV2(zeroOffsetMmHg, 1, ' mmHg')}.`;
    }

    const zeroOffsetEl = byId('pressure-zero-offset');
    if (zeroOffsetEl) {
        zeroOffsetEl.textContent = formatPressureNumberV2(zeroOffsetMmHg, 1, ' mmHg');
        zeroOffsetEl.dataset.value = Number.isFinite(zeroOffsetMmHg) ? zeroOffsetMmHg.toFixed(3) : '0.000';
    }

    for (let i = 0; i < PRESSURE_POINT_SLOTS; i += 1) {
        const voltage = Number(voltagePoints[i]);
        const pressure = Number(pressurePoints[i]);
        setValue(`pressure-voltage-${i}`, Number.isFinite(voltage) ? voltage.toFixed(4) : '');
        setValue(`pressure-mmhg-${i}`, Number.isFinite(pressure) ? pressure.toFixed(1) : '');
    }

    const rowsHost = byId('pressure-points');
    if (rowsHost) {
        initEquipmentNumberSteppers(rowsHost);
    }

    const pressureCard = byId('pressure-reference-mmhg')?.closest('.equipment-card');
    if (pressureCard) {
        initEquipmentNumberSteppers(pressureCard);
    }
}

function collectPressureCalibrationPayloadV2() {
    const voltagePoints = [];
    const pressurePoints = [];

    for (let i = 0; i < PRESSURE_POINT_SLOTS; i += 1) {
        const voltageRaw = String(byId(`pressure-voltage-${i}`)?.value || '').trim().replace(',', '.');
        const pressureRaw = String(byId(`pressure-mmhg-${i}`)?.value || '').trim().replace(',', '.');

        if (!voltageRaw && !pressureRaw) {
            continue;
        }
        if (!voltageRaw || !pressureRaw) {
            throw new Error(`Point ${i + 1}: fill both voltage and pressure`);
        }

        const voltage = Number(voltageRaw);
        const pressure = Number(pressureRaw);
        if (!Number.isFinite(voltage) || voltage < 0 || voltage > 4.096) {
            throw new Error(`Point ${i + 1}: voltage must be in range 0.0000..4.0960 V`);
        }
        if (!Number.isFinite(pressure) || pressure < 0 || pressure > 75) {
            throw new Error(`Point ${i + 1}: pressure must be in range 0..75 mmHg`);
        }

        voltagePoints.push(voltage);
        pressurePoints.push(pressure);
    }

    return { voltagePoints, pressurePoints };
}

function findNextPressurePointSlot() {
    for (let i = 0; i < PRESSURE_POINT_SLOTS; i += 1) {
        const voltageRaw = String(byId(`pressure-voltage-${i}`)?.value || '').trim();
        const pressureRaw = String(byId(`pressure-mmhg-${i}`)?.value || '').trim();
        if (!voltageRaw && !pressureRaw) {
            return i;
        }
    }
    return -1;
}

function getPressureReferenceValueV2() {
    const raw = String(byId('pressure-reference-mmhg')?.value || '').trim().replace(',', '.');
    const value = Number(raw);
    if (!raw || !Number.isFinite(value) || value < 0 || value > 75) {
        throw new Error('Reference manometer value must be in range 0..75 mmHg');
    }
    return value;
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

function normalizeCalibrationSnapshot(payload) {
    const source = payload?.calibration && typeof payload.calibration === 'object'
        ? payload.calibration
        : payload;
    if (!source || typeof source !== 'object') {
        throw new Error('Неверный формат snapshot');
    }

    const pump = source.pump && typeof source.pump === 'object' ? source.pump : {};
    const temperatures = Array.isArray(source.temperatures) ? source.temperatures : [];
    const pressureSensor = source.pressureSensor && typeof source.pressureSensor === 'object' ? source.pressureSensor : {};
    const hydrometer = source.hydrometer && typeof source.hydrometer === 'object' ? source.hydrometer : {};

    const normalizedPump = {
        mlPerRev: Number(pump.mlPerRev),
        stepsPerRev: Number(pump.stepsPerRev),
        microsteps: Number(pump.microsteps)
    };

    const normalizedTemps = temperatures
        .map((item) => ({
            index: Number(item?.index),
            offset: Number(item?.offset),
            address: String(item?.address || '')
        }))
        .filter((item) => Number.isInteger(item.index) && Number.isFinite(item.offset));

    const pressureVoltages = Array.isArray(pressureSensor.voltagePoints) ? pressureSensor.voltagePoints : [];
    const pressureSensorPoints = Array.isArray(pressureSensor.pressurePoints) ? pressureSensor.pressurePoints : [];
    const normalizedPressureSensor = {
        zeroOffsetMmHg: Number(pressureSensor.zeroOffsetMmHg),
        voltagePoints: pressureVoltages.map((item) => Number(item)).filter((item) => Number.isFinite(item)),
        pressurePoints: pressureSensorPoints.map((item) => Number(item)).filter((item) => Number.isFinite(item))
    };

    if (normalizedPressureSensor.voltagePoints.length !== normalizedPressureSensor.pressurePoints.length) {
        throw new Error('Р’ snapshot РґР°РІР»РµРЅРёСЏ РЅРµ СЃРѕРІРїР°РґР°РµС‚ РєРѕР»РёС‡РµСЃС‚РІРѕ voltage Рё pressure points');
    }
    if (normalizedPressureSensor.voltagePoints.length === 1) {
        throw new Error('Snapshot РґР°РІР»РµРЅРёСЏ СЃРѕРґРµСЂР¶РёС‚ С‚РѕР»СЊРєРѕ 1 С‚РѕС‡РєСѓ. РќСѓР¶РЅС‹ 0 РёР»Рё РјРёРЅРёРјСѓРј 2');
    }

    if (!Number.isFinite(normalizedPressureSensor.zeroOffsetMmHg)) {
        normalizedPressureSensor.zeroOffsetMmHg = 0;
    }

    const hydrometerPoints = Array.isArray(hydrometer.abvPoints) ? hydrometer.abvPoints : [];
    const hydrometerSignals = Array.isArray(hydrometer.pressurePoints) ? hydrometer.pressurePoints : [];
    const normalizedHydrometer = {
        densityOffset: Number(hydrometer.densityOffset),
        abvPoints: hydrometerPoints.map((item) => Number(item)).filter((item) => Number.isFinite(item)),
        pressurePoints: hydrometerSignals.map((item) => Number(item)).filter((item) => Number.isFinite(item))
    };

    if (normalizedHydrometer.abvPoints.length !== normalizedHydrometer.pressurePoints.length) {
        throw new Error('В snapshot ареометра не совпадает количество ABV и signal points');
    }
    if (normalizedHydrometer.abvPoints.length === 1) {
        throw new Error('Snapshot ареометра содержит только 1 точку. Нужны 0 или минимум 2');
    }

    return {
        meta: payload?.meta && typeof payload.meta === 'object' ? payload.meta : {},
        pump: normalizedPump,
        temperatures: normalizedTemps,
        pressureSensor: normalizedPressureSensor,
        hydrometer: normalizedHydrometer
    };
}

function describeCalibrationSnapshot(snapshot) {
    const pumpText = Number.isFinite(snapshot.pump.mlPerRev) && snapshot.pump.mlPerRev > 0
        ? `${snapshot.pump.mlPerRev.toFixed(3)} мл/об`
        : 'нет pump-cal';
    const tempText = `${snapshot.temperatures.length} offsets`;
    const pressurePointCount = snapshot.pressureSensor.voltagePoints.length;
    const pressureText = pressurePointCount >= 2
        ? `${pressurePointCount} С‚РѕС‡Рє. РґР°РІР»РµРЅРёСЏ`
        : 'Р±РµР· С‚Р°Р±Р»РёС†С‹ РґР°РІР»РµРЅРёСЏ';
    const hydroPointCount = snapshot.hydrometer.abvPoints.length;
    const hydroText = hydroPointCount >= 2
        ? `${hydroPointCount} точк. ареометра`
        : 'без таблицы ареометра';
    const versionText = snapshot.meta?.firmwareVersion ? ` | FW ${snapshot.meta.firmwareVersion}` : '';
    return `РќР°СЃРѕСЃ: ${pumpText} | РўРµСЂРјРѕРґР°С‚С‡РёРєРё: ${tempText} | Р”Р°РІР»РµРЅРёРµ: ${pressureText} | РђСЂРµРѕРјРµС‚СЂ: ${hydroText}${versionText}`;
    return `Насос: ${pumpText} | Термодатчики: ${tempText} | Ареометр: ${hydroText}${versionText}`;
}

function describeCalibrationSnapshotV2(snapshot) {
    const pumpText = Number.isFinite(snapshot.pump.mlPerRev) && snapshot.pump.mlPerRev > 0
        ? `${snapshot.pump.mlPerRev.toFixed(3)} ml/rev`
        : 'no pump-cal';
    const tempText = `${snapshot.temperatures.length} offsets`;
    const pressurePointCount = snapshot.pressureSensor.voltagePoints.length;
    const pressureText = pressurePointCount >= 2
        ? `${pressurePointCount} pressure pts`
        : 'no pressure table';
    const hydroPointCount = snapshot.hydrometer.abvPoints.length;
    const hydroText = hydroPointCount >= 2
        ? `${hydroPointCount} hydrometer pts`
        : 'no hydrometer table';
    const versionText = snapshot.meta?.firmwareVersion ? ` | FW ${snapshot.meta.firmwareVersion}` : '';
    return `Pump: ${pumpText} | Temp: ${tempText} | Pressure: ${pressureText} | Hydrometer: ${hydroText}${versionText}`;
}

export async function exportCalibrationSnapshot() {
    try {
        const [calibrationResponse, versionResponse] = await Promise.all([
            fetch(API_BASE),
            fetch('/api/version').catch(() => null)
        ]);
        if (!calibrationResponse.ok) {
            throw new Error(`HTTP ${calibrationResponse.status}`);
        }

        const calibration = await calibrationResponse.json();
        const version = versionResponse && versionResponse.ok ? await versionResponse.json() : {};
        const payload = {
            schema: 'smart-column-calibration-snapshot-v1',
            exportedAt: new Date().toISOString(),
            meta: {
                firmwareVersion: version?.firmware?.version || '',
                board: version?.firmware?.name || 'Smart-Column-S3'
            },
            calibration
        };

        const dayStamp = new Date().toISOString().slice(0, 10);
        downloadJsonFile(payload, `calibration_snapshot_${dayStamp}.json`);
        setMessage('calibrationImportResult', 'Snapshot калибровок экспортирован', 'success');
    } catch (error) {
        console.error('exportCalibrationSnapshot error:', error);
        setMessage('calibrationImportResult', `Ошибка экспорта snapshot: ${error.message}`, 'error');
    }
}

export function openCalibrationImportDialog() {
    byId('calibration-import-file')?.click();
}

export function onCalibrationSnapshotFileChange(event) {
    const file = event?.target?.files?.[0];
    calibrationImportSnapshot = null;
    updateCalibrationImportUi('Файл не выбран', false);

    if (!file) {
        return;
    }

    const reader = new FileReader();
    reader.onload = (loadEvent) => {
        try {
            const raw = JSON.parse(String(loadEvent?.target?.result || '{}'));
            calibrationImportSnapshot = normalizeCalibrationSnapshot(raw);
            updateCalibrationImportUi(describeCalibrationSnapshotV2(calibrationImportSnapshot), true);
            setMessage('calibrationImportResult', 'Snapshot прочитан. Можно применять.', 'success');
        } catch (error) {
            calibrationImportSnapshot = null;
            updateCalibrationImportUi(`Ошибка файла: ${error.message}`, false);
            setMessage('calibrationImportResult', `Ошибка чтения snapshot: ${error.message}`, 'error');
        }
    };
    reader.readAsText(file);
}

export async function applyCalibrationSnapshot() {
    if (!calibrationImportSnapshot) {
        setMessage('calibrationImportResult', 'Сначала выберите корректный snapshot', 'error');
        return;
    }

    try {
        const pumpPayload = {};
        if (Number.isFinite(calibrationImportSnapshot.pump.mlPerRev) && calibrationImportSnapshot.pump.mlPerRev > 0) {
            pumpPayload.mlPerRev = calibrationImportSnapshot.pump.mlPerRev;
        }
        if (Number.isFinite(calibrationImportSnapshot.pump.stepsPerRev) && calibrationImportSnapshot.pump.stepsPerRev > 0) {
            pumpPayload.stepsPerRev = Math.round(calibrationImportSnapshot.pump.stepsPerRev);
        }
        if (Object.keys(pumpPayload).length > 0) {
            const pumpResponse = await fetch(`${API_BASE}/pump`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(pumpPayload)
            });
            if (!pumpResponse.ok) {
                const pumpError = await pumpResponse.json().catch(() => ({}));
                throw new Error(pumpError?.error || `Pump import HTTP ${pumpResponse.status}`);
            }
        }

        for (const sensor of calibrationImportSnapshot.temperatures) {
            const tempResponse = await fetch(`${API_BASE}/temp`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ index: sensor.index, offset: sensor.offset })
            });
            if (!tempResponse.ok) {
                const tempError = await tempResponse.json().catch(() => ({}));
                throw new Error(tempError?.error || `Temp[${sensor.index}] import HTTP ${tempResponse.status}`);
            }
        }

        const pressureResponse = await fetch(`${API_BASE}/pressure`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(calibrationImportSnapshot.pressureSensor)
        });
        if (!pressureResponse.ok) {
            const pressureError = await pressureResponse.json().catch(() => ({}));
            throw new Error(pressureError?.error || `Pressure import HTTP ${pressureResponse.status}`);
        }

        const hydrometerResponse = await fetch(`${API_BASE}/hydrometer`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(calibrationImportSnapshot.hydrometer)
        });
        if (!hydrometerResponse.ok) {
            const hydroError = await hydrometerResponse.json().catch(() => ({}));
            throw new Error(hydroError?.error || `Hydrometer import HTTP ${hydrometerResponse.status}`);
        }

        await loadCalibrationData();
        setMessage(
            'calibrationImportResult',
            `Snapshot применён: pump, ${calibrationImportSnapshot.temperatures.length} temp offsets, ${calibrationImportSnapshot.hydrometer.abvPoints.length} hydrometer points`,
            'success'
        );
        addLog('Snapshot калибровок применён из Web UI', 'success');
    } catch (error) {
        console.error('applyCalibrationSnapshot error:', error);
        setMessage('calibrationImportResult', `Ошибка применения snapshot: ${error.message}`, 'error');
    }
}

export function fillPressurePointFromCurrent(index) {
    const voltageValue = Number(byId('pressure-current-voltage')?.dataset?.value);
    if (!Number.isFinite(voltageValue)) {
        setMessage('pressureResult', 'РќРµС‚ С‚РµРєСѓС‰РµРіРѕ СЃРёРіРЅР°Р»Р° РґР°РІР»РµРЅРёСЏ РґР»СЏ РїРѕРґСЃС‚Р°РЅРѕРІРєРё', 'error');
        return;
    }
    setValue(`pressure-voltage-${index}`, voltageValue.toFixed(4));
}

export async function savePressureCalibration() {
    let payload;
    try {
        payload = collectPressureCalibrationPayloadV2();
    } catch (error) {
        setMessage('pressureResult', error.message, 'error');
        return;
    }

    if (payload.voltagePoints.length === 1) {
        setMessage('pressureResult', 'Р”Р»СЏ СЂР°Р±РѕС‡РµР№ С‚Р°Р±Р»РёС†С‹ РЅСѓР¶РЅС‹ РјРёРЅРёРјСѓРј 2 С‚РѕС‡РєРё', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/pressure`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage(
            'pressureResult',
            payload.voltagePoints.length >= 2
                ? `РўР°Р±Р»РёС†Р° РґР°РІР»РµРЅРёСЏ СЃРѕС…СЂР°РЅРµРЅР°: ${Number(data.pointCount || payload.voltagePoints.length)} С‚РѕС‡Рє.`
                : 'РўР°Р±Р»РёС†Р° РґР°РІР»РµРЅРёСЏ РѕС‡РёС‰РµРЅР°',
            'success'
        );
        await loadCalibrationData();
    } catch (error) {
        setMessage('pressureResult', `РћС€РёР±РєР° СЃРѕС…СЂР°РЅРµРЅРёСЏ РґР°РІР»РµРЅРёСЏ: ${error.message}`, 'error');
    }
}

export async function clearPressureCalibration() {
    try {
        const response = await fetch(`${API_BASE}/pressure`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                voltagePoints: [],
                pressurePoints: []
            })
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage('pressureResult', 'РўР°Р±Р»РёС†Р° РґР°РІР»РµРЅРёСЏ РѕС‡РёС‰РµРЅР°', 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('pressureResult', `РћС€РёР±РєР° РѕС‡РёСЃС‚РєРё РґР°РІР»РµРЅРёСЏ: ${error.message}`, 'error');
    }
}

export function fillPressurePointFromCurrentV2(index) {
    const voltageValue = Number(byId('pressure-current-voltage')?.dataset?.value);
    if (!Number.isFinite(voltageValue)) {
        setMessage('pressureResult', 'No live pressure signal to copy', 'error');
        return;
    }
    setValue(`pressure-voltage-${index}`, voltageValue.toFixed(4));
}

export function addPressurePointFromCurrentV2() {
    const voltageValue = Number(byId('pressure-current-voltage')?.dataset?.value);
    if (!Number.isFinite(voltageValue)) {
        setMessage('pressureResult', 'No live pressure signal to capture', 'error');
        return;
    }

    let referencePressure;
    try {
        referencePressure = getPressureReferenceValueV2();
    } catch (error) {
        setMessage('pressureResult', error.message, 'error');
        return;
    }

    const slot = findNextPressurePointSlot();
    if (slot < 0) {
        setMessage('pressureResult', 'All 5 points are already filled. Clear or overwrite a row first.', 'error');
        return;
    }

    setValue(`pressure-voltage-${slot}`, voltageValue.toFixed(4));
    setValue(`pressure-mmhg-${slot}`, referencePressure.toFixed(1));
    setMessage('pressureResult', `Captured point ${slot + 1}: ${voltageValue.toFixed(4)} V -> ${referencePressure.toFixed(1)} mmHg`, 'success');
}

export async function applyPressureZeroTrimV2() {
    const currentPressure = Number(byId('pressure-current-value')?.dataset?.value);
    if (!Number.isFinite(currentPressure)) {
        setMessage('pressureResult', 'No live pressure reading for zero trim', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/pressure`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                zeroOffsetMmHg: currentPressure
            })
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage('pressureResult', `Zero trim applied: ${Number(data.zeroOffsetMmHg || currentPressure).toFixed(1)} mmHg`, 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('pressureResult', `Zero trim save error: ${error.message}`, 'error');
    }
}

export async function savePressureCalibrationV2() {
    let payload;
    try {
        payload = collectPressureCalibrationPayloadV2();
    } catch (error) {
        setMessage('pressureResult', error.message, 'error');
        return;
    }

    if (payload.voltagePoints.length === 1) {
        setMessage('pressureResult', 'At least 2 points are required for an active table', 'error');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/pressure`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage(
            'pressureResult',
            payload.voltagePoints.length >= 2
                ? `Pressure table saved: ${Number(data.pointCount || payload.voltagePoints.length)} points`
                : 'Pressure table cleared',
            'success'
        );
        await loadCalibrationData();
    } catch (error) {
        setMessage('pressureResult', `Pressure save error: ${error.message}`, 'error');
    }
}

export async function clearPressureCalibrationV2() {
    try {
        const response = await fetch(`${API_BASE}/pressure`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                voltagePoints: [],
                pressurePoints: []
            })
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage('pressureResult', 'Pressure table cleared', 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('pressureResult', `Pressure clear error: ${error.message}`, 'error');
    }
}

export async function clearPressureZeroTrimV2() {
    try {
        const response = await fetch(`${API_BASE}/pressure`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                zeroOffsetMmHg: 0
            })
        });
        const data = await response.json().catch(() => ({}));
        if (!response.ok) {
            throw new Error(data?.error || `HTTP ${response.status}`);
        }

        setMessage('pressureResult', 'Zero trim cleared', 'success');
        await loadCalibrationData();
    } catch (error) {
        setMessage('pressureResult', `Zero trim clear error: ${error.message}`, 'error');
    }
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
        const pressureSensor = data?.pressureSensor || {};
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

        populatePressureCalibrationV2(pressureSensor);
        populateHydrometerCalibration(hydrometer);
    } catch (error) {
        console.error('loadCalibrationData error:', error);
        setMessage('pressureResult', 'Pressure calibration load error', 'error');
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
    updateCalibrationImportUi('Файл не выбран', false);
    updateCalibrationTime();
    loadCalibrationData();
}

