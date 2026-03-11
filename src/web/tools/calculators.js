import { addLog } from '../core/logs.js';

const SUGAR_TO_AA_L_PER_KG = 0.647;
const SUGAR_VOLUME_DISPLACEMENT_L_PER_KG = 0.63;
const WATER_HEAT_KWH_PER_LC = 0.001163;

const PACKING_POWER_LIMITS = {
    spn: 65,
    rpn: 55,
    plates: 80
};

const PACKING_RATE_FACTORS = {
    spn: 1.0,
    rpn: 0.9,
    plates: 1.15
};

const STAGE_RATE_FACTORS = {
    heads: { min: 0.04, max: 0.08 },
    body: { min: 0.45, max: 0.75 },
    tails: { min: 0.18, max: 0.4 }
};

const ALCOHOLMETER_TEMP_ANCHORS = [0, 5, 10, 15, 20, 25, 30, 35, 40];
const ALCOHOLMETER_ABV_ANCHORS = [0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100];
const ALCOHOLMETER_CORRECTION_PER_DEGREE = [
    0.138,
    0.225,
    0.312,
    0.398,
    0.47,
    0.535,
    0.6,
    0.665,
    0.73,
    0.79,
    0.84
];
const ALCOHOLMETER_CORRECTION_GRID = ALCOHOLMETER_TEMP_ANCHORS.map((temp) => {
    const delta = 20 - temp;
    return ALCOHOLMETER_CORRECTION_PER_DEGREE.map((factor) => delta * factor);
});

const FERMENTATION_YEAST_PROFILES = {
    turbo: {
        label: 'Турбо / спиртовые',
        pitchPerLiter: 3.2,
        baseDays: 4,
        idealTemp: 28,
        minTemp: 22,
        maxTemp: 32
    },
    spirit: {
        label: 'Спиртовые классические',
        pitchPerLiter: 1.2,
        baseDays: 6,
        idealTemp: 26,
        minTemp: 20,
        maxTemp: 30
    },
    wine: {
        label: 'Винные',
        pitchPerLiter: 0.35,
        baseDays: 10,
        idealTemp: 22,
        minTemp: 17,
        maxTemp: 28
    },
    beer: {
        label: 'Пивные / эль',
        pitchPerLiter: 0.5,
        baseDays: 12,
        idealTemp: 20,
        minTemp: 16,
        maxTemp: 24
    }
};

function parseNumber(id) {
    const rawValue = document.getElementById(id)?.value ?? '';
    const normalized = String(rawValue).trim().replace(',', '.');
    const parsed = Number(normalized);
    return Number.isFinite(parsed) ? parsed : NaN;
}

function selectValue(id, fallback = '') {
    const el = document.getElementById(id);
    return el ? String(el.value || fallback) : fallback;
}

function setText(id, value) {
    const el = document.getElementById(id);
    if (el) {
        el.textContent = value;
    }
}

function setHidden(id, hidden) {
    const el = document.getElementById(id);
    if (el) {
        el.hidden = hidden;
    }
}

function clamp(value, min, max) {
    return Math.min(max, Math.max(min, value));
}

function formatLiters(value, digits = 2) {
    return `${value.toFixed(digits)} л`;
}

function formatMilliliters(value, digits = 0) {
    return `${value.toFixed(digits)} мл`;
}

function formatPower(value) {
    return `${Math.round(value)} Вт`;
}

function formatKwh(value) {
    return `${value.toFixed(2)} кВт·ч`;
}

function formatCurrency(value) {
    return `${value.toFixed(2)}`;
}

function formatDuration(hours) {
    const totalMinutes = Math.max(0, Math.round(hours * 60));
    const hh = Math.floor(totalMinutes / 60);
    const mm = totalMinutes % 60;

    if (hh > 0) {
        return `${hh} ч ${mm} мин`;
    }
    return `${mm} мин`;
}

function formatSignedPercent(value) {
    const sign = value >= 0 ? '+' : '';
    return `${sign}${value.toFixed(2)} %`;
}

function brixToSpecificGravity(brix) {
    return 1 + (brix / (258.6 - ((brix / 258.2) * 227.1)));
}

function specificGravityToBrix(sg) {
    return (((135.997 * sg - 630.272) * sg + 1111.14) * sg) - 616.868;
}

function specificGravityToPlato(sg) {
    return (((135.997 * sg - 630.272) * sg + 1111.14) * sg) - 616.868;
}

function platoToSpecificGravity(plato) {
    return 1 + (plato / (258.6 - ((plato / 258.2) * 227.1)));
}

function specificGravityToOechsle(sg) {
    return (sg - 1) * 1000;
}

function oechsleToSpecificGravity(oechsle) {
    return 1 + (oechsle / 1000);
}

function attenuationToFinalGravity(originalGravity, attenuationPercent) {
    return 1 + ((originalGravity - 1) * (1 - attenuationPercent / 100));
}

function describeSugarWaterRatio(sugarKg, waterL) {
    const ratio = waterL / sugarKg;

    if (ratio < 2.5) {
        return {
            ratioText: `1:${ratio.toFixed(1)}`,
            note: 'Очень плотная брага. Нужны сильные дрожжи и хороший контроль брожения.'
        };
    }

    if (ratio < 3.2) {
        return {
            ratioText: `1:${ratio.toFixed(1)}`,
            note: 'Плотная брага. Для современных турбо и спиртовых дрожжей обычно допустимо.'
        };
    }

    if (ratio <= 4.2) {
        return {
            ratioText: `1:${ratio.toFixed(1)}`,
            note: 'Классическая пропорция. Обычно дает спокойное и стабильное брожение.'
        };
    }

    return {
        ratioText: `1:${ratio.toFixed(1)}`,
        note: 'Жидкая брага. Брожение мягче, но перегонять придется больший объем.'
    };
}

function compareDesiredRate(desiredRate, minRate, maxRate) {
    if (!Number.isFinite(desiredRate) || desiredRate <= 0) {
        return 'Целевая скорость не задана';
    }

    if (desiredRate < minRate * 0.85) {
        return 'Слишком медленно. Режим будет стабильным, но неэффективным по времени.';
    }

    if (desiredRate > maxRate) {
        return 'Выше расчетного диапазона. Есть риск смаза фракций и захлеба.';
    }

    return 'Входит в расчетный диапазон.';
}

function buildSelectionNote(powerW, safePowerW, packingHeightMm, stage) {
    const notes = [];

    if (powerW > safePowerW * 1.05) {
        notes.push('Поданная мощность выше грубой оценки предзахлебной зоны.');
    }

    if (stage === 'body' && packingHeightMm < 1000) {
        notes.push('Для отбора тела царга низковата. Диапазон лучше держать ближе к нижней границе.');
    }

    if (notes.length === 0) {
        return 'Оценка выглядит рабочей. Диапазон все равно стоит проверять по давлению и поведению колонны.';
    }

    return notes.join(' ');
}

function readBlendRows() {
    const rows = [];

    for (let index = 1; index <= 4; index += 1) {
        const volumeMl = parseNumber(`calc-blend-volume-${index}`);
        const abv = parseNumber(`calc-blend-abv-${index}`);

        const hasVolume = Number.isFinite(volumeMl) && volumeMl > 0;
        const hasAbv = Number.isFinite(abv) && abv > 0;

        if (!hasVolume && !hasAbv) {
            continue;
        }

        if (!hasVolume || !hasAbv || abv > 100) {
            throw new Error(`Проверьте строку смеси ${index}. Нужны объем и крепость.`);
        }

        rows.push({ volumeMl, abv });
    }

    return rows;
}

function findInterpolationBounds(anchors, value) {
    if (value <= anchors[0]) {
        return { lowerIndex: 0, upperIndex: 0, ratio: 0, clamped: true };
    }

    const lastIndex = anchors.length - 1;
    if (value >= anchors[lastIndex]) {
        return { lowerIndex: lastIndex, upperIndex: lastIndex, ratio: 0, clamped: true };
    }

    for (let index = 0; index < lastIndex; index += 1) {
        const lower = anchors[index];
        const upper = anchors[index + 1];
        if (value >= lower && value <= upper) {
            const span = upper - lower || 1;
            return { lowerIndex: index, upperIndex: index + 1, ratio: (value - lower) / span, clamped: false };
        }
    }

    return { lowerIndex: lastIndex, upperIndex: lastIndex, ratio: 0, clamped: true };
}

function interpolateGrid(xAnchors, yAnchors, grid, xValue, yValue) {
    const xBounds = findInterpolationBounds(xAnchors, xValue);
    const yBounds = findInterpolationBounds(yAnchors, yValue);

    const q11 = grid[xBounds.lowerIndex][yBounds.lowerIndex];
    const q12 = grid[xBounds.lowerIndex][yBounds.upperIndex];
    const q21 = grid[xBounds.upperIndex][yBounds.lowerIndex];
    const q22 = grid[xBounds.upperIndex][yBounds.upperIndex];

    const top = q11 + ((q12 - q11) * yBounds.ratio);
    const bottom = q21 + ((q22 - q21) * yBounds.ratio);
    const value = top + ((bottom - top) * xBounds.ratio);

    return { value, clamped: xBounds.clamped || yBounds.clamped };
}

function describeAlcoholmeterRange(tempC, abvRaw) {
    const outOfTempRange = tempC < ALCOHOLMETER_TEMP_ANCHORS[0] || tempC > ALCOHOLMETER_TEMP_ANCHORS[ALCOHOLMETER_TEMP_ANCHORS.length - 1];
    const outOfAbvRange = abvRaw < ALCOHOLMETER_ABV_ANCHORS[0] || abvRaw > ALCOHOLMETER_ABV_ANCHORS[ALCOHOLMETER_ABV_ANCHORS.length - 1];

    if (!outOfTempRange && !outOfAbvRange) {
        return 'Табличная интерполяция по крепости и температуре.';
    }

    return 'Значение вышло за рабочий диапазон таблицы. Применена крайняя строка/столбец.';
}

function getDensityAsSpecificGravity(scale, rawValue) {
    if (!Number.isFinite(rawValue) || rawValue < 0) {
        return NaN;
    }

    if (scale === 'sg') {
        return rawValue;
    }

    if (scale === 'brix') {
        return brixToSpecificGravity(rawValue);
    }

    if (scale === 'plato') {
        return platoToSpecificGravity(rawValue);
    }

    if (scale === 'oechsle') {
        return oechsleToSpecificGravity(rawValue);
    }

    return NaN;
}

function summarizeFraction(absoluteAlcoholL, productL, productAbv, durationHours) {
    return `${absoluteAlcoholL.toFixed(2)} л АС / ${productL.toFixed(2)} л @ ${productAbv.toFixed(1)}% / ${formatDuration(durationHours)}`;
}

function getFermentationProfile() {
    const yeastType = selectValue('calc-ferment-yeast', 'spirit');
    return FERMENTATION_YEAST_PROFILES[yeastType] || FERMENTATION_YEAST_PROFILES.spirit;
}

function buildFermentationDuration(daysBase, fermentationTempC, idealTempC, washAbv, ratio) {
    let factor = 1;

    if (fermentationTempC < idealTempC) {
        factor += (idealTempC - fermentationTempC) * 0.08;
    } else if (fermentationTempC > idealTempC + 2) {
        factor += (fermentationTempC - idealTempC - 2) * 0.04;
    }

    if (washAbv > 14) {
        factor += 0.18;
    } else if (washAbv > 12) {
        factor += 0.1;
    }

    if (Number.isFinite(ratio) && ratio < 3) {
        factor += 0.12;
    }

    const centerDays = daysBase * factor;
    return {
        minDays: Math.max(1, centerDays * 0.85),
        maxDays: centerDays * 1.2
    };
}

function buildFermentationNote(profile, fermentationTempC, ratio, washAbv) {
    const notes = [];

    if (fermentationTempC < profile.minTemp || fermentationTempC > profile.maxTemp) {
        notes.push(`Температура вне комфортного диапазона для ${profile.label.toLowerCase()}.`);
    }

    if (Number.isFinite(ratio) && ratio < 3) {
        notes.push('Брага плотная, нужна хорошая аэрация и контроль температуры.');
    }

    if (washAbv > 15) {
        notes.push('Потенциальная крепость высокая, брожение может замедлиться ближе к финишу.');
    }

    if (notes.length === 0) {
        notes.push('Режим выглядит рабочим. Контроль температуры и pH все равно остается обязательным.');
    }

    return notes.join(' ');
}

export function updatePotentialAlcoholMode() {
    const mode = selectValue('calc-potential-source-type', 'sugar');

    setHidden('calc-potential-sugar-group', mode !== 'sugar');
    setHidden('calc-potential-water-group', mode !== 'sugar');
    setHidden('calc-potential-brix-group', mode !== 'brix');
    setHidden('calc-potential-sg-group', mode !== 'sg');
    setHidden('calc-potential-efficiency-group', mode !== 'sugar');
    setHidden('calc-potential-attenuation-group', mode === 'sugar');

    if (mode === 'sugar') {
        setText('calc-potential-helper', 'Для сахарной браги считаются потенциал спирта, КПД брожения и соотношение сахар/вода.');
    } else if (mode === 'brix') {
        setText('calc-potential-helper', 'Brix переводится в OG, затем крепость оценивается по выбранной степени сбраживания.');
    } else {
        setText('calc-potential-helper', 'Для сусла по SG расчет идет от начальной плотности к ожидаемому FG по степени сбраживания.');
    }
}

export function updateFermentationMode() {
    const mode = selectValue('calc-ferment-basis', 'sugar');

    setHidden('calc-ferment-sugar-group', mode !== 'sugar');
    setHidden('calc-ferment-water-group', mode !== 'sugar');
    setHidden('calc-ferment-brix-group', mode !== 'brix');
    setHidden('calc-ferment-sg-group', mode !== 'sg');
    setHidden('calc-ferment-efficiency-group', mode !== 'sugar');
    setHidden('calc-ferment-attenuation-group', mode === 'sugar');

    if (mode === 'sugar') {
        setText('calc-ferment-helper', 'Оценка OG/FG, крепости, гидромодуля, дозировки дрожжей и времени для сахарной браги.');
    } else if (mode === 'brix') {
        setText('calc-ferment-helper', 'Расчет по начальному Brix: OG, ожидаемый FG, крепость, срок и риски брожения.');
    } else {
        setText('calc-ferment-helper', 'Расчет по SG: для зерновых и фруктовых заторов с учетом сбраживания и температуры.');
    }
}

export function fetchCurrentTempForCalc() {
    const tempEl = document.getElementById('temp-reflux');
    const inputEl = document.getElementById('calc-temp-raw');

    if (tempEl && inputEl) {
        const tempValue = parseFloat(tempEl.textContent);
        if (!Number.isNaN(tempValue)) {
            inputEl.value = tempValue;
            addLog('Температура для коррекции крепости получена с датчика', 'info');
        } else {
            addLog('Нет актуальных данных датчика для коррекции крепости', 'warning');
        }
    }
}

export function calculateAbvCorrection() {
    const abvRaw = parseNumber('calc-abv-raw');
    const tempRaw = parseNumber('calc-temp-raw');

    if (Number.isNaN(abvRaw) || Number.isNaN(tempRaw)) {
        alert('Введите корректные значения.');
        return;
    }

    if (abvRaw < 0 || abvRaw > 100) {
        alert('Показания спиртометра должны быть в диапазоне 0-100%.');
        return;
    }

    const interpolation = interpolateGrid(
        ALCOHOLMETER_TEMP_ANCHORS,
        ALCOHOLMETER_ABV_ANCHORS,
        ALCOHOLMETER_CORRECTION_GRID,
        tempRaw,
        abvRaw
    );
    const realAbv = clamp(abvRaw + interpolation.value, 0, 100);

    setText('calc-abv-result', `${realAbv.toFixed(2)} %`);
    setText('calc-abv-correction', formatSignedPercent(interpolation.value));
    setText('calc-abv-note', describeAlcoholmeterRange(tempRaw, abvRaw));

    addLog(
        `Коррекция спиртометра: ${abvRaw.toFixed(1)}% при ${tempRaw.toFixed(1)}°C -> ${realAbv.toFixed(2)}%`,
        'info'
    );
}

export function calculateDilution() {
    const sourceVolumeMl = parseNumber('calc-dil-volume');
    const sourceAbv = parseNumber('calc-dil-abv-src');
    const targets = [
        parseNumber('calc-dil-stage-1'),
        parseNumber('calc-dil-stage-2'),
        parseNumber('calc-dil-stage-3')
    ].filter((value) => Number.isFinite(value) && value > 0);

    if (!Number.isFinite(sourceVolumeMl) || !Number.isFinite(sourceAbv) || sourceVolumeMl <= 0 || sourceAbv <= 0 || sourceAbv > 100) {
        alert('Проверьте исходный объем и крепость.');
        return;
    }

    if (targets.length === 0) {
        alert('Укажите хотя бы одну целевую крепость.');
        return;
    }

    let currentVolumeMl = sourceVolumeMl;
    let currentAbv = sourceAbv;
    let totalWaterMl = 0;
    const stageSummaries = [];

    for (const targetAbv of targets) {
        if (targetAbv <= 0 || targetAbv >= currentAbv) {
            alert('Каждый следующий этап должен быть ниже предыдущей крепости.');
            return;
        }

        const stageTotalMl = currentVolumeMl * (currentAbv / targetAbv);
        const waterMl = stageTotalMl - currentVolumeMl;
        totalWaterMl += waterMl;
        stageSummaries.push(`до ${targetAbv.toFixed(1)}%: +${waterMl.toFixed(0)} мл, итог ${stageTotalMl.toFixed(0)} мл`);

        currentVolumeMl = stageTotalMl;
        currentAbv = targetAbv;
    }

    setText('calc-dil-stage-1-result', stageSummaries[0] || '—');
    setText('calc-dil-stage-2-result', stageSummaries[1] || '—');
    setText('calc-dil-stage-3-result', stageSummaries[2] || '—');
    setText('calc-dil-water', formatMilliliters(totalWaterMl));
    setText('calc-dil-total', formatMilliliters(currentVolumeMl));

    addLog(
        `Разбавление по этапам: ${targets.length} этап., итог ${currentVolumeMl.toFixed(0)} мл @ ${currentAbv.toFixed(1)}%`,
        'info'
    );
}

export function calculateYieldFractions() {
    const sourceVolumeL = parseNumber('calc-yield-volume-l');
    const sourceAbv = parseNumber('calc-yield-abv');
    const headsPercent = parseNumber('calc-yield-heads-pct');
    const bodyPercent = parseNumber('calc-yield-body-pct');
    const bodyTargetAbv = parseNumber('calc-yield-body-abv');
    const headsAverageAbv = parseNumber('calc-yield-heads-abv');
    const tailsAverageAbv = parseNumber('calc-yield-tails-abv');
    const headsRateMlH = parseNumber('calc-yield-heads-rate');
    const bodyRateMlH = parseNumber('calc-yield-body-rate');
    const tailsRateMlH = parseNumber('calc-yield-tails-rate');

    const values = [
        sourceVolumeL,
        sourceAbv,
        headsPercent,
        bodyPercent,
        bodyTargetAbv,
        headsAverageAbv,
        tailsAverageAbv,
        headsRateMlH,
        bodyRateMlH,
        tailsRateMlH
    ];
    if (values.some((value) => Number.isNaN(value))) {
        alert('Введите корректные значения для расчета фракций.');
        return;
    }

    if (sourceVolumeL <= 0 || sourceAbv <= 0 || sourceAbv > 100 || bodyTargetAbv <= 0 || bodyTargetAbv >= 100) {
        alert('Проверьте объем и крепость. Крепость продукта должна быть в диапазоне 1-99%.');
        return;
    }

    if (headsAverageAbv <= 0 || headsAverageAbv >= 100 || tailsAverageAbv <= 0 || tailsAverageAbv >= 100) {
        alert('Средняя крепость голов и хвостов должна быть в диапазоне 1-99%.');
        return;
    }

    if (headsPercent < 0 || bodyPercent <= 0 || headsPercent + bodyPercent > 100) {
        alert('Сумма голов и тела не должна превышать 100% абсолютного спирта.');
        return;
    }

    if (headsRateMlH <= 0 || bodyRateMlH <= 0 || tailsRateMlH <= 0) {
        alert('Скорость отбора для каждого этапа должна быть больше нуля.');
        return;
    }

    const totalAbsoluteAlcoholL = sourceVolumeL * sourceAbv / 100;
    const headsAbsoluteAlcoholL = totalAbsoluteAlcoholL * headsPercent / 100;
    const bodyAbsoluteAlcoholL = totalAbsoluteAlcoholL * bodyPercent / 100;
    const tailsAbsoluteAlcoholL = Math.max(0, totalAbsoluteAlcoholL - headsAbsoluteAlcoholL - bodyAbsoluteAlcoholL);

    const headsProductL = headsAbsoluteAlcoholL / (headsAverageAbv / 100);
    const bodyProductL = bodyAbsoluteAlcoholL / (bodyTargetAbv / 100);
    const tailsProductL = tailsAbsoluteAlcoholL / (tailsAverageAbv / 100);

    const headsDurationH = (headsProductL * 1000) / headsRateMlH;
    const bodyDurationH = (bodyProductL * 1000) / bodyRateMlH;
    const tailsDurationH = (tailsProductL * 1000) / tailsRateMlH;
    const totalDurationH = headsDurationH + bodyDurationH + tailsDurationH;

    setText('calc-yield-aa-total', formatLiters(totalAbsoluteAlcoholL, 2));
    setText('calc-yield-heads-aa', summarizeFraction(headsAbsoluteAlcoholL, headsProductL, headsAverageAbv, headsDurationH));
    setText('calc-yield-body-aa', summarizeFraction(bodyAbsoluteAlcoholL, bodyProductL, bodyTargetAbv, bodyDurationH));
    setText('calc-yield-tails-aa', summarizeFraction(tailsAbsoluteAlcoholL, tailsProductL, tailsAverageAbv, tailsDurationH));
    setText('calc-yield-total-time', formatDuration(totalDurationH));

    addLog(
        `Фракции: АС ${totalAbsoluteAlcoholL.toFixed(2)} л, тело ${bodyProductL.toFixed(2)} л за ${formatDuration(bodyDurationH)}`,
        'info'
    );
}

export function calculatePotentialAlcohol() {
    const mode = selectValue('calc-potential-source-type', 'sugar');
    const volumeL = parseNumber('calc-potential-volume-l');

    if (!Number.isFinite(volumeL) || volumeL <= 0) {
        alert('Введите корректный объем браги или сусла.');
        return;
    }

    let washAbv;
    let absoluteAlcoholL;
    let primaryText;
    let secondaryText;
    let ratioText = 'н/д';
    let noteText;

    if (mode === 'sugar') {
        const sugarKg = parseNumber('calc-potential-sugar-kg');
        const waterL = parseNumber('calc-potential-water-l');
        const efficiencyPercent = parseNumber('calc-potential-efficiency-pct');

        if (!Number.isFinite(sugarKg) || !Number.isFinite(waterL) || !Number.isFinite(efficiencyPercent) ||
            sugarKg <= 0 || waterL <= 0 || efficiencyPercent <= 0 || efficiencyPercent > 100) {
            alert('Для сахарной браги укажите сахар, воду и КПД брожения.');
            return;
        }

        absoluteAlcoholL = sugarKg * SUGAR_TO_AA_L_PER_KG * (efficiencyPercent / 100);
        washAbv = (absoluteAlcoholL / volumeL) * 100;

        const ratio = describeSugarWaterRatio(sugarKg, waterL);
        const derivedVolumeL = waterL + sugarKg * SUGAR_VOLUME_DISPLACEMENT_L_PER_KG;

        primaryText = `${sugarKg.toFixed(2)} кг сахара, ${waterL.toFixed(1)} л воды`;
        secondaryText = `КПД брожения: ${efficiencyPercent.toFixed(0)}%, ожидаемый объем после растворения ~ ${derivedVolumeL.toFixed(1)} л`;
        ratioText = ratio.ratioText;
        noteText = ratio.note;
    } else {
        const attenuationPercent = parseNumber('calc-potential-attenuation-pct');
        if (!Number.isFinite(attenuationPercent) || attenuationPercent <= 0 || attenuationPercent > 100) {
            alert('Для сусла укажите степень сбраживания.');
            return;
        }

        let originalGravity;
        if (mode === 'brix') {
            const brix = parseNumber('calc-potential-brix');
            if (!Number.isFinite(brix) || brix <= 0 || brix > 40) {
                alert('Укажите корректное значение Brix.');
                return;
            }
            originalGravity = brixToSpecificGravity(brix);
            primaryText = `OG ≈ ${originalGravity.toFixed(3)} из ${brix.toFixed(1)} Brix`;
        } else {
            originalGravity = parseNumber('calc-potential-sg');
            if (!Number.isFinite(originalGravity) || originalGravity <= 1 || originalGravity > 1.2) {
                alert('Укажите корректную начальную плотность SG.');
                return;
            }
            primaryText = `OG: ${originalGravity.toFixed(3)}`;
        }

        const finalGravity = attenuationToFinalGravity(originalGravity, attenuationPercent);
        washAbv = Math.max(0, (originalGravity - finalGravity) * 131.25);
        absoluteAlcoholL = volumeL * washAbv / 100;
        secondaryText = `FG ≈ ${finalGravity.toFixed(3)} при сбраживании ${attenuationPercent.toFixed(0)}%`;
        noteText = 'Соотношение сахар/вода не применяется для расчета по суслу.';
    }

    const product40L = absoluteAlcoholL / 0.4;

    setText('calc-potential-abv', `${washAbv.toFixed(1)} %`);
    setText('calc-potential-aa', formatLiters(absoluteAlcoholL, 2));
    setText('calc-potential-40', formatLiters(product40L, 2));
    setText('calc-potential-primary', primaryText);
    setText('calc-potential-secondary', secondaryText);
    setText('calc-potential-ratio', ratioText);
    setText('calc-potential-note', noteText);

    addLog(
        `Потенциал спирта: ${washAbv.toFixed(1)}%, АС ${absoluteAlcoholL.toFixed(2)} л из ${volumeL.toFixed(1)} л`,
        'info'
    );
}

export function calculateDensityConverter() {
    const scale = selectValue('calc-density-scale', 'brix');
    const rawValue = parseNumber('calc-density-value');
    const targetFg = parseNumber('calc-density-fg');
    const specificGravity = getDensityAsSpecificGravity(scale, rawValue);

    if (!Number.isFinite(specificGravity) || specificGravity <= 0.99 || specificGravity > 1.2) {
        alert('Проверьте входное значение плотности.');
        return;
    }

    const brix = specificGravityToBrix(specificGravity);
    const plato = specificGravityToPlato(specificGravity);
    const oechsle = specificGravityToOechsle(specificGravity);
    const targetFinalGravity = Number.isFinite(targetFg) && targetFg > 0.98 && targetFg < specificGravity ? targetFg : 1.0;
    const potentialAbv = Math.max(0, (specificGravity - targetFinalGravity) * 131.25);

    setText('calc-density-sg', specificGravity.toFixed(3));
    setText('calc-density-brix', `${brix.toFixed(1)} °Bx`);
    setText('calc-density-plato', `${plato.toFixed(1)} °P`);
    setText('calc-density-oechsle', `${oechsle.toFixed(0)} °Oe`);
    setText('calc-density-potential', `${potentialAbv.toFixed(1)} %`);
    setText('calc-density-note', `Потенциал посчитан до FG ${targetFinalGravity.toFixed(3)}.`);

    addLog(
        `Конвертер плотности: SG ${specificGravity.toFixed(3)}, Brix ${brix.toFixed(1)}, потенциал ${potentialAbv.toFixed(1)}%`,
        'info'
    );
}

export function calculateFermentation() {
    const mode = selectValue('calc-ferment-basis', 'sugar');
    const volumeL = parseNumber('calc-ferment-volume-l');
    const fermentationTempC = parseNumber('calc-ferment-temp');
    const profile = getFermentationProfile();

    if (!Number.isFinite(volumeL) || !Number.isFinite(fermentationTempC) || volumeL <= 0) {
        alert('Введите корректный объем партии и температуру брожения.');
        return;
    }

    let originalGravity;
    let finalGravity;
    let washAbv;
    let absoluteAlcoholL;
    let ratioText = 'н/д';
    let ratioValue = NaN;
    let noteText;

    if (mode === 'sugar') {
        const sugarKg = parseNumber('calc-ferment-sugar-kg');
        const waterL = parseNumber('calc-ferment-water-l');
        const efficiencyPercent = parseNumber('calc-ferment-efficiency-pct');

        if (!Number.isFinite(sugarKg) || !Number.isFinite(waterL) || !Number.isFinite(efficiencyPercent) ||
            sugarKg <= 0 || waterL <= 0 || efficiencyPercent <= 0 || efficiencyPercent > 100) {
            alert('Для сахарной браги укажите сахар, воду и КПД брожения.');
            return;
        }

        const sugarBrix = (sugarKg / (sugarKg + waterL)) * 100;
        originalGravity = brixToSpecificGravity(sugarBrix);
        absoluteAlcoholL = sugarKg * SUGAR_TO_AA_L_PER_KG * (efficiencyPercent / 100);
        washAbv = (absoluteAlcoholL / volumeL) * 100;
        finalGravity = Math.max(0.992, originalGravity - (washAbv / 131.25));
        const ratio = describeSugarWaterRatio(sugarKg, waterL);
        ratioText = ratio.ratioText;
        ratioValue = waterL / sugarKg;
        noteText = `${ratio.note} ${buildFermentationNote(profile, fermentationTempC, ratioValue, washAbv)}`;
    } else {
        const attenuationPercent = parseNumber('calc-ferment-attenuation-pct');
        if (!Number.isFinite(attenuationPercent) || attenuationPercent <= 0 || attenuationPercent > 100) {
            alert('Укажите степень сбраживания.');
            return;
        }

        if (mode === 'brix') {
            const brix = parseNumber('calc-ferment-brix');
            if (!Number.isFinite(brix) || brix <= 0 || brix > 40) {
                alert('Укажите корректный Brix.');
                return;
            }
            originalGravity = brixToSpecificGravity(brix);
        } else {
            originalGravity = parseNumber('calc-ferment-sg');
            if (!Number.isFinite(originalGravity) || originalGravity <= 1 || originalGravity > 1.2) {
                alert('Укажите корректное SG.');
                return;
            }
        }

        finalGravity = attenuationToFinalGravity(originalGravity, attenuationPercent);
        washAbv = Math.max(0, (originalGravity - finalGravity) * 131.25);
        absoluteAlcoholL = volumeL * washAbv / 100;
        noteText = buildFermentationNote(profile, fermentationTempC, NaN, washAbv);
    }

    const gravityFactor = 1 + Math.max(0, (originalGravity - 1.07) / 0.01) * 0.04;
    const recommendedYeastG = volumeL * profile.pitchPerLiter * gravityFactor;
    const duration = buildFermentationDuration(profile.baseDays, fermentationTempC, profile.idealTemp, washAbv, ratioValue);

    setText('calc-ferment-og', originalGravity.toFixed(3));
    setText('calc-ferment-fg', finalGravity.toFixed(3));
    setText('calc-ferment-abv', `${washAbv.toFixed(1)} %`);
    setText('calc-ferment-aa', formatLiters(absoluteAlcoholL, 2));
    setText('calc-ferment-ratio', ratioText);
    setText('calc-ferment-yeast-dose', `${recommendedYeastG.toFixed(0)} г сухих дрожжей`);
    setText('calc-ferment-time', `${duration.minDays.toFixed(0)}-${duration.maxDays.toFixed(0)} суток`);
    setText('calc-ferment-note', noteText);

    addLog(
        `Брожение: OG ${originalGravity.toFixed(3)}, FG ${finalGravity.toFixed(3)}, потенциал ${washAbv.toFixed(1)}%`,
        'info'
    );
}

export function calculateReverseBatch() {
    const targetVolumeL = parseNumber('calc-reverse-target-volume');
    const targetAbv = parseNumber('calc-reverse-target-abv');
    const sourceAbv = parseNumber('calc-reverse-source-abv');
    const neutralAbv = parseNumber('calc-reverse-neutral-abv');

    if (!Number.isFinite(targetVolumeL) || !Number.isFinite(targetAbv) || !Number.isFinite(sourceAbv) ||
        !Number.isFinite(neutralAbv) || targetVolumeL <= 0 || targetAbv <= 0 || targetAbv >= 100 ||
        sourceAbv <= 0 || sourceAbv > 100 || neutralAbv <= targetAbv || neutralAbv > 100) {
        alert('Проверьте объем и крепость для обратного расчета.');
        return;
    }

    const requiredAbsoluteAlcoholL = targetVolumeL * targetAbv / 100;
    const sourcePossible = sourceAbv >= targetAbv;
    const sourceVolumeL = sourcePossible ? requiredAbsoluteAlcoholL / (sourceAbv / 100) : NaN;
    const sourceWaterL = sourcePossible ? Math.max(0, targetVolumeL - sourceVolumeL) : NaN;
    const neutralVolumeL = requiredAbsoluteAlcoholL / (neutralAbv / 100);
    const neutralWaterL = Math.max(0, targetVolumeL - neutralVolumeL);

    setText('calc-reverse-aa', formatLiters(requiredAbsoluteAlcoholL, 2));
    setText('calc-reverse-source-volume', sourcePossible ? formatLiters(sourceVolumeL, 2) : 'Нужен более крепкий исходник');
    setText('calc-reverse-source-water', sourcePossible ? formatLiters(sourceWaterL, 2) : '--');
    setText('calc-reverse-neutral-volume', formatLiters(neutralVolumeL, 2));
    setText('calc-reverse-neutral-water', formatLiters(neutralWaterL, 2));

    addLog(
        `Обратный расчет партии: нужно ${requiredAbsoluteAlcoholL.toFixed(2)} л АС для ${targetVolumeL.toFixed(1)} л @ ${targetAbv.toFixed(1)}%`,
        'info'
    );
}

export function calculateHeatingCost() {
    const volumeL = parseNumber('calc-heat-volume');
    const startTempC = parseNumber('calc-heat-start');
    const endTempC = parseNumber('calc-heat-end');
    const heaterPowerW = parseNumber('calc-heat-power');
    const efficiencyPercent = parseNumber('calc-heat-efficiency');
    const tariff = parseNumber('calc-heat-tariff');

    if (!Number.isFinite(volumeL) || !Number.isFinite(startTempC) || !Number.isFinite(endTempC) ||
        !Number.isFinite(heaterPowerW) || !Number.isFinite(efficiencyPercent) ||
        volumeL <= 0 || heaterPowerW <= 0 || efficiencyPercent <= 0 || efficiencyPercent > 100 ||
        endTempC <= startTempC) {
        alert('Проверьте параметры нагрева.');
        return;
    }

    const deltaTemp = endTempC - startTempC;
    const idealEnergyKwh = volumeL * deltaTemp * WATER_HEAT_KWH_PER_LC;
    const actualEnergyKwh = idealEnergyKwh / (efficiencyPercent / 100);
    const heatingTimeHours = actualEnergyKwh / (heaterPowerW / 1000);
    const cost = Number.isFinite(tariff) && tariff >= 0 ? actualEnergyKwh * tariff : 0;

    setText('calc-heat-ideal', formatKwh(idealEnergyKwh));
    setText('calc-heat-actual', formatKwh(actualEnergyKwh));
    setText('calc-heat-time', formatDuration(heatingTimeHours));
    setText('calc-heat-cost', `${formatCurrency(cost)} ₽`);
    setText('calc-heat-note', `ΔT = ${deltaTemp.toFixed(1)}°C, мощность ${formatPower(heaterPowerW)}`);

    addLog(
        `Нагрев: ${actualEnergyKwh.toFixed(2)} кВт·ч, ${formatDuration(heatingTimeHours)}, стоимость ${cost.toFixed(2)} ₽`,
        'info'
    );
}

export function calculateSelectionRate() {
    const diameterMm = parseNumber('calc-select-diameter');
    const packingHeightMm = parseNumber('calc-select-height');
    const heaterPowerW = parseNumber('calc-select-power');
    const desiredRateMlH = parseNumber('calc-select-rate');
    const stage = selectValue('calc-select-stage', 'body');
    const packingType = selectValue('calc-select-packing', 'spn');

    if (!Number.isFinite(diameterMm) || !Number.isFinite(packingHeightMm) || !Number.isFinite(heaterPowerW) ||
        diameterMm <= 0 || packingHeightMm <= 0 || heaterPowerW <= 0) {
        alert('Проверьте геометрию колонны и мощность.');
        return;
    }

    const areaCm2 = Math.PI * ((diameterMm / 20) ** 2);
    const powerLimitPerCm2 = PACKING_POWER_LIMITS[packingType] || PACKING_POWER_LIMITS.spn;
    const packingRateFactor = PACKING_RATE_FACTORS[packingType] || PACKING_RATE_FACTORS.spn;
    const stageFactors = STAGE_RATE_FACTORS[stage] || STAGE_RATE_FACTORS.body;
    const heightFactor = packingHeightMm < 1000 ? 0.9 : packingHeightMm > 1600 ? 1.05 : 1.0;

    const safePowerW = areaCm2 * powerLimitPerCm2 * heightFactor;
    const usablePowerW = Math.min(heaterPowerW, safePowerW);
    const minRateMlH = usablePowerW * stageFactors.min * packingRateFactor;
    const maxRateMlH = usablePowerW * stageFactors.max * packingRateFactor;

    setText('calc-select-safe-power', formatPower(safePowerW));
    setText('calc-select-range', `${Math.round(minRateMlH)}-${Math.round(maxRateMlH)} мл/ч`);
    setText('calc-select-verdict', compareDesiredRate(desiredRateMlH, minRateMlH, maxRateMlH));
    setText('calc-select-note', buildSelectionNote(heaterPowerW, safePowerW, packingHeightMm, stage));

    addLog(
        `Режим отбора: расчетный диапазон ${Math.round(minRateMlH)}-${Math.round(maxRateMlH)} мл/ч`,
        'info'
    );
}

export function calculateBlendFractions() {
    let rows;

    try {
        rows = readBlendRows();
    } catch (error) {
        alert(error.message);
        return;
    }

    if (rows.length === 0) {
        alert('Заполните хотя бы одну фракцию для купажа.');
        return;
    }

    const targetAbv = parseNumber('calc-blend-target-abv');
    const totalVolumeMl = rows.reduce((sum, row) => sum + row.volumeMl, 0);
    const totalAbsoluteAlcoholMl = rows.reduce((sum, row) => sum + (row.volumeMl * row.abv / 100), 0);
    const blendAbv = totalAbsoluteAlcoholMl / totalVolumeMl * 100;

    let dilutionText = 'Целевая крепость не задана';
    if (Number.isFinite(targetAbv) && targetAbv > 0 && targetAbv < 100) {
        if (targetAbv < blendAbv) {
            const targetVolumeMl = totalAbsoluteAlcoholMl / (targetAbv / 100);
            dilutionText = formatLiters((targetVolumeMl - totalVolumeMl) / 1000, 2);
        } else if (Math.abs(targetAbv - blendAbv) < 0.05) {
            dilutionText = 'Вода не требуется';
        } else {
            dilutionText = 'Водой крепость не повысить. Нужен более крепкий компонент.';
        }
    }

    setText('calc-blend-volume-total', formatLiters(totalVolumeMl / 1000, 2));
    setText('calc-blend-aa-total', formatLiters(totalAbsoluteAlcoholMl / 1000, 2));
    setText('calc-blend-abv-total', `${blendAbv.toFixed(1)} %`);
    setText('calc-blend-dilution', dilutionText);

    addLog(
        `Купаж: ${rows.length} фракц., итог ${blendAbv.toFixed(1)}% и ${(totalVolumeMl / 1000).toFixed(2)} л`,
        'info'
    );
}
