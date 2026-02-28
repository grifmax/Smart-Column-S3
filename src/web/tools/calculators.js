// ============================================================================
// Калькуляторы (Инструменты)
// ============================================================================

export function fetchCurrentTempForCalc() {
    // Пытаемся взять температуру дистиллята (дефлегматор или попугай, если есть датчик)
    // Если нет, берем температуру в кубе как fallback (хотя это неверно для дистиллята, но лучше чем ничего)
    // В идеале нужен отдельный датчик T_distillate
    const tempEl = document.getElementById('temp-reflux'); // Используем дефлегматор как приближение
    const inputEl = document.getElementById('calc-temp-raw');

    if (tempEl && inputEl) {
        const tempText = tempEl.textContent;
        const tempVal = parseFloat(tempText);
        if (!isNaN(tempVal)) {
            inputEl.value = tempVal;
            addLog('🌡️ Температура получена с датчика', 'info');
        } else {
            addLog('⚠️ Нет данных с датчика', 'warning');
        }
    }
}

export function calculateAbvCorrection() {
    const abvRaw = parseFloat(document.getElementById('calc-abv-raw').value);
    const tempRaw = parseFloat(document.getElementById('calc-temp-raw').value);
    const resultEl = document.getElementById('calc-abv-result');

    if (isNaN(abvRaw) || isNaN(tempRaw)) {
        alert('Введите корректные значения');
        return;
    }

    // Упрощенная формула коррекции (достаточно точная для диапазона 10-30°C)
    // C = C_measured - 0.4 * (T - 20) * (1 - C_measured/100)
    // Более точная табличная аппроксимация:
    // Каждые ±1°C меняют показания примерно на ±(0.003 * ABV + 0.15)%

    // Используем общепринятую аппроксимацию для самогонщиков:
    // Реальная крепость = Измеренная + (20 - Т) * Коэффициент
    // Коэффициент зависит от крепости, но в среднем ~0.3-0.4

    const correction = (20 - tempRaw) * (0.001 * abvRaw + 0.16); // Эмпирическая формула
    let realAbv = abvRaw + correction;

    realAbv = Math.min(100, Math.max(0, realAbv));

    resultEl.textContent = realAbv.toFixed(2) + ' %';
}

export function calculateDilution() {
    const V1 = parseFloat(document.getElementById('calc-dil-volume').value); // Исходный объем
    const C1 = parseFloat(document.getElementById('calc-dil-abv-src').value); // Исходная крепость
    const C2 = parseFloat(document.getElementById('calc-dil-abv-target').value); // Желаемая крепость

    if (isNaN(V1) || isNaN(C1) || isNaN(C2) || C2 <= 0 || C2 > C1) {
        alert('Проверьте введенные данные. Желаемая крепость должна быть меньше исходной.');
        return;
    }

    // Формула: V_water = V1 * (C1 / C2) - V1
    const V_total = V1 * (C1 / C2);
    const V_water = V_total - V1;

    document.getElementById('calc-dil-water').textContent = Math.round(V_water) + ' мл';
    document.getElementById('calc-dil-total').textContent = Math.round(V_total) + ' мл';
}
