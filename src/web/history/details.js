import { historyData } from './list.js';

export async function viewHistoryDetails(id) {

    try {

        const response = await fetch(`/api/history/${id}`);

        if (!response.ok) {

            throw new Error('Не удалось загрузить детали истории');

        }



        const process = await response.json();
        const previousSummary = findPreviousSuccessfulProcessSummary(process);
        let previousSuccessfulProcess = null;

        if (previousSummary?.id) {

            try {

                const previousResponse = await fetch(`/api/history/${previousSummary.id}`);

                if (previousResponse.ok) {

                    previousSuccessfulProcess = await previousResponse.json();

                }

            } catch (compareError) {

                console.warn('Не удалось загрузить эталонный прогон для Run Advisor:', compareError);

            }

        }



        // Создать модальное окно с деталями

        showHistoryDetailsModal(process, {
            previousSummary,
            previousSuccessfulProcess
        });



        addLog(`👁️ Просмотр процесса ${id}`, 'info');

    } catch (error) {

        console.error('Ошибка загрузки деталей истории:', error);

        addLog('❌ Ошибка загрузки деталей процесса', 'error');

        alert('Ошибка загрузки деталей процесса');

    }

}



export let tempChart = null;

export let powerChart = null;

function normalizeProfileKey(value) {

    return String(value || '').trim().toLowerCase();

}

function getProcessProfile(process) {

    return String(process?.process?.profile || '').trim();

}

function findPreviousSuccessfulProcessSummary(process) {

    const currentId = String(process?.id || '').trim();
    const currentType = String(process?.process?.type || '').trim();
    const currentProfileKey = normalizeProfileKey(getProcessProfile(process));
    const currentStartTime = Number(process?.metadata?.startTime || 0);

    if (!currentType || !currentProfileKey || currentStartTime <= 0) {

        return null;

    }

    return [...historyData]
        .filter((item) => String(item?.id || '').trim() !== currentId)
        .filter((item) => String(item?.type || '').trim() === currentType)
        .filter((item) => normalizeProfileKey(item?.profile) === currentProfileKey)
        .filter((item) => Number(item?.startTime || 0) < currentStartTime)
        .filter((item) => Boolean(item?.completedSuccessfully) || String(item?.status || '').trim() === 'completed')
        .sort((left, right) => Number(right?.startTime || 0) - Number(left?.startTime || 0))[0] || null;

}

const SAFETY_REASON_LABELS = {
    RC_SAFETY_LIMIT_POWER: 'Ограничение мощности',
    RC_SAFETY_LIMIT_TAKEOFF: 'Ограничение отбора',
    RC_SAFETY_PHASE_BLOCKED: 'Переход фазы заблокирован',
    RC_SAFETY_RECOVERY_ENTERED: 'Условия безопасности восстановлены',
    RC_SAFETY_RECOVERY_EXITED: 'Режим восстановления завершён',
    RC_SAFETY_TRIP_PRESSURE: 'Авария по давлению',
    RC_SAFETY_TRIP_SENSOR: 'Авария по датчикам',
    RC_SAFETY_TRIP_OVERHEAT: 'Авария по перегреву',
    RC_SAFETY_TRIP_POWER: 'Авария по питанию',
    RC_SAFETY_TRIP_GENERIC: 'Неидентифицированная авария',
    RC_SAFETY_ACKNOWLEDGED: 'Авария подтверждена оператором',
    RC_SAFETY_RESET_COMPLETED: 'Авария сброшена оператором',
    RC_OPERATOR_SERVICE_ACTION: 'Сервисное действие оператора'
};

function formatReasonCode(reasonCode) {

    const raw = String(reasonCode || '').trim();

    if (!raw || raw === 'RC_NONE') {

        return '';

    }

    return raw.replace(/^RC_/, '').replace(/_/g, ' ').toLowerCase();

}

function appendInfoItem(container, label, value) {

    const item = document.createElement('div');

    item.className = 'modal-info-item';

    const labelEl = document.createElement('div');

    labelEl.className = 'modal-info-label';

    labelEl.textContent = label;

    const valueEl = document.createElement('div');

    valueEl.className = 'modal-info-value';

    valueEl.textContent = value;

    item.appendChild(labelEl);

    item.appendChild(valueEl);

    container.appendChild(item);

}

function appendPhaseDetail(container, label, value) {

    const detail = document.createElement('div');

    detail.className = 'modal-phase-detail';

    detail.appendChild(document.createTextNode(`${label}: `));

    const strong = document.createElement('strong');

    strong.textContent = value;

    detail.appendChild(strong);

    container.appendChild(detail);

}

function getReasonLabel(reasonCode) {

    const raw = String(reasonCode || '').trim();

    if (!raw || raw === 'RC_NONE') {

        return '';

    }

    return SAFETY_REASON_LABELS[raw] || formatReasonCode(raw);

}

function getHistoryReasonInsight(reasonCode, operatorMessage = '', completedSuccessfully = false) {

    const raw = String(reasonCode || '').trim();
    const message = String(operatorMessage || '').trim();
    const label = getReasonLabel(raw) || 'Итог процесса';

    if (message) {
        return {
            tone: raw.startsWith('RC_SAFETY_') ? 'danger' : (completedSuccessfully ? 'good' : 'warn'),
            title: label,
            detail: message,
            action: raw.startsWith('RC_SAFETY_')
                ? 'Перед следующим запуском нужно устранить причину safety-события, а не только повторить старт.'
                : 'Используйте этот комментарий как главный контекст при сравнении прогона с успешными запусками.'
        };
    }

    switch (raw) {
        case 'RC_NONE':
        case '':
            return {
                tone: completedSuccessfully ? 'good' : 'muted',
                title: completedSuccessfully ? 'Цикл завершён' : 'Причина не зафиксирована',
                detail: completedSuccessfully
                    ? 'Процесс завершился без явной финальной причины в истории переходов.'
                    : 'Для этого прогона не удалось восстановить явную финальную причину завершения.',
                action: completedSuccessfully
                    ? 'Ориентируйтесь на фазы, графики и итоговые показатели качества.'
                    : 'Сверьте хронологию безопасности, предупреждения и последние фазы процесса.'
            };
        case 'RC_MODE_STOP_REQUEST':
        case 'RC_MANUAL_OPERATOR_STOP':
            return {
                tone: 'warn',
                title: label,
                detail: 'Цикл был остановлен оператором, поэтому финал нельзя считать полностью автоматическим эталоном.',
                action: 'При сравнении с другими прогонами учитывайте, что завершение было ручным.'
            };
        case 'RC_SAFETY_TRIP_PRESSURE':
        case 'RC_SAFETY_TRIP_SENSOR':
        case 'RC_SAFETY_TRIP_OVERHEAT':
        case 'RC_SAFETY_TRIP_POWER':
        case 'RC_SAFETY_TRIP_GENERIC':
            return {
                tone: 'danger',
                title: label,
                detail: 'Процесс завершился аварийным safety-событием, поэтому результат нужно трактовать как защитную остановку.',
                action: 'Перед повторением сценария проверьте первопричину trip по датчикам, давлению, охлаждению и питанию.'
            };
        case 'RC_SAFETY_LIMIT_POWER':
        case 'RC_SAFETY_LIMIT_TAKEOFF':
        case 'RC_SAFETY_PHASE_BLOCKED':
            return {
                tone: 'warn',
                title: label,
                detail: 'Автоматика завершала или ограничивала цикл через защитные лимиты, а не в полностью свободном рабочем окне.',
                action: 'Для следующего прогона проверьте охлаждение, стабильность колонны и корректность профиля.'
            };
        case 'RC_SAFETY_RECOVERY_ENTERED':
        case 'RC_PHASE_RECOVERY_APPLIED':
            return {
                tone: 'warn',
                title: label,
                detail: 'История показывает вход в recovery или восстановление фазы, значит процесс пережил нестабильный участок.',
                action: 'Смотрите графики и хронологию safety, чтобы понять, где автоматика потеряла устойчивость.'
            };
        case 'RC_HEADS_VOLUME_REACHED':
        case 'RC_HEADS_SCORE_REACHED':
        case 'RC_BODY_TARGET_VOLUME_REACHED':
        case 'RC_BODY_END_DETECTED':
        case 'RC_TAILS_TARGET_REACHED':
        case 'RC_FINISH_COOLDOWN_COMPLETE':
        case 'RC_DISTILLATION_END_TEMP_REACHED':
        case 'RC_DISTILLATION_TARGET_VOLUME_REACHED':
        case 'RC_TEMP_STEP_HOLD_COMPLETE':
        case 'RC_FERM_TARGET_REACHED':
            return {
                tone: completedSuccessfully ? 'good' : 'warn',
                title: label,
                detail: 'Процесс дошёл до ожидаемой технологической точки завершения или перехода по правилам автоматики.',
                action: 'Используйте этот прогон как материал для сравнения профиля, выхода и энергозатрат.'
            };
        default:
            return {
                tone: completedSuccessfully ? 'good' : 'muted',
                title: label,
                detail: 'В истории есть финальная причина, но для неё ещё нет отдельной расширенной расшифровки.',
                action: 'Ориентируйтесь на последние фазы, графики, предупреждения и safety timeline.'
            };
    }

}

function getProcessCompletionInsight(process) {

    const phases = Array.isArray(process?.phases) ? process.phases : [];
    const warnings = Array.isArray(process?.results?.warnings) ? process.results.warnings : [];
    const errors = Array.isArray(process?.results?.errors) ? process.results.errors : [];
    const completedSuccessfully = Boolean(process?.metadata?.completedSuccessfully);

    const lastPhase = phases.length > 0 ? phases[phases.length - 1] : null;
    const lastPhaseReason = String(lastPhase?.reasonCode || '').trim();
    const lastPhaseMessage = String(lastPhase?.operatorMessage || '').trim();

    if (lastPhaseReason || lastPhaseMessage) {
        return getHistoryReasonInsight(lastPhaseReason, lastPhaseMessage, completedSuccessfully);
    }

    const lastEventWithReason = [...errors, ...warnings]
        .filter((eventItem) => String(eventItem?.reasonCode || '').trim() || String(eventItem?.operatorMessage || '').trim())
        .sort((left, right) => Number(left?.time || 0) - Number(right?.time || 0))
        .pop();

    if (lastEventWithReason) {
        return getHistoryReasonInsight(
            String(lastEventWithReason.reasonCode || ''),
            String(lastEventWithReason.operatorMessage || ''),
            completedSuccessfully
        );
    }

    return getHistoryReasonInsight('', '', completedSuccessfully);

}

function appendCompletionInsightSection(container, process) {

    const insight = getProcessCompletionInsight(process);
    const section = document.createElement('div');
    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');
    titleEl.className = 'modal-info-label';
    titleEl.textContent = 'Итог сценария';

    const cardEl = document.createElement('div');
    cardEl.className = `modal-history-insight is-${insight.tone}`;

    const headEl = document.createElement('div');
    headEl.className = 'modal-history-insight-head';

    const insightTitleEl = document.createElement('strong');
    insightTitleEl.textContent = insight.title;

    headEl.appendChild(insightTitleEl);

    const detailEl = document.createElement('p');
    detailEl.className = 'modal-history-insight-text';
    detailEl.textContent = insight.detail;

    const actionEl = document.createElement('p');
    actionEl.className = 'modal-history-insight-action';
    actionEl.textContent = insight.action;

    cardEl.appendChild(headEl);
    cardEl.appendChild(detailEl);
    cardEl.appendChild(actionEl);

    section.appendChild(titleEl);
    section.appendChild(cardEl);
    container.appendChild(section);

}

function isSafetyEvent(eventItem) {

    const reasonCode = String(eventItem?.reasonCode || '').trim();
    const message = String(eventItem?.message || '').toLowerCase();

    return reasonCode.startsWith('RC_SAFETY_') ||
        message.includes('safety') ||
        message.includes('авари') ||
        message.includes('перегрев') ||
        message.includes('давлен');

}

function getSafetyTimelineTone(eventItem) {

    const reasonCode = String(eventItem?.reasonCode || '').trim();
    const severity = String(eventItem?.severity || '').trim().toLowerCase();

    if (reasonCode === 'RC_SAFETY_RECOVERY_ENTERED' || reasonCode === 'RC_SAFETY_RECOVERY_EXITED') {

        return 'recovery';

    }

    if (reasonCode === 'RC_SAFETY_ACKNOWLEDGED' || reasonCode === 'RC_SAFETY_RESET_COMPLETED') {

        return 'info';

    }

    if (
        reasonCode === 'RC_SAFETY_LIMIT_POWER' ||
        reasonCode === 'RC_SAFETY_LIMIT_TAKEOFF' ||
        reasonCode === 'RC_SAFETY_PHASE_BLOCKED'
    ) {

        return 'limited';

    }

    if (severity === 'error' || reasonCode.startsWith('RC_SAFETY_TRIP_')) {

        return 'error';

    }

    return 'warning';

}

function buildSafetyTimeline(process) {

    const errors = Array.isArray(process?.results?.errors) ? process.results.errors : [];
    const warnings = Array.isArray(process?.results?.warnings) ? process.results.warnings : [];

    return [...errors, ...warnings]
        .filter(isSafetyEvent)
        .map((eventItem) => ({
            ...eventItem,
            tone: getSafetyTimelineTone(eventItem),
            title: getReasonLabel(eventItem?.reasonCode) || String(eventItem?.severity || 'Событие')
        }))
        .sort((left, right) => Number(left?.time || 0) - Number(right?.time || 0));

}

function appendSafetyTimelineSection(container, process) {

    const events = buildSafetyTimeline(process);

    if (events.length === 0) {

        return;

    }

    const section = document.createElement('div');

    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');

    titleEl.className = 'modal-info-label';

    titleEl.textContent = 'Хронология безопасности';

    const countEl = document.createElement('div');

    countEl.className = 'modal-info-value';

    countEl.textContent = `${events.length} ${events.length === 1 ? 'событие' : (events.length < 5 ? 'события' : 'событий')}`;

    const listEl = document.createElement('div');

    listEl.className = 'modal-event-list';

    events.forEach((eventItem) => {

        const row = document.createElement('div');

        row.className = `modal-event-item is-${eventItem.tone}`;

        const headerEl = document.createElement('div');

        headerEl.className = 'modal-event-header';

        const kindEl = document.createElement('div');

        kindEl.className = `modal-event-kind is-${eventItem.tone}`;

        kindEl.textContent = eventItem.title;

        headerEl.appendChild(kindEl);

        const timestamp = Number(eventItem?.time || 0);

        if (timestamp > 0) {

            const metaEl = document.createElement('div');

            metaEl.className = 'modal-event-meta';

            metaEl.textContent = new Date(timestamp * 1000).toLocaleString('ru-RU');

            headerEl.appendChild(metaEl);

        }

        row.appendChild(headerEl);

        const messageEl = document.createElement('div');

        messageEl.className = 'modal-event-message';

        messageEl.textContent = String(eventItem?.message || 'Без текста');

        row.appendChild(messageEl);

        const reasonLabel = getReasonLabel(eventItem?.reasonCode);

        if (reasonLabel) {

            const reasonEl = document.createElement('div');

            reasonEl.className = 'modal-event-extra';

            reasonEl.textContent = `Код: ${reasonLabel}`;

            row.appendChild(reasonEl);

        }

        const operatorMessage = String(eventItem?.operatorMessage || '').trim();

        if (operatorMessage) {

            const operatorEl = document.createElement('div');

            operatorEl.className = 'modal-event-extra';

            operatorEl.textContent = `Комментарий: ${operatorMessage}`;

            row.appendChild(operatorEl);

        }

        listEl.appendChild(row);

    });

    section.appendChild(titleEl);

    section.appendChild(countEl);

    section.appendChild(listEl);

    container.appendChild(section);

}

function appendEventSection(container, title, events, tone) {

    if (!Array.isArray(events) || events.length === 0) {

        return;

    }

    const section = document.createElement('div');

    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');

    titleEl.className = 'modal-info-label';

    titleEl.textContent = title;

    const countEl = document.createElement('div');

    countEl.className = 'modal-info-value';

    countEl.textContent = `${events.length} ${events.length === 1 ? 'событие' : (events.length < 5 ? 'события' : 'событий')}`;

    const listEl = document.createElement('div');

    listEl.className = 'modal-event-list';

    events.forEach((eventItem) => {

        const row = document.createElement('div');

        row.className = `modal-event-item ${tone === 'error' ? 'is-error' : 'is-warning'}`;

        const timestamp = Number(eventItem?.time || 0);

        if (timestamp > 0) {

            const metaEl = document.createElement('div');

            metaEl.className = 'modal-event-meta';

            metaEl.textContent = new Date(timestamp * 1000).toLocaleString('ru-RU');

            row.appendChild(metaEl);

        }

        const messageEl = document.createElement('div');

        messageEl.className = 'modal-event-message';

        messageEl.textContent = String(eventItem?.message || 'Без текста');

        row.appendChild(messageEl);

        const reasonCode = formatReasonCode(eventItem?.reasonCode);

        if (reasonCode) {

            const reasonEl = document.createElement('div');

            reasonEl.className = 'modal-event-extra';

            reasonEl.textContent = `Причина: ${reasonCode}`;

            row.appendChild(reasonEl);

        }

        const operatorMessage = String(eventItem?.operatorMessage || '').trim();

        if (operatorMessage) {

            const operatorEl = document.createElement('div');

            operatorEl.className = 'modal-event-extra';

            operatorEl.textContent = `Комментарий: ${operatorMessage}`;

            row.appendChild(operatorEl);

        }

        listEl.appendChild(row);

    });

    section.appendChild(titleEl);

    section.appendChild(countEl);

    section.appendChild(listEl);

    container.appendChild(section);

}

function appendNotesSection(container, notes) {

    const text = String(notes || '').trim();

    if (!text) {

        return;

    }

    const section = document.createElement('div');

    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');

    titleEl.className = 'modal-info-label';

    titleEl.textContent = 'Заметки';

    const textEl = document.createElement('div');

    textEl.className = 'modal-note-text';

    textEl.textContent = text;

    section.appendChild(titleEl);

    section.appendChild(textEl);

    container.appendChild(section);

}

function appendIndicatorSummarySection(container, process) {

    const indicators = process?.metrics?.indicators;

    if (!indicators) {

        return;

    }

    const samples = Number(indicators.samples || 0);

    if (samples <= 0) {

        return;

    }

    const share = (value) => `${((Number(value || 0) / samples) * 100).toFixed(0)}%`;

    appendInfoItem(container, 'Здоровье процесса', `${(Number(indicators.processHealth?.avg || 0) * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Мин. здоровье', `${(Number(indicators.processHealth?.min || 0) * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Стабильность', `${(Number(indicators.stabilityIndexAvg || 0) * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Риск захлёба', `${(Number(indicators.floodRisk?.avg || 0) * 100).toFixed(0)}% / max ${(Number(indicators.floodRisk?.max || 0) * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Запас охлаждения', `${Number(indicators.coolingMarginC?.avg || 0).toFixed(1)}°C / min ${Number(indicators.coolingMarginC?.min || 0).toFixed(1)}°C`);
    appendInfoItem(container, 'Отбор разрешён', share(indicators.takeoffAllowedSamples));
    appendInfoItem(container, 'Свежесть датчиков', share(indicators.sensorFreshnessOkSamples));
    appendInfoItem(container, 'Финальные score', `heads ${(Number(indicators.headsCompletionScoreFinal || 0) * 100).toFixed(0)}%, body ${(Number(indicators.bodyEndScoreFinal || 0) * 100).toFixed(0)}%`);

}

function getPhase(process, phaseNames) {

    const names = Array.isArray(phaseNames) ? phaseNames : [phaseNames];
    const normalized = names.map((item) => String(item || '').trim()).filter(Boolean);
    const phases = Array.isArray(process?.phases) ? process.phases : [];

    return phases.find((phase) => normalized.includes(String(phase?.name || '').trim())) || null;

}

function getEnergyPerLiter(process) {

    const energyUsed = Number(process?.metrics?.power?.energyUsed || 0);
    const totalCollectedMl = Number(process?.results?.totalCollected || 0);

    if (energyUsed <= 0 || totalCollectedMl <= 0) {

        return null;

    }

    return energyUsed / (totalCollectedMl / 1000);

}

function getIndicatorShares(process) {

    const indicators = process?.metrics?.indicators;
    const samples = Number(indicators?.samples || 0);

    if (!indicators || samples <= 0) {

        return null;

    }

    return {
        takeoffShare: Number(indicators.takeoffAllowedSamples || 0) / samples,
        freshnessShare: Number(indicators.sensorFreshnessOkSamples || 0) / samples,
        avgStability: Number(indicators.stabilityIndexAvg || 0),
        minCoolingMargin: Number(indicators.coolingMarginC?.min || 0),
        maxFloodRisk: Number(indicators.floodRisk?.max || 0)
    };

}

function formatSignedPercent(deltaFraction, digits = 0) {

    const sign = deltaFraction > 0 ? '+' : '';
    return `${sign}${(deltaFraction * 100).toFixed(digits)}%`;

}

function formatSignedNumber(delta, digits = 1, suffix = '') {

    const sign = delta > 0 ? '+' : '';
    return `${sign}${Number(delta || 0).toFixed(digits)}${suffix}`;

}

function buildProfileComparisonItems(process, previousProcess, previousSummary) {

    const profileName = getProcessProfile(process);

    if (!profileName) {
        return [{
            tone: 'muted',
            title: 'У прогона нет привязанного профиля',
            detail: 'Run Advisor не может подобрать эталонный запуск, если процесс сохранён без имени профиля.',
            action: 'Сохраняйте и запускайте режим через профиль, чтобы включить сравнение с успешной историей.'
        }];
    }

    if (!previousProcess) {
        return [{
            tone: 'muted',
            title: 'Ещё нет эталонного успешного прогона',
            detail: `Для профиля "${profileName}" не найден предыдущий успешный запуск в истории.`,
            action: 'Первый успешный прогон этого профиля станет базой для следующего сравнения.'
        }];
    }

    const items = [];
    const previousDate = new Date(Number(previousProcess?.metadata?.startTime || previousSummary?.startTime || 0) * 1000);
    const currentDuration = Number(process?.metadata?.duration || 0);
    const previousDuration = Number(previousProcess?.metadata?.duration || 0);
    const currentHeating = Number(getPhase(process, 'heating')?.duration || 0);
    const previousHeating = Number(getPhase(previousProcess, 'heating')?.duration || 0);
    const currentStabilization = Number(getPhase(process, ['stabilization', 'post_heads_stabilization'])?.duration || 0);
    const previousStabilization = Number(getPhase(previousProcess, ['stabilization', 'post_heads_stabilization'])?.duration || 0);
    const currentEnergyPerLiter = getEnergyPerLiter(process);
    const previousEnergyPerLiter = getEnergyPerLiter(previousProcess);
    const currentIndicators = getIndicatorShares(process);
    const previousIndicators = getIndicatorShares(previousProcess);

    items.push({
        tone: 'muted',
        title: `Эталон профиля: ${profileName}`,
        detail: `Сравнение с успешным прогоном от ${previousDate.toLocaleString('ru-RU')} для того же профиля.`,
        action: 'Ниже показаны главные отклонения текущего запуска от последнего успешного эталона.'
    });

    if (currentHeating > 0 && previousHeating > 0) {
        const heatingDelta = (currentHeating - previousHeating) / previousHeating;
        if (Math.abs(heatingDelta) >= 0.12) {
            items.push({
                tone: heatingDelta > 0 ? 'warn' : 'good',
                title: heatingDelta > 0 ? 'Разгон стал заметно дольше' : 'Разгон стал быстрее эталона',
                detail: `Фаза нагрева ${Math.round(currentHeating / 60)} мин против ${Math.round(previousHeating / 60)} мин (${formatSignedPercent(heatingDelta)}).`,
                action: heatingDelta > 0
                    ? 'Проверьте мощность разгона, стартовый объём, напряжение сети и теплопотери.'
                    : 'Быстрый разгон полезен, но проверьте, не вырос ли риск захлёба в начале рабочего окна.'
            });
        }
    }

    if (currentStabilization > 0 && previousStabilization > 0) {
        const stabilizationDelta = (currentStabilization - previousStabilization) / previousStabilization;
        if (Math.abs(stabilizationDelta) >= 0.15) {
            items.push({
                tone: stabilizationDelta > 0 ? 'warn' : 'good',
                title: stabilizationDelta > 0 ? 'Стабилизация затянулась' : 'Стабилизация проходит быстрее',
                detail: `Рабочая выдержка ${Math.round(currentStabilization / 60)} мин против ${Math.round(previousStabilization / 60)} мин (${formatSignedPercent(stabilizationDelta)}).`,
                action: stabilizationDelta > 0
                    ? 'Если это повторяется, проверьте охлаждение, давление и не завышена ли нагрузка на колонну.'
                    : 'Ускорение хорошее только если при этом не просели стабильность и качество отбора.'
            });
        }
    }

    if (currentIndicators && previousIndicators) {
        const stabilityDelta = currentIndicators.avgStability - previousIndicators.avgStability;
        const takeoffDelta = currentIndicators.takeoffShare - previousIndicators.takeoffShare;
        const floodDelta = currentIndicators.maxFloodRisk - previousIndicators.maxFloodRisk;
        const coolingDelta = currentIndicators.minCoolingMargin - previousIndicators.minCoolingMargin;

        if (stabilityDelta <= -0.08 || takeoffDelta <= -0.10 || floodDelta >= 0.12 || coolingDelta <= -1.0) {
            items.push({
                tone: 'warn',
                title: 'Рабочее окно хуже эталона',
                detail: `Стабильность ${(currentIndicators.avgStability * 100).toFixed(0)}% vs ${(previousIndicators.avgStability * 100).toFixed(0)}%, отбор разрешён ${(currentIndicators.takeoffShare * 100).toFixed(0)}% vs ${(previousIndicators.takeoffShare * 100).toFixed(0)}%, flood risk ${(currentIndicators.maxFloodRisk * 100).toFixed(0)}% vs ${(previousIndicators.maxFloodRisk * 100).toFixed(0)}%.`,
                action: 'Для следующего запуска начните мягче: проверьте охлаждение, давление и первые минуты отбора.'
            });
        } else if (stabilityDelta >= 0.08 || takeoffDelta >= 0.10 || (floodDelta <= -0.12 && coolingDelta >= 1.0)) {
            items.push({
                tone: 'good',
                title: 'Рабочее окно стало устойчивее',
                detail: `Стабильность ${(currentIndicators.avgStability * 100).toFixed(0)}% vs ${(previousIndicators.avgStability * 100).toFixed(0)}%, запас охлаждения ${currentIndicators.minCoolingMargin.toFixed(1)}°C vs ${previousIndicators.minCoolingMargin.toFixed(1)}°C.`,
                action: 'Этот прогон выглядит сильнее эталона. Его уже можно рассматривать как новый опорный запуск профиля.'
            });
        }
    }

    if (currentEnergyPerLiter !== null && previousEnergyPerLiter !== null) {
        const energyDelta = (currentEnergyPerLiter - previousEnergyPerLiter) / previousEnergyPerLiter;
        const durationDelta = previousDuration > 0 ? (currentDuration - previousDuration) / previousDuration : 0;

        if (Math.abs(energyDelta) >= 0.10 || Math.abs(durationDelta) >= 0.12) {
            items.push({
                tone: energyDelta > 0 || durationDelta > 0 ? 'warn' : 'good',
                title: energyDelta > 0 || durationDelta > 0 ? 'Экономика прогона хуже эталона' : 'Экономика прогона лучше эталона',
                detail: `Энергия на литр ${currentEnergyPerLiter.toFixed(2)} против ${previousEnergyPerLiter.toFixed(2)} кВт·ч/л (${formatSignedPercent(energyDelta)}), общая длительность ${formatSignedNumber((currentDuration - previousDuration) / 3600, 1, ' ч')}.`,
                action: energyDelta > 0 || durationDelta > 0
                    ? 'Посмотрите, где ушло время: разгон, стабилизация или узкое окно отбора.'
                    : 'Текущий профиль даёт более быстрый или более энергоэффективный результат без явного инженерного штрафа.'
            });
        }
    }

    return items.slice(0, 4);

}

function buildRunAdvisorItems(process, previousSuccessfulProcess = null, previousSummary = null) {

    const indicators = process?.metrics?.indicators;
    const type = String(process?.process?.type || '').trim();
    const completedSuccessfully = Boolean(process?.metadata?.completedSuccessfully);
    const comparisonItems = buildProfileComparisonItems(process, previousSuccessfulProcess, previousSummary);
    const items = [...comparisonItems];

    if (!indicators) {
        return items.slice(0, 4);
    }

    const avgStability = Number(indicators.stabilityIndexAvg || 0);
    const minCoolingMargin = Number(indicators.coolingMarginC?.min || 0);
    const maxFloodRisk = Number(indicators.floodRisk?.max || 0);
    const samples = Math.max(1, Number(indicators.samples || 0));
    const takeoffShare = Number(indicators.takeoffAllowedSamples || 0) / samples;
    const freshnessShare = Number(indicators.sensorFreshnessOkSamples || 0) / samples;
    const finalHeadsScore = Number(indicators.headsCompletionScoreFinal || 0);
    const finalBodyScore = Number(indicators.bodyEndScoreFinal || 0);

    if (freshnessShare < 0.95) {
        items.push({
            tone: 'danger',
            title: 'Телеметрия была неполной',
            detail: `Свежие данные были доступны только ${(freshnessShare * 100).toFixed(0)}% времени.`,
            action: 'Перед сравнением профилей сначала стабилизируйте датчики и качество потока данных.'
        });
    }

    if (minCoolingMargin <= 0) {
        items.push({
            tone: 'danger',
            title: 'Охлаждение уходило в красную зону',
            detail: `Минимальный cooling margin опускался до ${minCoolingMargin.toFixed(1)}°C.`,
            action: 'Для следующего запуска проверьте воду, дефлегматор и не форсируйте мощность на этом профиле.'
        });
    } else if (minCoolingMargin < 4) {
        items.push({
            tone: 'warn',
            title: 'Запас охлаждения был низким',
            detail: `Минимальный cooling margin всего ${minCoolingMargin.toFixed(1)}°C.`,
            action: 'Есть смысл немного разгрузить колонну или заранее усилить охлаждение.'
        });
    }

    if (maxFloodRisk >= 0.8) {
        items.push({
            tone: 'danger',
            title: 'Высокий риск захлёба',
            detail: `Пиковый flood risk достигал ${(maxFloodRisk * 100).toFixed(0)}%.`,
            action: 'Следующий прогон лучше начать мягче: меньше мощность, меньше отбор, внимательнее к давлению.'
        });
    } else if (maxFloodRisk >= 0.55) {
        items.push({
            tone: 'warn',
            title: 'Колонна работала на грани',
            detail: `Максимальный flood risk доходил до ${(maxFloodRisk * 100).toFixed(0)}%.`,
            action: 'Профиль рабочий, но запас устойчивости небольшой. Хорошая точка для аккуратной оптимизации.'
        });
    }

    if ((type === 'rectification' || type === 'distillation' || type === 'nbk') && avgStability < 0.55) {
        items.push({
            tone: 'warn',
            title: 'Средняя стабильность ниже желаемой',
            detail: `Средний stability index около ${(avgStability * 100).toFixed(0)}%.`,
            action: 'Стоит проверить разгон, стабилизацию и первые минуты отбора: там, вероятно, теряется повторяемость.'
        });
    }

    if ((type === 'rectification' || type === 'distillation' || type === 'nbk') && takeoffShare < 0.65) {
        items.push({
            tone: 'warn',
            title: 'Окно разрешённого отбора было узким',
            detail: `Автоматика считала отбор допустимым только ${(takeoffShare * 100).toFixed(0)}% времени.`,
            action: 'Для следующего запуска полезно снять нагрузку с колонны или увеличить выдержку перед телом.'
        });
    }

    if (type === 'rectification' && finalHeadsScore < 0.75) {
        items.push({
            tone: 'warn',
            title: 'Головы закончились неубедительно',
            detail: `Финальный heads score всего ${(finalHeadsScore * 100).toFixed(0)}%.`,
            action: 'Проверьте, не стоит ли увеличить стабилизацию или объём/длительность голов.'
        });
    }

    if (type === 'rectification' && finalBodyScore > 0.9) {
        items.push({
            tone: 'warn',
            title: 'Конец тела пришёл жёстко',
            detail: `Финальный body end score ${(finalBodyScore * 100).toFixed(0)}%.`,
            action: 'Есть смысл заранее смягчать конец тела, чтобы переход не был таким резким.'
        });
    }

    if (items.length === 0) {
        items.push({
            tone: completedSuccessfully ? 'good' : 'muted',
            title: completedSuccessfully ? 'Прогон выглядит повторяемым' : 'Грубых инженерных провалов не видно',
            detail: completedSuccessfully
                ? 'По сохранённым indicators явных провалов по устойчивости, охлаждению и safety не видно.'
                : 'Даже без идеального финала indicators не показывают явную системную проблему этого прогона.',
            action: 'Этот запуск можно использовать как опорный для сравнения следующих версий профиля.'
        });
    }

    return items.slice(0, 4);

}

function appendRunAdvisorSection(container, process, previousSuccessfulProcess = null, previousSummary = null) {

    const items = buildRunAdvisorItems(process, previousSuccessfulProcess, previousSummary);

    if (!items.length) {
        return;
    }

    const section = document.createElement('div');
    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');
    titleEl.className = 'modal-info-label';
    titleEl.textContent = 'Run Advisor';

    const listEl = document.createElement('div');
    listEl.className = 'modal-advisor-list';

    items.forEach((item) => {
        const row = document.createElement('div');
        row.className = `modal-advisor-item is-${item.tone}`;

        const headEl = document.createElement('strong');
        headEl.textContent = item.title;

        const detailEl = document.createElement('p');
        detailEl.className = 'modal-advisor-text';
        detailEl.textContent = item.detail;

        const actionEl = document.createElement('p');
        actionEl.className = 'modal-advisor-action';
        actionEl.textContent = item.action;

        row.appendChild(headEl);
        row.appendChild(detailEl);
        row.appendChild(actionEl);
        listEl.appendChild(row);
    });

    section.appendChild(titleEl);
    section.appendChild(listEl);
    container.appendChild(section);

}



export function showHistoryDetailsModal(process, options = {}) {

    const previousSuccessfulProcess = options.previousSuccessfulProcess || null;
    const previousSummary = options.previousSummary || null;

    const typeNames = {

        rectification: 'Ректификация',

        distillation: 'Дистилляция',

        mashing: 'Затирка',

        hold: 'Пастеризация',

        nbk: 'НБК',

        fermentation: 'Ферментация'

    };



    const startDate = new Date(process.metadata.startTime * 1000);

    const endDate = new Date(process.metadata.endTime * 1000);

    const typeName = typeNames[process.process.type] || process.process.type;



    // Установить заголовок

    document.getElementById('modal-title').textContent = `${typeName} - ${startDate.toLocaleDateString('ru-RU')}`;



    // Заполнить основную информацию

    const infoGrid = document.getElementById('modal-info-grid');

    infoGrid.innerHTML = `

        <div class="modal-info-item">

            <div class="modal-info-label">Тип процесса</div>

            <div class="modal-info-value">${typeName}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Режим</div>

            <div class="modal-info-value">${process.process.mode === 'auto' ? 'Авто' : 'Ручной'}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Профиль</div>

            <div class="modal-info-value">${getProcessProfile(process) || '—'}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Начало</div>

            <div class="modal-info-value">${startDate.toLocaleString('ru-RU')}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Окончание</div>

            <div class="modal-info-value">${endDate.toLocaleString('ru-RU')}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Длительность</div>

            <div class="modal-info-value">${(process.metadata.duration / 3600).toFixed(1)} ч</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Статус</div>

            <div class="modal-info-value">${process.metadata.completedSuccessfully ? '✅ Успешно' : '⚠️ Прервано'}</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Средняя мощность</div>

            <div class="modal-info-value">${process.metrics?.power?.avgPower || 0} Вт</div>

        </div>

        <div class="modal-info-item">

            <div class="modal-info-label">Потреблено энергии</div>

            <div class="modal-info-value">${(process.metrics?.power?.energyUsed || 0).toFixed(2)} кВт·ч</div>

        </div>

    `;



    // Построить график температур

    renderTempChart(process);



    // Построить график мощности

    renderPowerChart(process);



    // Заполнить фазы

    renderPhases(process);



    const resultsGrid = document.getElementById('modal-results-grid');

    resultsGrid.innerHTML = '';

    appendInfoItem(resultsGrid, 'Головы', `${process.results.headsCollected || 0} мл`);
    appendInfoItem(resultsGrid, 'Тело', `${process.results.bodyCollected || 0} мл`);
    appendInfoItem(resultsGrid, 'Хвосты', `${process.results.tailsCollected || 0} мл`);
    appendInfoItem(resultsGrid, 'Всего собрано', `${process.results.totalCollected || 0} мл`);
    appendCompletionInsightSection(resultsGrid, process);
    appendRunAdvisorSection(resultsGrid, process, previousSuccessfulProcess, previousSummary);
    appendIndicatorSummarySection(resultsGrid, process);
    appendSafetyTimelineSection(resultsGrid, process);
    appendEventSection(resultsGrid, 'Ошибки и аварии', process.results?.errors || [], 'error');
    appendEventSection(resultsGrid, 'Предупреждения', process.results?.warnings || [], 'warning');
    appendNotesSection(resultsGrid, process.notes);



    // Привязать обработчики к кнопкам экспорта

    const exportCsvBtn = document.getElementById('modal-export-csv');

    const exportJsonBtn = document.getElementById('modal-export-json');



    if (exportCsvBtn) {

        exportCsvBtn.onclick = () => exportHistoryCSV(process.id);

    }



    if (exportJsonBtn) {

        exportJsonBtn.onclick = () => exportHistoryJSON(process.id);

    }



    // Показать модальное окно

    document.getElementById('history-modal').classList.add('active');

    document.body.style.overflow = 'hidden';

}



export function closeHistoryModal() {

    document.getElementById('history-modal').classList.remove('active');

    document.body.style.overflow = '';



    // Уничтожить графики

    if (tempChart) {

        tempChart.destroy();

        tempChart = null;

    }

    if (powerChart) {

        powerChart.destroy();

        powerChart = null;

    }

}



export function renderTempChart(process) {

    const chartEl = document.getElementById('modal-temp-chart');

    chartEl.innerHTML = '';



    if (!process.timeseries || process.timeseries.data.length === 0) {

        chartEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">Нет данных временного ряда</p>';

        return;

    }



    const data = process.timeseries.data;



    const options = {

        chart: {

            type: 'line',

            height: 350,

            animations: {

                enabled: false

            },

            toolbar: {
                show: true,
                tools: {
                    download: true,
                    selection: true,
                    zoom: true,
                    zoomin: true,
                    zoomout: true,
                    pan: true,
                    reset: true
                },
                autoSelected: 'zoom'
            },
            zoom: {
                enabled: true,
                type: 'x'
            },

            background: 'transparent'

        },

        theme: {

            mode: document.body.getAttribute('data-theme') || 'light'

        },

        series: [

            {

                name: 'Куб',

                data: data.map(p => ({ x: p.time * 1000, y: p.cube }))

            },

            {

                name: 'Царга верх',

                data: data.map(p => ({ x: p.time * 1000, y: p.columnTop }))

            }

        ],

        xaxis: {

            type: 'datetime',

            labels: {

                datetimeFormatter: {

                    hour: 'HH:mm'

                }

            }

        },

        yaxis: {

            title: {

                text: 'Температура (°C)'

            },

            decimalsInFloat: 1

        },

        stroke: {

            curve: 'smooth',

            width: 2

        },

        colors: ['#dc3545', '#007bff'],

        legend: {

            show: true,

            position: 'top'

        },

        tooltip: {

            x: {

                format: 'dd MMM HH:mm'

            }

        }

    };



    tempChart = new ApexCharts(chartEl, options);

    tempChart.render();

}



export function renderPowerChart(process) {

    const chartEl = document.getElementById('modal-power-chart');

    chartEl.innerHTML = '';



    if (!process.timeseries || process.timeseries.data.length === 0) {

        chartEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">Нет данных временного ряда</p>';

        return;

    }



    const data = process.timeseries.data;



    const options = {

        chart: {

            type: 'area',

            height: 300,

            animations: {

                enabled: false

            },

            toolbar: {
                show: true,
                tools: {
                    download: true,
                    selection: true,
                    zoom: true,
                    zoomin: true,
                    zoomout: true,
                    pan: true,
                    reset: true
                },
                autoSelected: 'zoom'
            },
            zoom: {
                enabled: true,
                type: 'x'
            },

            background: 'transparent'

        },

        theme: {

            mode: document.body.getAttribute('data-theme') || 'light'

        },

        series: [

            {

                name: 'Мощность',

                data: data.map(p => ({ x: p.time * 1000, y: p.power }))

            }

        ],

        xaxis: {

            type: 'datetime',

            labels: {

                datetimeFormatter: {

                    hour: 'HH:mm'

                }

            }

        },

        yaxis: {

            title: {

                text: 'Мощность (Вт)'

            },

            decimalsInFloat: 0

        },

        stroke: {

            curve: 'smooth',

            width: 2

        },

        fill: {

            type: 'gradient',

            gradient: {

                shadeIntensity: 1,

                opacityFrom: 0.7,

                opacityTo: 0.3

            }

        },

        colors: ['#28a745'],

        tooltip: {

            x: {

                format: 'dd MMM HH:mm'

            }

        }

    };



    powerChart = new ApexCharts(chartEl, options);

    powerChart.render();

}



export function renderPhases(process) {

    const phasesEl = document.getElementById('modal-phases');



    if (!process.phases || process.phases.length === 0) {

        phasesEl.innerHTML = '<p style="text-align: center; color: var(--text-secondary); padding: 20px;">Нет информации о фазах</p>';

        return;

    }



    const phaseNames = {

        heating: 'Нагрев',

        stabilization: 'Стабилизация',

        heads: 'Отбор голов',

        body: 'Отбор тела',

        tails: 'Отбор хвостов',

        purge: 'Очистка',

        finish: 'Завершение'

    };



    phasesEl.innerHTML = '';



    process.phases.forEach(phase => {

        const phaseEl = document.createElement('div');

        phaseEl.className = 'modal-phase-item';



        const phaseName = phaseNames[phase.name] || phase.name;

        const startDate = new Date(phase.startTime * 1000);

        const endDate = new Date(phase.endTime * 1000);



        const nameEl = document.createElement('div');

        nameEl.className = 'modal-phase-name';

        nameEl.textContent = phaseName;

        const detailsEl = document.createElement('div');

        detailsEl.className = 'modal-phase-details';

        appendPhaseDetail(detailsEl, 'Начало', startDate.toLocaleTimeString('ru-RU'));
        appendPhaseDetail(detailsEl, 'Окончание', endDate.toLocaleTimeString('ru-RU'));
        appendPhaseDetail(detailsEl, 'Длительность', `${(phase.duration / 60).toFixed(0)} мин`);
        appendPhaseDetail(detailsEl, 'Объём', `${phase.volume || 0} мл`);
        appendPhaseDetail(detailsEl, 'Средняя скорость', `${phase.avgSpeed || 0} мл/ч`);

        const reasonCode = formatReasonCode(phase.reasonCode);
        if (reasonCode) {
            appendPhaseDetail(detailsEl, 'Причина', reasonCode);
        }

        const operatorMessage = String(phase.operatorMessage || '').trim();
        if (operatorMessage) {
            appendPhaseDetail(detailsEl, 'Комментарий', operatorMessage);
        }

        phaseEl.appendChild(nameEl);
        phaseEl.appendChild(detailsEl);



        phasesEl.appendChild(phaseEl);

    });

}



// Закрытие модального окна при клике на overlay

document.addEventListener('DOMContentLoaded', function () {

    const modalOverlay = document.getElementById('history-modal');

    if (modalOverlay) {

        modalOverlay.addEventListener('click', function (e) {

            if (e.target === modalOverlay) {

                closeHistoryModal();

            }

        });

    }

});
