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
    if (el) el.textContent = value;
}

function setHidden(id, hidden) {
    const el = document.getElementById(id);
    if (el) el.hidden = hidden;
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

function brixToSpecificGravity(brix) {
    return 1 + (brix / (258.6 - ((brix / 258.2) * 227.1)));
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

    const correction = (20 - tempRaw) * (0.001 * abvRaw + 0.16);
    const realAbv = Math.min(100, Math.max(0, abvRaw + correction));

    setText('calc-abv-result', `${realAbv.toFixed(2)} %`);
}

export function calculateDilution() {
    const sourceVolumeMl = parseNumber('calc-dil-volume');
    const sourceAbv = parseNumber('calc-dil-abv-src');
    const targetAbv = parseNumber('calc-dil-abv-target');

    if (Number.isNaN(sourceVolumeMl) || Number.isNaN(sourceAbv) || Number.isNaN(targetAbv) ||
        sourceVolumeMl <= 0 || targetAbv <= 0 || targetAbv > sourceAbv) {
        alert('Проверьте данные. Желаемая крепость должна быть меньше исходной.');
        return;
    }

    const totalVolumeMl = sourceVolumeMl * (sourceAbv / targetAbv);
    const waterVolumeMl = totalVolumeMl - sourceVolumeMl;

    setText('calc-dil-water', formatMilliliters(waterVolumeMl));
    setText('calc-dil-total', formatMilliliters(totalVolumeMl));
}

export function calculateYieldFractions() {
    const sourceVolumeL = parseNumber('calc-yield-volume-l');
    const sourceAbv = parseNumber('calc-yield-abv');
    const headsPercent = parseNumber('calc-yield-heads-pct');
    const bodyPercent = parseNumber('calc-yield-body-pct');
    const bodyTargetAbv = parseNumber('calc-yield-body-abv');

    const values = [sourceVolumeL, sourceAbv, headsPercent, bodyPercent, bodyTargetAbv];
    if (values.some((value) => Number.isNaN(value))) {
        alert('Введите корректные значения для расчета выхода.');
        return;
    }

    if (sourceVolumeL <= 0 || sourceAbv <= 0 || sourceAbv > 100 || bodyTargetAbv <= 0 || bodyTargetAbv >= 100) {
        alert('Проверьте объем и крепость. Крепость готового продукта должна быть в диапазоне 1-99%.');
        return;
    }

    if (headsPercent < 0 || bodyPercent <= 0 || headsPercent + bodyPercent > 100) {
        alert('Сумма голов и тела не должна превышать 100% абсолютного спирта.');
        return;
    }

    const totalAbsoluteAlcoholL = sourceVolumeL * sourceAbv / 100;
    const headsAbsoluteAlcoholL = totalAbsoluteAlcoholL * headsPercent / 100;
    const bodyAbsoluteAlcoholL = totalAbsoluteAlcoholL * bodyPercent / 100;
    const tailsAbsoluteAlcoholL = Math.max(0, totalAbsoluteAlcoholL - headsAbsoluteAlcoholL - bodyAbsoluteAlcoholL);
    const bodyProductL = bodyAbsoluteAlcoholL / (bodyTargetAbv / 100);

    setText('calc-yield-aa-total', formatLiters(totalAbsoluteAlcoholL, 2));
    setText('calc-yield-heads-aa', formatMilliliters(headsAbsoluteAlcoholL * 1000, 0));
    setText('calc-yield-body-aa', formatLiters(bodyAbsoluteAlcoholL, 2));
    setText('calc-yield-body-product', `${bodyProductL.toFixed(2)} л @ ${bodyTargetAbv.toFixed(1)}%`);
    setText('calc-yield-tails-aa', formatMilliliters(tailsAbsoluteAlcoholL * 1000, 0));

    addLog(
        `Расчет выхода: АС ${totalAbsoluteAlcoholL.toFixed(2)} л, тело ${bodyProductL.toFixed(2)} л @ ${bodyTargetAbv.toFixed(1)}%`,
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

    let washAbv = 0;
    let absoluteAlcoholL = 0;
    let primaryText = '--';
    let secondaryText = '--';
    let ratioText = 'н/д';
    let noteText = '--';

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
