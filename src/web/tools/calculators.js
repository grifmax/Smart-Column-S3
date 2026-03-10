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
