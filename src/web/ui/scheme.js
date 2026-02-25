// ============================================================================
// Интерактивная схема (SVG)
// ============================================================================

export function updateInteractiveScheme(data) {
    const obj = document.querySelector('.operator-scheme');
    if (!obj) return;

    // Доступ к документу внутри <object>
    const svg = obj.contentDocument;
    if (!svg) {
        // SVG ещё не загружен — подписываемся на load для повторного вызова
        if (!obj._schemeLoadPending) {
            obj._schemeLoadPending = true;
            obj.addEventListener('load', () => {
                obj._schemeLoadPending = false;
                updateInteractiveScheme(data);
            }, { once: true });
        }
        return;
    }

    // Обновление температур на схеме
    if (data.temps) {
        const setTxt = (id, val) => {
            const el = svg.getElementById(id);
            if (el && val !== undefined) el.textContent = val.toFixed(1);
        };
        setTxt('txt-temp-cube', data.temps.cube);
        setTxt('txt-temp-col-top', data.temps.columnTop);
        setTxt('txt-temp-col-mid', data.temps.columnMiddle);
        setTxt('txt-temp-node', data.temps.product);
        setTxt('txt-temp-reflux', data.temps.reflux);
        setTxt('txt-temp-tsa', data.temps.tsa);
        setTxt('txt-water-in', data.temps.waterIn);
        setTxt('txt-water-out', data.temps.waterOut);

        // Капли дефлегматора — видимы при T царги > 70°C
        const refluxZone = svg.getElementById('zone-reflux');
        if (refluxZone) {
            const colMid = data.temps.columnMiddle || 0;
            refluxZone.classList.toggle('condensing', colMid > 70);
        }
    }

    // Обновление давления на схеме
    let pCube = undefined;
    if (data.pressure && data.pressure.cube !== undefined) {
        pCube = data.pressure.cube;
    } else if (data.p_cube !== undefined) {
        pCube = data.p_cube;
    }
    if (pCube !== undefined) {
        const el = svg.getElementById('txt-pressure-cube');
        if (el) el.textContent = pCube.toFixed(1);
    }

    // Обновление ABV на схеме
    const abvEl = svg.getElementById('txt-abv');
    if (abvEl) {
        const abvData = getEffectiveAbvForCalculations();
        abvEl.textContent = abvData.value.toFixed(1) + '%';

        // Визуальная индикация источника (датчик или план)
        const box = abvEl.previousElementSibling;
        if (box) {
            // Если данные с датчика - фиолетовая рамка, иначе серая
            box.style.stroke = abvData.source === 'sensor' ? '#6610f2' : '#7f8c8d';
            box.style.strokeWidth = abvData.source === 'sensor' ? '2' : '1';
        }
    }

    // Анимация ТЭНа
    if (data.power && data.power.power !== undefined) {
        const heater = svg.getElementById('svg-heater');
        if (heater) {
            if (data.power.power > 0) heater.classList.add('heater-on');
            else heater.classList.remove('heater-on');
        }

        // Анимация пара
        const vaporGroup = svg.getElementById('anim-vapor');
        if (vaporGroup) {
            if (data.power.power > 0) vaporGroup.classList.add('vapor-active');
            else vaporGroup.classList.remove('vapor-active');
        }

        // Прогресс-бар мощности
        const powerBar = svg.getElementById('anim-power-bar');
        if (powerBar) {
            const maxW = (runtimeMonitorState && runtimeMonitorState.equipment && runtimeMonitorState.equipment.heaterPowerW)
                ? runtimeMonitorState.equipment.heaterPowerW
                : 3000;
            const currentW = data.power.power;
            const pct = Math.min(1, Math.max(0, currentW / maxW));
            powerBar.setAttribute('width', pct * 160); // 160 - полная ширина бара в SVG
        }

        // Прямоугольник мощности: установленная (%) и фактическая (Вт)
        const powerSetEl = svg.getElementById('txt-power-set');
        if (powerSetEl && data.power.setPercent !== undefined)
            powerSetEl.textContent = data.power.setPercent.toFixed(0) + '%';
        const powerActEl = svg.getElementById('txt-power-actual');
        if (powerActEl && data.power.power !== undefined)
            powerActEl.textContent = data.power.power.toFixed(0) + 'Вт';
    }

    // Состояние клапанов (если данные приходят в status)
    if (data.valves) {
        const updateValve = (id, state) => {
            const el = svg.getElementById(id);
            if (el) el.classList.toggle('valve-open', !!state);
            if (el) el.classList.toggle('valve-closed', !state);
        };
        if (data.valves.water !== undefined) updateValve('svg-valve-water', data.valves.water);
        if (data.valves.heads !== undefined) updateValve('svg-valve-heads', data.valves.heads);
        if (data.valves.uno !== undefined) updateValve('svg-valve-uno', data.valves.uno);
        if (data.valves.tails !== undefined) updateValve('svg-valve-tails', data.valves.tails);

        // Анимация потока воды
        const waterFlow = svg.getElementById('anim-water-flow');
        if (waterFlow && data.valves.water !== undefined) {
            if (data.valves.water) waterFlow.classList.add('flowing');
            else waterFlow.classList.remove('flowing');
        }
    }

    // Анимация уровня жидкости
    const liquidPath = svg.getElementById('anim-liquid-level');
    if (liquidPath && runtimeMonitorState) {
        const s = runtimeMonitorState;
        // Объем куба (л) -> мл
        const maxVol = (s.rectification?.feedVolumeL || 20) * 1000;
        // Отобрано (мл)
        const collected = s.pump?.totalMl || 0;

        // Оставшийся объем (0..1)
        let pct = 1.0;
        if (maxVol > 0) {
            pct = Math.max(0, Math.min(1, (maxVol - collected) / maxVol));
        }

        // Координаты Y для уровня жидкости: Полный (100%): y=480, Пустой (0%): y=600
        const yTop = 600 - (pct * 120);
        const d = `M65,${yTop} L295,${yTop} L295,600 Q295,615 280,615 L80,615 Q65,615 65,600 Z`;
        liquidPath.setAttribute('d', d);
    }

    // Визуализация капель (скорость отбора)
    if ((data.pump && data.pump.speedMlH !== undefined) || data.valves) {
        const speed = runtimeMonitorState.pump.speedMlH || 0;
        const dropHeads = svg.getElementById('drop-heads');
        const dropUno = svg.getElementById('drop-uno');
        const valveHeads = svg.getElementById('svg-valve-heads');
        const valveUno = svg.getElementById('svg-valve-uno');

        const updateDrop = (drop, valveOpen) => {
            if (!drop) return;
            if (speed > 0 && valveOpen) {
                let duration = 180 / speed; // T = 180/S (примерно 20 капель/мл)
                if (duration < 0.1) duration = 0.1;
                if (duration > 2.0) duration = 2.0;
                drop.style.animation = `drop-fall ${duration.toFixed(2)}s infinite linear`;
            } else {
                drop.style.animation = 'none';
                drop.style.opacity = '0';
            }
        };

        const valveTails = svg.getElementById('svg-valve-tails');
        const dropTails = svg.getElementById('drop-tails');

        updateDrop(dropHeads, valveHeads && valveHeads.classList.contains('valve-open'));
        updateDrop(dropUno, valveUno && valveUno.classList.contains('valve-open'));
        updateDrop(dropTails, valveTails && valveTails.classList.contains('valve-open'));

        // Скорость насоса
        const pumpSpeedEl = svg.getElementById('txt-pump-speed');
        if (pumpSpeedEl) pumpSpeedEl.textContent = speed.toFixed(0) + ' мл/ч';

        // Анимация ротора насоса (класс вешается на zone-pump)
        const zonePump = svg.getElementById('zone-pump');
        if (zonePump) zonePump.classList.toggle('pump-running', speed > 0);
    }

    // Индикатор фазы
    const phaseTextEl = svg.getElementById('txt-phase');
    if (phaseTextEl && data.phase !== undefined) {
        const phases = ['ОЖИДАНИЕ', 'НАГРЕВ', 'СТАБИЛ.', 'ГОЛОВЫ', 'ПРОДУВКА', 'ТЕЛО', 'ХВОСТЫ', 'ФИНИШ', 'ОШИБКА'];
        const colors = [
            '#7f8c8d', // IDLE - Gray
            '#e67e22', // HEATING - Orange
            '#f1c40f', // STABIL - Yellow
            '#e74c3c', // HEADS - Red
            '#3498db', // PURGE - Blue
            '#2ecc71', // BODY - Green
            '#9b59b6', // TAILS - Purple
            '#27ae60', // FINISH - Dark Green
            '#c0392b'  // ERROR - Dark Red
        ];
        const idx = Number(data.phase);
        if (idx >= 0 && idx < phases.length) {
            phaseTextEl.textContent = phases[idx];
            phaseTextEl.setAttribute('fill', colors[idx]);
        }
    }

    // Анимация наполнения банок (Heads / Body)
    if (runtimeMonitorState) {
        const s = runtimeMonitorState;

        // Heads Jar
        const headsVol = s.volumes?.heads || 0;
        // Если цель не задана, берем примерную (5% от 40% спирта в 20л сырца ~ 400мл) или дефолт 300мл
        let headsMax = s.rectification?.headsTargetMl || 0;
        if (headsMax === 0 && s.rectification?.feedVolumeL) {
            headsMax = s.rectification.feedVolumeL * 1000 * 0.4 * 0.05;
        }
        if (headsMax === 0) headsMax = 300;

        const headsPct = Math.min(1, headsVol / headsMax);
        const headsH = 96; // Высота SVG jar (новый SVG 415×737)
        const headsHFill = headsH * headsPct;

        const elHeads = svg.getElementById('anim-liquid-heads');
        if (elHeads) {
            elHeads.setAttribute('y', headsH - headsHFill);
            elHeads.setAttribute('height', headsHFill);
        }

        // Body Jar
        const bodyVol = s.volumes?.body || 0;
        let bodyMax = s.rectification?.bodyTargetMl || 0;
        if (bodyMax === 0 && s.rectification?.feedVolumeL) {
            bodyMax = s.rectification.feedVolumeL * 1000 * 0.4 * 0.4; // ~3.2л
        }
        if (bodyMax === 0) bodyMax = 3000;

        const bodyPct = Math.min(1, bodyVol / bodyMax);
        const bodyH = 117; // Высота SVG jar (новый SVG 415×737)
        const bodyHFill = bodyH * bodyPct;

        const elBody = svg.getElementById('anim-liquid-body');
        if (elBody) {
            elBody.setAttribute('y', bodyH - bodyHFill);
            elBody.setAttribute('height', bodyHFill);
        }

        // Tails Jar
        const tailsVol = s.volumes?.tails || 0;
        let tailsMax = s.rectification?.tailsTargetMl || 0;
        if (tailsMax === 0 && s.rectification?.feedVolumeL) {
            tailsMax = s.rectification.feedVolumeL * 1000 * 0.4 * 0.1; // ~0.8л
        }
        if (tailsMax === 0) tailsMax = 800;

        const tailsPct = Math.min(1, tailsVol / tailsMax);
        const tailsH = 95; // Высота SVG jar (новый SVG 415×737)
        const tailsHFill = tailsH * tailsPct;

        const elTails = svg.getElementById('anim-liquid-tails');
        if (elTails) {
            elTails.setAttribute('y', tailsH - tailsHFill);
            elTails.setAttribute('height', tailsHFill);
        }
    }

    // Анимация конденсации (если есть нагрев и включена вода)
    const condensateGroup = svg.getElementById('anim-condensate');
    const heater = svg.getElementById('svg-heater');
    const waterFlow = svg.getElementById('anim-water-flow');

    if (condensateGroup && heater && waterFlow) {
        const isHeating = heater.classList.contains('heater-on');
        const isCooling = waterFlow.classList.contains('flowing');

        if (isHeating && isCooling) condensateGroup.classList.add('condensing');
        else condensateGroup.classList.remove('condensing');
    }
}
