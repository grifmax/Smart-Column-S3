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

const ADVISOR_SNAPSHOT_SCHEMA_VERSION = 'run-advisor-v3';

const ADVISOR_PARAMETER_DEFINITIONS = {
    targetPower: {
        label: 'мощность',
        step: 50,
        format: (value) => `${Math.round(Number(value || 0))} Вт`
    },
    stabilizationTime: {
        label: 'стабилизация',
        step: 60,
        format: (value) => formatMinutes(value)
    },
    pumpSpeedHead: {
        label: 'скорость голов',
        step: 5,
        format: (value) => `${Math.round(Number(value || 0))} мл/ч`
    },
    pumpSpeedBody: {
        label: 'скорость тела',
        step: 5,
        format: (value) => `${Math.round(Number(value || 0))} мл/ч`
    },
    headVolume: {
        label: 'объём голов',
        step: 10,
        format: (value) => `${Math.round(Number(value || 0))} мл`
    }
};

function normalizeProfileKey(value) {

    return String(value || '').trim().toLowerCase();

}

export function getProcessProfile(process) {

    return String(process?.process?.profile || process?.profile || '').trim();

}

function getProcessProfileId(process) {

    return String(process?.process?.profileId || process?.profileId || '').trim();

}

function getProcessProfileKey(process) {

    const profileId = getProcessProfileId(process);
    if (profileId) {
        return `id:${profileId}`;
    }

    const profileName = normalizeProfileKey(getProcessProfile(process));
    return profileName ? `name:${profileName}` : '';

}

export function findPreviousSuccessfulProcessSummary(process) {

    const currentId = String(process?.id || '').trim();
    const currentType = String(process?.process?.type || '').trim();
    const currentProfileKey = getProcessProfileKey(process);
    const currentStartTime = Number(process?.metadata?.startTime || 0);

    if (!currentType || !currentProfileKey || currentStartTime <= 0) {

        return null;

    }

    return [...historyData]
        .filter((item) => String(item?.id || '').trim() !== currentId)
        .filter((item) => String(item?.type || '').trim() === currentType)
        .filter((item) => getProcessProfileKey(item) === currentProfileKey)
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
    const processHealthAvg = Number(indicators.processHealth?.avg || 0);
    const processHealthMin = Number(indicators.processHealth?.min || 0);
    const stabilityAvg = Number(indicators.stabilityIndexAvg || 0);
    const coolingMin = Number(indicators.coolingMarginC?.min || 0);
    const floodMax = Number(indicators.floodRisk?.max || 0);
    const takeoffShare = Number(indicators.takeoffAllowedSamples || 0) / samples;
    const freshnessShare = Number(indicators.sensorFreshnessOkSamples || 0) / samples;

    appendInfoItem(container, 'Здоровье процесса', `${(processHealthAvg * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Мин. здоровье', `${(processHealthMin * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Стабильность', `${(stabilityAvg * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Риск захлёба', `${(Number(indicators.floodRisk?.avg || 0) * 100).toFixed(0)}% / max ${(floodMax * 100).toFixed(0)}%`);
    appendInfoItem(container, 'Запас охлаждения', `${Number(indicators.coolingMarginC?.avg || 0).toFixed(1)}°C / min ${coolingMin.toFixed(1)}°C`);
    appendInfoItem(container, 'Отбор разрешён', share(indicators.takeoffAllowedSamples));
    appendInfoItem(container, 'Свежесть датчиков', share(indicators.sensorFreshnessOkSamples));
    appendInfoItem(container, 'Финальные score', `heads ${(Number(indicators.headsCompletionScoreFinal || 0) * 100).toFixed(0)}%, body ${(Number(indicators.bodyEndScoreFinal || 0) * 100).toFixed(0)}%`);

    const verdictSection = document.createElement('div');
    verdictSection.className = 'modal-info-item modal-info-item-wide';

    const verdictLabel = document.createElement('div');
    verdictLabel.className = 'modal-info-label';
    verdictLabel.textContent = 'Indicators verdict';

    const verdictCard = document.createElement('div');
    const verdictTone = freshnessShare < 0.95 || coolingMin <= 0 || floodMax >= 0.8
        ? 'danger'
        : ((stabilityAvg < 0.55 || takeoffShare < 0.65 || coolingMin < 4 || floodMax >= 0.55) ? 'warn' : 'good');
    verdictCard.className = `modal-history-insight is-${verdictTone}`;

    const verdictHead = document.createElement('div');
    verdictHead.className = 'modal-history-insight-head';

    const verdictTitle = document.createElement('strong');
    verdictTitle.textContent = verdictTone === 'good'
        ? 'Indicators выглядели рабочими'
        : (verdictTone === 'warn' ? 'Indicators показывают узкое рабочее окно' : 'Indicators подтверждают проблемный прогон');

    const verdictText = document.createElement('p');
    verdictText.className = 'modal-history-insight-text';
    verdictText.textContent = `Process health ${formatPercent0(processHealthAvg)}, минимум ${formatPercent0(processHealthMin)}, stability ${formatPercent0(stabilityAvg)}, flood max ${formatPercent0(floodMax)}, cooling margin min ${coolingMin.toFixed(1)}°C, takeoff window ${formatPercent0(takeoffShare)}, telemetry freshness ${formatPercent0(freshnessShare)}.`;

    const verdictAction = document.createElement('p');
    verdictAction.className = 'modal-history-insight-action';
    verdictAction.textContent = verdictTone === 'good'
        ? 'Этот запуск подходит как reference для baseline и последующих сравнений профиля.'
        : (verdictTone === 'warn'
            ? 'Сценарий рабочий, но запас устойчивости небольшой. Это хороший кандидат для мягкой оптимизации профиля.'
            : 'Перед повторением сценария сначала устраните телеметрию/охлаждение/захлёб, а уже потом сравнивайте профили.');

    verdictHead.appendChild(verdictTitle);
    verdictCard.appendChild(verdictHead);
    verdictCard.appendChild(verdictText);
    verdictCard.appendChild(verdictAction);
    verdictSection.appendChild(verdictLabel);
    verdictSection.appendChild(verdictCard);
    container.appendChild(verdictSection);

}

function formatDurationCompact(seconds) {

    const totalSeconds = Math.max(0, Math.round(Number(seconds || 0)));
    const hours = Math.floor(totalSeconds / 3600);
    const minutes = Math.round((totalSeconds % 3600) / 60);

    if (hours <= 0) {
        return `${minutes} мин`;
    }

    if (minutes <= 0) {
        return `${hours} ч`;
    }

    return `${hours} ч ${minutes} мин`;

}

function formatPercent0(value) {

    return `${(Number(value || 0) * 100).toFixed(0)}%`;

}

function buildRunReportItems(process, previousSuccessfulProcess = null, previousSummary = null) {

    const items = [];
    const completedSuccessfully = Boolean(process?.metadata?.completedSuccessfully);
    const totalCollected = Number(process?.results?.totalCollected || 0);
    const headsCollected = Number(process?.results?.headsCollected || 0);
    const bodyCollected = Number(process?.results?.bodyCollected || 0);
    const tailsCollected = Number(process?.results?.tailsCollected || 0);
    const energyUsed = Number(process?.metrics?.power?.energyUsed || 0);
    const avgPower = Number(process?.metrics?.power?.avgPower || 0);
    const energyPerLiter = getEnergyPerLiter(process);
    const indicators = process?.metrics?.indicators || null;
    const indicatorShares = getIndicatorShares(process);
    const safetyEvents = buildSafetyTimeline(process);
    const warnings = Array.isArray(process?.results?.warnings) ? process.results.warnings.length : 0;
    const errors = Array.isArray(process?.results?.errors) ? process.results.errors.length : 0;

    const heating = Number(getPhase(process, 'heating')?.duration || 0);
    const stabilization = Number(getPhase(process, ['stabilization', 'post_heads_stabilization'])?.duration || 0);
    const headsPhase = Number(getPhase(process, 'heads')?.duration || 0);
    const bodyPhase = Number(getPhase(process, 'body')?.duration || 0);
    const tailsPhase = Number(getPhase(process, 'tails')?.duration || 0);

    const phaseParts = [
        heating > 0 ? `разгон ${formatDurationCompact(heating)}` : '',
        stabilization > 0 ? `стабилизация ${formatDurationCompact(stabilization)}` : '',
        headsPhase > 0 ? `головы ${formatDurationCompact(headsPhase)}` : '',
        bodyPhase > 0 ? `тело ${formatDurationCompact(bodyPhase)}` : '',
        tailsPhase > 0 ? `хвосты ${formatDurationCompact(tailsPhase)}` : ''
    ].filter(Boolean);

    if (phaseParts.length) {
        items.push({
            tone: completedSuccessfully ? 'good' : 'warn',
            title: 'Фазы и темп прогона',
            detail: `Хронология прогона: ${phaseParts.join(', ')}.`,
            action: previousSuccessfulProcess
                ? 'Сравните длительность разгона, стабилизации и тела с прошлым успешным baseline: там чаще всего видны реальные отклонения профиля.'
                : 'Этот разбор уже полезен как первый baseline по времени фаз для следующих запусков.'
        });
    }

    if (totalCollected > 0) {
        const headsShare = headsCollected / totalCollected;
        const bodyShare = bodyCollected / totalCollected;
        const tailsShare = tailsCollected / totalCollected;
        items.push({
            tone: bodyShare >= 0.7 ? 'good' : 'muted',
            title: 'Выход и фракции',
            detail: `Собрано ${Math.round(totalCollected)} мл: головы ${Math.round(headsCollected)} мл (${formatPercent0(headsShare)}), тело ${Math.round(bodyCollected)} мл (${formatPercent0(bodyShare)}), хвосты ${Math.round(tailsCollected)} мл (${formatPercent0(tailsShare)}).`,
            action: bodyShare >= 0.7
                ? 'Выход тела выглядит уверенно. Дальше имеет смысл смотреть не на объём сам по себе, а на устойчивость и энергию на литр.'
                : 'Если тело получилось коротким, проверьте конец рабочего окна, момент перехода в хвосты и запас охлаждения.'
        });
    }

    if (energyUsed > 0 || avgPower > 0 || energyPerLiter !== null) {
        const previousEnergyPerLiter = previousSuccessfulProcess ? getEnergyPerLiter(previousSuccessfulProcess) : null;
        const energyDelta = (energyPerLiter !== null && previousEnergyPerLiter !== null && previousEnergyPerLiter > 0)
            ? (energyPerLiter - previousEnergyPerLiter) / previousEnergyPerLiter
            : null;

        items.push({
            tone: energyDelta !== null ? (energyDelta > 0.1 ? 'warn' : 'good') : 'muted',
            title: 'Энергия и эффективность',
            detail: `Потреблено ${energyUsed.toFixed(2)} кВт·ч при средней мощности ${Math.round(avgPower)} Вт.${energyPerLiter !== null ? ` Удельная энергия ${energyPerLiter.toFixed(2)} кВт·ч/л.` : ''}`,
            action: energyDelta !== null
                ? (energyDelta > 0.1
                    ? `Относительно прошлого успешного прогона энергоёмкость выросла на ${formatSignedPercent(energyDelta)}. Ищите потери во времени фаз и узком рабочем окне.`
                    : `Относительно прошлого успешного прогона энергоёмкость не ухудшилась критично (${formatSignedPercent(energyDelta)}).`)
                : 'Если хотите улучшать профиль системно, это одна из ключевых метрик для сравнения между прогонами.'
        });
    }

    if (indicators && indicatorShares) {
        const processHealthAvg = Number(indicators.processHealth?.avg || 0);
        const processHealthMin = Number(indicators.processHealth?.min || 0);
        const stabilityAvg = Number(indicators.stabilityIndexAvg || 0);
        const floodMax = Number(indicators.floodRisk?.max || 0);
        const coolingMin = Number(indicators.coolingMarginC?.min || 0);
        const takeoffShare = Number(indicatorShares.takeoffShare || 0);
        const tone = coolingMin <= 0 || floodMax >= 0.8 ? 'danger'
            : (stabilityAvg < 0.55 || takeoffShare < 0.65 || coolingMin < 4 ? 'warn' : 'good');

        items.push({
            tone,
            title: 'Устойчивость процесса',
            detail: `Process health ${formatPercent0(processHealthAvg)}, минимум ${formatPercent0(processHealthMin)}, stability ${formatPercent0(stabilityAvg)}, flood risk max ${formatPercent0(floodMax)}, cooling margin min ${coolingMin.toFixed(1)}°C, окно отбора ${formatPercent0(takeoffShare)}.`,
            action: tone === 'good'
                ? 'Рабочее окно выглядело достаточно спокойным. Такой прогон можно использовать как инженерный baseline.'
                : 'Именно здесь находится главный материал для настройки профиля: охлаждение, flood risk и ширина окна отбора.'
        });
    }

    items.push({
        tone: errors > 0 ? 'danger' : (safetyEvents.length > 0 || warnings > 0 ? 'warn' : 'good'),
        title: 'Safety и ограничения',
        detail: `Safety-событий: ${safetyEvents.length}. Предупреждений: ${warnings}. Ошибок: ${errors}.`,
        action: safetyEvents.length > 0
            ? 'Перед следующим запуском сначала разберите safety timeline и только потом меняйте сам профиль.'
            : 'Если safety не вмешивался, оценка профиля и повторяемости будет намного чище.'
    });

    if (previousSuccessfulProcess && previousSummary) {
        const delta = evaluateRunDelta(process, previousSuccessfulProcess);
        const previousDate = new Date(Number(previousSuccessfulProcess?.metadata?.startTime || previousSummary?.startTime || 0) * 1000);

        if (delta) {
            const deviationTone = delta.weightedScore >= 0.9 ? 'good'
                : (delta.weightedScore <= -0.9 ? 'warn' : 'muted');
            items.push({
                tone: deviationTone,
                title: 'Отклонение от эталона профиля',
                detail: `Сравнение с успешным прогоном от ${previousDate.toLocaleString('ru-RU')}: stability ${formatSignedPercent(delta.stabilityDelta)}, окно отбора ${formatSignedPercent(delta.takeoffDelta)}, cooling margin ${formatSignedNumber(delta.coolingDelta, 1, '°C')}, энергоёмкость ${delta.previousEnergyPerLiter !== null && delta.currentEnergyPerLiter !== null && delta.previousEnergyPerLiter > 0 ? formatSignedPercent((delta.currentEnergyPerLiter - delta.previousEnergyPerLiter) / delta.previousEnergyPerLiter) : 'н/д'}.`,
                action: deviationTone === 'good'
                    ? 'Текущий прогон можно рассматривать как более сильный baseline для профиля.'
                    : (deviationTone === 'warn'
                        ? 'Профиль или условия прогона просели относительно baseline. Меняйте только один параметр за следующий запуск.'
                        : 'Сдвиг относительно baseline есть, но пока без уверенного инженерного вердикта.')
            });
        }
    } else {
        items.push({
            tone: 'muted',
            title: 'Эталон профиля ещё не сформирован',
            detail: 'Для этого прогона нет предыдущего успешного baseline того же профиля, поэтому отчёт пока оценивает только текущий запуск.',
            action: 'Первый успешный прогон этого профиля станет опорной точкой для следующего честного сравнения.'
        });
    }

    const recommendationSeed = buildRunAdvisorItems(process, previousSuccessfulProcess, previousSummary)[0];
    if (recommendationSeed) {
        items.push({
            tone: recommendationSeed.tone,
            title: 'Что делать перед следующим запуском',
            detail: recommendationSeed.detail,
            action: recommendationSeed.action
        });
    }

    return items.slice(0, 7);

}

function getPhase(process, phaseNames) {

    const names = Array.isArray(phaseNames) ? phaseNames : [phaseNames];
    const normalized = names.map((item) => String(item || '').trim()).filter(Boolean);
    const phases = Array.isArray(process?.phases) ? process.phases : [];

    return phases.find((phase) => normalized.includes(String(phase?.name || '').trim())) || null;

}

export function getEnergyPerLiter(process) {

    const energyUsed = Number(process?.metrics?.power?.energyUsed || 0);
    const totalCollectedMl = Number(process?.results?.totalCollected || 0);

    if (energyUsed <= 0 || totalCollectedMl <= 0) {

        return null;

    }

    return energyUsed / (totalCollectedMl / 1000);

}

export function getIndicatorShares(process) {

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

function roundToStep(value, step) {

    const numeric = Number(value || 0);
    const safeStep = Math.max(1, Number(step || 1));

    return Math.round(numeric / safeStep) * safeStep;

}

function getNumericParam(process, key) {

    const value = Number(process?.parameters?.[key] || 0);
    return value > 0 ? value : 0;

}

function formatMinutes(seconds) {

    return `${Math.round(Number(seconds || 0) / 60)} мин`;

}

function getAdvisorParameterDefinition(key) {

    return ADVISOR_PARAMETER_DEFINITIONS[String(key || '').trim()] || null;

}

function formatAdvisorParameterValue(key, value) {

    const definition = getAdvisorParameterDefinition(key);

    if (!definition) {
        return `${Number(value || 0)}`;
    }

    return definition.format(Number(value || 0));

}

function slugifyAdvisorCode(value) {

    return String(value || '')
        .trim()
        .toLowerCase()
        .replace(/[^a-z0-9а-яё]+/gi, '_')
        .replace(/^_+|_+$/g, '') || 'advisor_item';

}

function normalizeAdvisorItem(item) {

    return {
        tone: String(item?.tone || 'muted').trim() || 'muted',
        kind: String(item?.kind || 'observation').trim() || 'observation',
        code: String(item?.code || slugifyAdvisorCode(item?.title)).trim() || 'advisor_item',
        title: String(item?.title || '').trim(),
        detail: String(item?.detail || '').trim(),
        action: String(item?.action || '').trim(),
        parameterKey: String(item?.parameterKey || '').trim(),
        previousValue: Number(item?.previousValue || 0),
        suggestedValue: Number(item?.suggestedValue || 0)
    };

}

function buildAdvisorSnapshot(process, previousSuccessfulProcess, previousSummary, items = null) {

    const normalizedItems = (Array.isArray(items) ? items : buildRunAdvisorItems(process, previousSuccessfulProcess, previousSummary))
        .map((item) => normalizeAdvisorItem(item))
        .filter((item) => item.title);

    if (!normalizedItems.length) {
        return null;
    }

    return {
        schemaVersion: ADVISOR_SNAPSHOT_SCHEMA_VERSION,
        createdAt: Number(process?.metadata?.endTime || process?.metadata?.startTime || 0),
        baselineProcessId: String(previousSuccessfulProcess?.id || previousSummary?.id || '').trim(),
        baselineProfile: getProcessProfile(process),
        items: normalizedItems
    };

}

function normalizeAdvisorSnapshotForCompare(snapshot) {

    if (!snapshot || typeof snapshot !== 'object') {
        return null;
    }

    const items = Array.isArray(snapshot.items)
        ? snapshot.items.map((item) => normalizeAdvisorItem(item)).filter((item) => item.title)
        : [];

    return {
        schemaVersion: String(snapshot.schemaVersion || '').trim(),
        createdAt: Number(snapshot.createdAt || 0),
        baselineProcessId: String(snapshot.baselineProcessId || '').trim(),
        baselineProfile: String(snapshot.baselineProfile || '').trim(),
        items
    };

}

function areAdvisorSnapshotsEqual(left, right) {

    const normalizedLeft = normalizeAdvisorSnapshotForCompare(left);
    const normalizedRight = normalizeAdvisorSnapshotForCompare(right);

    return JSON.stringify(normalizedLeft) === JSON.stringify(normalizedRight);

}

async function persistAdvisorSnapshotIfNeeded(process, previousSuccessfulProcess, previousSummary, items = null) {

    const processId = String(process?.id || '').trim();

    if (!processId) {
        return;
    }

    const snapshot = buildAdvisorSnapshot(process, previousSuccessfulProcess, previousSummary, items);

    if (!snapshot) {
        return;
    }

    if (areAdvisorSnapshotsEqual(snapshot, process?.advisorSnapshot)) {
        return;
    }

    const response = await fetch(`/api/history/${processId}/advisor`, {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(snapshot)
    });

    if (!response.ok) {
        throw new Error(`Не удалось сохранить advisor snapshot (${response.status})`);
    }

    process.advisorSnapshot = snapshot;

}

function evaluateAdvisorRecommendationApplication(currentValue, baselineValue, suggestedValue, parameterKey) {

    const definition = getAdvisorParameterDefinition(parameterKey);
    const numericCurrent = Number(currentValue || 0);
    const numericBaseline = Number(baselineValue || 0);
    const numericSuggested = Number(suggestedValue || 0);

    if (!definition || numericCurrent <= 0 || numericBaseline <= 0 || numericSuggested <= 0) {
        return null;
    }

    const direction = numericSuggested > numericBaseline ? 'increase' : (numericSuggested < numericBaseline ? 'decrease' : 'keep');
    if (direction === 'keep') {
        return null;
    }

    const step = Math.max(1, Number(definition.step || 1));
    const fullTolerance = Math.max(step, Math.abs(numericSuggested - numericBaseline) * 0.35);
    const partialTolerance = Math.max(step / 2, Math.abs(numericSuggested - numericBaseline) * 0.15);

    const applied = direction === 'increase'
        ? numericCurrent >= (numericSuggested - fullTolerance)
        : numericCurrent <= (numericSuggested + fullTolerance);

    const partial = !applied && (direction === 'increase'
        ? numericCurrent > (numericBaseline + partialTolerance)
        : numericCurrent < (numericBaseline - partialTolerance));

    return {
        direction,
        applied,
        partial,
        step,
        currentValue: numericCurrent,
        baselineValue: numericBaseline,
        suggestedValue: numericSuggested
    };

}

function buildPreviousAdvisorFollowUpItems(process, previousSuccessfulProcess) {

    const snapshot = previousSuccessfulProcess?.advisorSnapshot;
    const profileName = getProcessProfile(process);
    const delta = evaluateRunDelta(process, previousSuccessfulProcess);
    const previousDate = new Date(Number(previousSuccessfulProcess?.metadata?.startTime || 0) * 1000);

    if (!snapshot || !Array.isArray(snapshot.items) || !delta) {
        return [];
    }

    const adjustmentItems = snapshot.items
        .map((item) => normalizeAdvisorItem(item))
        .filter((item) => item.kind === 'adjustment' && item.parameterKey && item.suggestedValue > 0);

    if (!adjustmentItems.length) {
        return [];
    }

    let appliedCount = 0;
    let partialCount = 0;
    let untouchedCount = 0;

    const followUpItems = adjustmentItems.map((item) => {
        const definition = getAdvisorParameterDefinition(item.parameterKey);
        const baselineValue = getNumericParam(previousSuccessfulProcess, item.parameterKey) || Number(item.previousValue || 0);
        const currentValue = getNumericParam(process, item.parameterKey);
        const application = evaluateAdvisorRecommendationApplication(
            currentValue,
            baselineValue,
            item.suggestedValue,
            item.parameterKey
        );

        if (!definition || !application) {
            return null;
        }

        if (application.applied) {
            appliedCount += 1;
        } else if (application.partial) {
            partialCount += 1;
        } else {
            untouchedCount += 1;
        }

        let tone = 'muted';
        let title = `Проверка совета: ${definition.label}`;
        let action = '';

        if (application.applied && delta.weightedScore >= 0.9) {
            tone = 'good';
            title = `Совет по "${definition.label}" сработал`;
            action = 'Эту правку можно считать рабочей, но всё равно лучше подтвердить её повторным запуском без новых изменений поверх.';
        } else if (application.applied && delta.weightedScore <= -0.9) {
            tone = 'warn';
            title = `Совет по "${definition.label}" применили, но эффект слабый`;
            action = 'Не закрепляйте это как новый baseline автоматически: либо откатите правку, либо меняйте уже другой параметр отдельно.';
        } else if (application.applied) {
            tone = 'muted';
            title = `Совет по "${definition.label}" применили частично успешно`;
            action = 'Пока эффект смешанный. Полезно повторить запуск с теми же уставками и не наслаивать новые изменения.';
        } else if (application.partial) {
            tone = 'muted';
            title = `Совет по "${definition.label}" перенесли не полностью`;
            action = 'Сейчас сложно честно судить о результате: уставка двинулась в нужную сторону, но не дошла до рекомендованного уровня.';
        } else {
            tone = 'muted';
            title = `Совет по "${definition.label}" не применяли`;
            action = 'Если хотите проверить старую гипотезу, перенесите именно эту правку в следующий запуск отдельно от остальных изменений.';
        }

        return {
            tone,
            kind: 'follow_up',
            code: `follow_up_${item.code}`,
            title,
            detail: `После успешного прогона от ${previousDate.toLocaleString('ru-RU')} Run Advisor советовал ${formatAdvisorParameterValue(item.parameterKey, baselineValue)} -> ${formatAdvisorParameterValue(item.parameterKey, item.suggestedValue)}. Текущий запуск: ${formatAdvisorParameterValue(item.parameterKey, currentValue)}.`,
            action
        };
    }).filter(Boolean);

    if (!followUpItems.length) {
        return [];
    }

    let summaryTone = 'muted';
    let summaryTitle = `Память рекомендаций для профиля ${profileName || 'без профиля'}`;
    let summaryAction = 'Это мостик между запусками: он показывает, какие советы реально были перенесены в следующий прогон.';

    if (appliedCount > 0 && delta.weightedScore >= 0.9) {
        summaryTone = 'good';
        summaryTitle = 'Прошлые рекомендации, вероятно, помогли';
        summaryAction = 'У вас уже появляется инженерная цепочка "совет -> изменение -> улучшение". Это хороший кандидат на новый рабочий baseline.';
    } else if (appliedCount > 0 && delta.weightedScore <= -0.9) {
        summaryTone = 'warn';
        summaryTitle = 'Прошлые рекомендации применили, но улучшения нет';
        summaryAction = 'Лучше не усиливать эти правки дальше. Следующий запуск делайте либо с откатом, либо с изоляцией одного изменения.';
    } else if (partialCount > 0 && appliedCount === 0) {
        summaryTone = 'muted';
        summaryTitle = 'Прошлые рекомендации перенесены только частично';
        summaryAction = 'Промежуточные правки труднее оценивать. Для честного сравнения полезно доводить одну гипотезу до конца.';
    } else if (untouchedCount > 0 && appliedCount === 0 && partialCount === 0) {
        summaryTone = 'muted';
        summaryTitle = 'Прошлые рекомендации пока не использовались';
        summaryAction = 'Snapshot сохранён именно для этого: чтобы на следующем прогоне можно было осознанно проверить старую рекомендацию, а не потерять её в истории.';
    }

    return [{
        tone: summaryTone,
        kind: 'follow_up',
        code: 'follow_up_summary',
        title: summaryTitle,
        detail: `Из прошлого успешного прогона найдено ${adjustmentItems.length} инженерных рекомендаций: применено ${appliedCount}, частично перенесено ${partialCount}, не тронуто ${untouchedCount}. Относительно предыдущего успешного запуска итоговый score сейчас ${formatSignedNumber(delta.weightedScore, 2)}.`,
        action: summaryAction
    }, ...followUpItems.slice(0, 3)];

}

function buildParameterChangeList(process, previousProcess) {

    const definitions = [
        { key: 'targetPower', label: 'мощность', suffix: ' Вт' },
        { key: 'stabilizationTime', label: 'стабилизация', suffix: ' сек', formatter: (value) => formatMinutes(value) },
        { key: 'pumpSpeedHead', label: 'головы', suffix: ' мл/ч' },
        { key: 'pumpSpeedBody', label: 'тело', suffix: ' мл/ч' },
        { key: 'headVolume', label: 'объём голов', suffix: ' мл' }
    ];

    return definitions
        .map((definition) => {
            const currentValue = getNumericParam(process, definition.key);
            const previousValue = getNumericParam(previousProcess, definition.key);

            if (currentValue <= 0 || previousValue <= 0 || currentValue === previousValue) {
                return null;
            }

            const formatValue = definition.formatter || ((value) => `${value}${definition.suffix}`);
            return `${definition.label}: ${formatValue(previousValue)} -> ${formatValue(currentValue)}`;
        })
        .filter(Boolean);

}

export function evaluateRunDelta(process, previousProcess) {

    const currentIndicators = getIndicatorShares(process);
    const previousIndicators = getIndicatorShares(previousProcess);
    const currentEnergyPerLiter = getEnergyPerLiter(process);
    const previousEnergyPerLiter = getEnergyPerLiter(previousProcess);
    const currentDuration = Number(process?.metadata?.duration || 0);
    const previousDuration = Number(previousProcess?.metadata?.duration || 0);

    if (!currentIndicators || !previousIndicators) {

        return null;

    }

    const stabilityDelta = currentIndicators.avgStability - previousIndicators.avgStability;
    const takeoffDelta = currentIndicators.takeoffShare - previousIndicators.takeoffShare;
    const floodDelta = previousIndicators.maxFloodRisk - currentIndicators.maxFloodRisk;
    const coolingDelta = currentIndicators.minCoolingMargin - previousIndicators.minCoolingMargin;
    const energyDelta = (currentEnergyPerLiter !== null && previousEnergyPerLiter !== null && previousEnergyPerLiter > 0)
        ? (previousEnergyPerLiter - currentEnergyPerLiter) / previousEnergyPerLiter
        : 0;
    const durationDelta = previousDuration > 0 ? (previousDuration - currentDuration) / previousDuration : 0;

    const weightedScore =
        (stabilityDelta * 4.0) +
        (takeoffDelta * 3.0) +
        (floodDelta * 2.5) +
        (coolingDelta * 0.35) +
        (energyDelta * 1.5) +
        (durationDelta * 1.0);

    return {
        currentIndicators,
        previousIndicators,
        currentEnergyPerLiter,
        previousEnergyPerLiter,
        currentDuration,
        previousDuration,
        stabilityDelta,
        takeoffDelta,
        floodDelta,
        coolingDelta,
        energyDelta,
        durationDelta,
        weightedScore
    };

}

function buildImprovementVerdictItem(process, previousProcess, previousSummary) {

    const profileName = getProcessProfile(process);

    if (!profileName || !previousProcess || !previousSummary) {

        return null;

    }

    const parameterChanges = buildParameterChangeList(process, previousProcess);
    const delta = evaluateRunDelta(process, previousProcess);

    if (!delta) {

        return null;

    }

    const hasMeaningfulProfileChanges = parameterChanges.length > 0;
    const changeSummary = hasMeaningfulProfileChanges
        ? `Последние изменения профиля: ${parameterChanges.slice(0, 3).join('; ')}.`
        : 'Между эталоном и текущим прогоном явных изменений ключевых уставок профиля не найдено.';

    if (delta.weightedScore >= 0.9) {
        return {
            tone: 'good',
            kind: 'verdict',
            code: hasMeaningfulProfileChanges ? 'profile_changes_helped' : 'current_run_better_than_baseline',
            title: hasMeaningfulProfileChanges
                ? 'Вердикт: последние правки профиля, вероятно, помогли'
                : 'Вердикт: текущий прогон сильнее эталона',
            detail: `${changeSummary} Стабильность ${(delta.currentIndicators.avgStability * 100).toFixed(0)}% против ${(delta.previousIndicators.avgStability * 100).toFixed(0)}%, окно отбора ${(delta.currentIndicators.takeoffShare * 100).toFixed(0)}% против ${(delta.previousIndicators.takeoffShare * 100).toFixed(0)}%, flood risk ${(delta.currentIndicators.maxFloodRisk * 100).toFixed(0)}% против ${(delta.previousIndicators.maxFloodRisk * 100).toFixed(0)}%.`,
            action: hasMeaningfulProfileChanges
                ? 'Эти правки можно считать удачным направлением. Закрепите их как новый рабочий baseline и проверяйте повторяемость на следующем запуске.'
                : 'Текущий запуск можно рассматривать как более сильный baseline для этого профиля.'
        };
    }

    if (delta.weightedScore <= -0.9) {
        return {
            tone: 'warn',
            kind: 'verdict',
            code: hasMeaningfulProfileChanges ? 'profile_changes_failed' : 'current_run_worse_than_baseline',
            title: hasMeaningfulProfileChanges
                ? 'Вердикт: последние правки профиля не помогли'
                : 'Вердикт: текущий прогон хуже эталона',
            detail: `${changeSummary} Рабочее окно просело: стабильность ${(delta.currentIndicators.avgStability * 100).toFixed(0)}% против ${(delta.previousIndicators.avgStability * 100).toFixed(0)}%, запас охлаждения ${delta.currentIndicators.minCoolingMargin.toFixed(1)}°C против ${delta.previousIndicators.minCoolingMargin.toFixed(1)}°C, flood risk ${(delta.currentIndicators.maxFloodRisk * 100).toFixed(0)}% против ${(delta.previousIndicators.maxFloodRisk * 100).toFixed(0)}%.`,
            action: hasMeaningfulProfileChanges
                ? 'Откатите последнюю правку или меняйте только один параметр за следующий запуск, чтобы понять реальную причину ухудшения.'
                : 'Без смены уставок профиль деградировал относительно эталона: проверьте сырьё, охлаждение, давление и состояние оборудования.'
        };
    }

    return {
        tone: 'muted',
        kind: 'verdict',
        code: hasMeaningfulProfileChanges ? 'profile_changes_mixed' : 'current_run_neutral_vs_baseline',
        title: hasMeaningfulProfileChanges
            ? 'Вердикт: правки дали смешанный результат'
            : 'Вердикт: существенного сдвига относительно эталона нет',
        detail: `${changeSummary} Часть метрик улучшилась, часть осталась на месте или просела, поэтому уверенного инженерного вывода пока нет.`,
        action: hasMeaningfulProfileChanges
            ? 'Не добавляйте новые правки поверх этих. Лучше повторить запуск с теми же настройками и проверить, воспроизводится ли эффект.'
            : 'Этот запуск полезен как промежуточная точка, но не как уверенно новый baseline.'
    };

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
        kind: 'comparison',
        code: 'baseline_profile_reference',
        title: `Эталон профиля: ${profileName}`,
        detail: `Сравнение с успешным прогоном от ${previousDate.toLocaleString('ru-RU')} для того же профиля.`,
        action: 'Ниже показаны главные отклонения текущего запуска от последнего успешного эталона.'
    });

    if (currentHeating > 0 && previousHeating > 0) {
        const heatingDelta = (currentHeating - previousHeating) / previousHeating;
        if (Math.abs(heatingDelta) >= 0.12) {
            items.push({
                tone: heatingDelta > 0 ? 'warn' : 'good',
                kind: 'comparison',
                code: heatingDelta > 0 ? 'heating_slower_than_baseline' : 'heating_faster_than_baseline',
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
                kind: 'comparison',
                code: stabilizationDelta > 0 ? 'stabilization_slower_than_baseline' : 'stabilization_faster_than_baseline',
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
                kind: 'comparison',
                code: 'working_window_worse_than_baseline',
                title: 'Рабочее окно хуже эталона',
                detail: `Стабильность ${(currentIndicators.avgStability * 100).toFixed(0)}% vs ${(previousIndicators.avgStability * 100).toFixed(0)}%, отбор разрешён ${(currentIndicators.takeoffShare * 100).toFixed(0)}% vs ${(previousIndicators.takeoffShare * 100).toFixed(0)}%, flood risk ${(currentIndicators.maxFloodRisk * 100).toFixed(0)}% vs ${(previousIndicators.maxFloodRisk * 100).toFixed(0)}%.`,
                action: 'Для следующего запуска начните мягче: проверьте охлаждение, давление и первые минуты отбора.'
            });
        } else if (stabilityDelta >= 0.08 || takeoffDelta >= 0.10 || (floodDelta <= -0.12 && coolingDelta >= 1.0)) {
            items.push({
                tone: 'good',
                kind: 'comparison',
                code: 'working_window_better_than_baseline',
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
                kind: 'comparison',
                code: energyDelta > 0 || durationDelta > 0 ? 'economy_worse_than_baseline' : 'economy_better_than_baseline',
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

export function buildBaselineComparisonSummary(process, previousProcess, previousSummary = null) {

    const comparisonItems = buildProfileComparisonItems(process, previousProcess, previousSummary);
    const verdictItem = buildImprovementVerdictItem(process, previousProcess, previousSummary);

    if (!verdictItem) {
        return comparisonItems.slice(0, 5);
    }

    const [referenceItem, ...restItems] = comparisonItems;

    return [
        referenceItem || null,
        verdictItem,
        ...restItems
    ].filter(Boolean).slice(0, 5);

}

function buildProfileAdjustmentItems(process, previousProcess) {

    const type = String(process?.process?.type || '').trim();
    const indicators = getIndicatorShares(process);
    const previousIndicators = previousProcess ? getIndicatorShares(previousProcess) : null;
    const items = [];

    if (!previousProcess || !indicators) {

        return items;

    }

    const currentHeating = Number(getPhase(process, 'heating')?.duration || 0);
    const previousHeating = Number(getPhase(previousProcess, 'heating')?.duration || 0);
    const currentStabilization = Number(getPhase(process, ['stabilization', 'post_heads_stabilization'])?.duration || 0);
    const previousStabilization = Number(getPhase(previousProcess, ['stabilization', 'post_heads_stabilization'])?.duration || 0);

    const currentTargetPower = getNumericParam(process, 'targetPower');
    const previousTargetPower = getNumericParam(previousProcess, 'targetPower');
    const currentStabilizationParam = getNumericParam(process, 'stabilizationTime');
    const previousStabilizationParam = getNumericParam(previousProcess, 'stabilizationTime');
    const currentPumpSpeedHead = getNumericParam(process, 'pumpSpeedHead');
    const previousPumpSpeedHead = getNumericParam(previousProcess, 'pumpSpeedHead');
    const currentPumpSpeedBody = getNumericParam(process, 'pumpSpeedBody');
    const previousPumpSpeedBody = getNumericParam(previousProcess, 'pumpSpeedBody');
    const currentHeadVolume = getNumericParam(process, 'headVolume');
    const previousHeadVolume = getNumericParam(previousProcess, 'headVolume');

    const finalHeadsScore = Number(process?.metrics?.indicators?.headsCompletionScoreFinal || 0);
    const finalBodyScore = Number(process?.metrics?.indicators?.bodyEndScoreFinal || 0);

    const basePower = currentTargetPower || previousTargetPower;
    const baseStabilization = currentStabilizationParam || previousStabilizationParam;
    const baseHeadSpeed = currentPumpSpeedHead || previousPumpSpeedHead;
    const baseBodySpeed = currentPumpSpeedBody || previousPumpSpeedBody;
    const baseHeadVolume = currentHeadVolume || previousHeadVolume;

    if (basePower > 0 && (indicators.maxFloodRisk >= 0.8 || indicators.minCoolingMargin <= 2 || indicators.takeoffShare < 0.6)) {
        const suggestedPower = Math.max(300, roundToStep(basePower * 0.94, 50));
        if (suggestedPower < basePower) {
            items.push({
                tone: 'warn',
                kind: 'adjustment',
                code: 'reduce_target_power',
                parameterKey: 'targetPower',
                previousValue: basePower,
                suggestedValue: suggestedPower,
                title: 'Профиль: слегка снизить мощность разгона',
                detail: `Текущая нагрузка ${basePower} Вт выглядит жёсткой для этого профиля. Безопасная пробная коррекция: ${basePower} -> ${suggestedPower} Вт.`,
                action: 'Сначала уменьшите мощность на 4-6% и посмотрите, улучшатся ли flood risk, cooling margin и окно разрешённого отбора.'
            });
        }
    } else if (basePower > 0 && previousHeating > 0 && currentHeating > previousHeating * 1.18 && indicators.maxFloodRisk < 0.45 && indicators.minCoolingMargin > 5) {
        const suggestedPower = roundToStep(basePower * 1.04, 50);
        if (suggestedPower > basePower) {
            items.push({
                tone: 'good',
                kind: 'adjustment',
                code: 'increase_target_power_carefully',
                parameterKey: 'targetPower',
                previousValue: basePower,
                suggestedValue: suggestedPower,
                title: 'Профиль: можно аккуратно ускорить разгон',
                detail: `Разгон заметно дольше эталона, но по cooling margin и flood risk запас хороший. Пробный шаг: ${basePower} -> ${suggestedPower} Вт.`,
                action: 'Поднимайте мощность маленьким шагом 3-5% и контролируйте первые минуты после выхода на рабочий режим.'
            });
        }
    }

    if (baseStabilization > 0 && (finalHeadsScore < 0.75 || (previousIndicators && indicators.takeoffShare < previousIndicators.takeoffShare - 0.08) || (previousStabilization > 0 && currentStabilization > previousStabilization * 1.15))) {
        const suggestedStabilization = Math.max(300, roundToStep(baseStabilization * 1.15, 60));
        if (suggestedStabilization > baseStabilization) {
            items.push({
                tone: 'warn',
                kind: 'adjustment',
                code: 'increase_stabilization_time',
                parameterKey: 'stabilizationTime',
                previousValue: baseStabilization,
                suggestedValue: suggestedStabilization,
                title: 'Профиль: увеличить стабилизацию',
                detail: `Текущая уставка стабилизации ${Math.round(baseStabilization / 60)} мин. Безопасный шаг: поднять до ${Math.round(suggestedStabilization / 60)} мин.`,
                action: 'Это консервативная правка: она помогает головам и старту тела без риска пережать профиль слишком сильно.'
            });
        }
    }

    if (type === 'rectification' && baseHeadSpeed > 0 && (finalHeadsScore < 0.72 || (previousIndicators && indicators.avgStability < previousIndicators.avgStability - 0.08))) {
        const suggestedHeadSpeed = Math.max(50, roundToStep(baseHeadSpeed * 0.92, 5));
        if (suggestedHeadSpeed < baseHeadSpeed) {
            items.push({
                tone: 'warn',
                kind: 'adjustment',
                code: 'reduce_head_takeoff_speed',
                parameterKey: 'pumpSpeedHead',
                previousValue: baseHeadSpeed,
                suggestedValue: suggestedHeadSpeed,
                title: 'Профиль: замедлить отбор голов',
                detail: `Скорость голов ${baseHeadSpeed} мл/ч выглядит слишком агрессивной. Пробный шаг: ${baseHeadSpeed} -> ${suggestedHeadSpeed} мл/ч.`,
                action: 'Замедление голов обычно безопаснее, чем попытка компенсировать проблему только объёмом.'
            });
        }
    }

    if (type === 'rectification' && baseHeadVolume > 0 && finalHeadsScore < 0.68) {
        const suggestedHeadVolume = Math.max(baseHeadVolume + 10, roundToStep(baseHeadVolume * 1.08, 10));
        if (suggestedHeadVolume > baseHeadVolume) {
            items.push({
                tone: 'warn',
                kind: 'adjustment',
                code: 'increase_head_volume',
                parameterKey: 'headVolume',
                previousValue: baseHeadVolume,
                suggestedValue: suggestedHeadVolume,
                title: 'Профиль: немного увеличить объём голов',
                detail: `Текущая уставка ${baseHeadVolume} мл. Консервативная поправка: ${baseHeadVolume} -> ${suggestedHeadVolume} мл.`,
                action: 'Увеличивайте объём небольшим шагом и лучше вместе с дополнительной стабилизацией, а не отдельно.'
            });
        }
    }

    if (baseBodySpeed > 0 && (finalBodyScore > 0.9 || indicators.maxFloodRisk >= 0.7 || indicators.takeoffShare < 0.65)) {
        const suggestedBodySpeed = Math.max(50, roundToStep(baseBodySpeed * 0.93, 5));
        if (suggestedBodySpeed < baseBodySpeed) {
            items.push({
                tone: 'warn',
                kind: 'adjustment',
                code: 'reduce_body_takeoff_speed',
                parameterKey: 'pumpSpeedBody',
                previousValue: baseBodySpeed,
                suggestedValue: suggestedBodySpeed,
                title: 'Профиль: смягчить скорость отбора тела',
                detail: `Скорость тела ${baseBodySpeed} мл/ч можно чуть разгрузить. Пробный шаг: ${baseBodySpeed} -> ${suggestedBodySpeed} мл/ч.`,
                action: 'Начните с небольшого снижения 5-7%: это самый безопасный способ расширить рабочее окно без полной перенастройки режима.'
            });
        }
    } else if (baseBodySpeed > 0 && previousIndicators && indicators.avgStability > previousIndicators.avgStability + 0.08 && indicators.minCoolingMargin > previousIndicators.minCoolingMargin + 1.0 && indicators.maxFloodRisk < 0.45) {
        const suggestedBodySpeed = roundToStep(baseBodySpeed * 1.04, 5);
        if (suggestedBodySpeed > baseBodySpeed) {
            items.push({
                tone: 'good',
                kind: 'adjustment',
                code: 'increase_body_takeoff_speed_carefully',
                parameterKey: 'pumpSpeedBody',
                previousValue: baseBodySpeed,
                suggestedValue: suggestedBodySpeed,
                title: 'Профиль: можно аккуратно ускорить тело',
                detail: `Тело идёт устойчивее эталона. Осторожный шаг: ${baseBodySpeed} -> ${suggestedBodySpeed} мл/ч.`,
                action: 'Если будете ускорять, меняйте только один параметр за запуск и контролируйте flood risk вместе с cooling margin.'
            });
        }
    }

    return items.slice(0, 3);

}

function buildRunAdvisorItems(process, previousSuccessfulProcess = null, previousSummary = null) {

    const indicators = process?.metrics?.indicators;
    const type = String(process?.process?.type || '').trim();
    const completedSuccessfully = Boolean(process?.metadata?.completedSuccessfully);
    const verdictItem = buildImprovementVerdictItem(process, previousSuccessfulProcess, previousSummary);
    const comparisonItems = buildProfileComparisonItems(process, previousSuccessfulProcess, previousSummary);
    const adjustmentItems = buildProfileAdjustmentItems(process, previousSuccessfulProcess);
    const items = [
        ...(verdictItem ? [verdictItem] : []),
        ...comparisonItems,
        ...adjustmentItems
    ];

    if (!indicators) {
        return items.slice(0, 6);
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

    const uniqueItems = [];
    const seenTitles = new Set();

    items.forEach((item) => {
        const normalizedItem = normalizeAdvisorItem(item);
        const key = normalizedItem.title;
        if (!key || seenTitles.has(key)) {
            return;
        }
        seenTitles.add(key);
        uniqueItems.push(normalizedItem);
    });

    return uniqueItems.slice(0, 6);

}

function buildNextRunPlanItems(process, previousSuccessfulProcess = null, previousSummary = null) {

    const planItems = [];
    const indicators = getIndicatorShares(process);
    const adjustmentItems = buildProfileAdjustmentItems(process, previousSuccessfulProcess)
        .map((item) => normalizeAdvisorItem(item))
        .filter((item) => item.title);
    const verdictItem = buildImprovementVerdictItem(process, previousSuccessfulProcess, previousSummary);
    const errors = Array.isArray(process?.results?.errors) ? process.results.errors.length : 0;
    const safetyEvents = buildSafetyTimeline(process).length;
    const profileName = getProcessProfile(process);

    if (indicators && (indicators.freshnessShare < 0.95 || errors > 0 || safetyEvents > 0)) {
        planItems.push({
            tone: errors > 0 ? 'danger' : 'warn',
            title: 'Сначала стабилизировать измерения и safety',
            detail: `Перед новой настройкой профиля нужно убрать шум входных данных: telemetry freshness ${formatPercent0(indicators.freshnessShare)}, safety-событий ${safetyEvents}, ошибок ${errors}.`,
            action: 'Пока датчики или safety шумят, сравнение с baseline будет нечестным и любые правки рецепта легко окажутся ложными.'
        });
    }

    if (indicators && (indicators.minCoolingMargin <= 0 || indicators.maxFloodRisk >= 0.8)) {
        planItems.push({
            tone: 'danger',
            title: 'Следующий запуск начать мягче',
            detail: `Текущий прогон заходил в красную зону: cooling margin min ${indicators.minCoolingMargin.toFixed(1)}°C, flood risk max ${formatPercent0(indicators.maxFloodRisk)}.`,
            action: 'Сначала разгрузите колонну и только потом проверяйте тонкие профильные гипотезы.'
        });
    }

    if (adjustmentItems.length > 0) {
        const primaryAdjustment = adjustmentItems[0];
        planItems.push({
            tone: primaryAdjustment.tone,
            title: 'Проверить одну профильную правку',
            detail: `Перенесите в следующий запуск только одну гипотезу: ${formatAdvisorParameterValue(primaryAdjustment.parameterKey, primaryAdjustment.previousValue)} -> ${formatAdvisorParameterValue(primaryAdjustment.parameterKey, primaryAdjustment.suggestedValue)}.`,
            action: `${primaryAdjustment.action} Не добавляйте поверх неё другие изменения, иначе baseline-сравнение потеряет смысл.`
        });

        if (adjustmentItems.length > 1) {
            const reserveAdjustment = adjustmentItems[1];
            planItems.push({
                tone: 'muted',
                title: 'Вторую правку пока держать в резерве',
                detail: `Следующая кандидатная гипотеза уже есть: ${formatAdvisorParameterValue(reserveAdjustment.parameterKey, reserveAdjustment.previousValue)} -> ${formatAdvisorParameterValue(reserveAdjustment.parameterKey, reserveAdjustment.suggestedValue)}.`,
                action: 'Её имеет смысл проверять только после отдельного прогона с первой правкой, а не одновременно.'
            });
        }
    } else if (previousSuccessfulProcess && verdictItem?.tone === 'good') {
        planItems.push({
            tone: 'good',
            title: 'Повторить запуск без новых правок',
            detail: 'Текущий прогон выглядит сильнее baseline, но это ещё нужно подтвердить повторяемостью без новых изменений.',
            action: 'Если следующий запуск повторит результат, его уже можно закреплять как новый рабочий baseline профиля.'
        });
    } else if (!previousSuccessfulProcess && profileName) {
        planItems.push({
            tone: 'muted',
            title: 'Сначала получить первый эталон профиля',
            detail: `Для профиля "${profileName}" пока нет предыдущего успешного baseline, поэтому сейчас важнее чисто завершить повторяемый запуск, чем тонко оптимизировать уставки.`,
            action: 'Первый уверенно успешный прогон станет опорной точкой для уже настоящей инженерной оптимизации.'
        });
    } else {
        planItems.push({
            tone: 'muted',
            title: 'Повторить сценарий без лишних изменений',
            detail: 'По этому прогону нет одной доминирующей правки, которую стоило бы немедленно переносить в профиль.',
            action: 'Лучший следующий шаг сейчас — повторить запуск в тех же условиях и посмотреть, воспроизводится ли картина.'
        });
    }

    planItems.push({
        tone: 'muted',
        title: 'Фиксировать результат после следующего прогона',
        detail: 'После следующего запуска сразу откройте history details и сравните его с текущим baseline, а не по памяти.',
        action: 'Задача Run Advisor не заменить оператора, а оставить вам чистую инженерную цепочку: гипотеза -> один запуск -> сравнение -> решение.'
    });

    return planItems
        .slice(0, 4)
        .map((item, index) => normalizeAdvisorItem({
            ...item,
            kind: 'plan',
            code: `next_run_plan_${index + 1}`,
            title: `Шаг ${index + 1}. ${item.title}`
        }));

}

function appendAdvisorSection(container, title, items) {

    if (!items.length) {
        return;
    }

    const section = document.createElement('div');
    section.className = 'modal-info-item modal-info-item-wide';

    const titleEl = document.createElement('div');
    titleEl.className = 'modal-info-label';
    titleEl.textContent = title;

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

function appendAdvisorFollowUpSection(container, process, previousSuccessfulProcess = null) {

    const items = buildPreviousAdvisorFollowUpItems(process, previousSuccessfulProcess);

    appendAdvisorSection(container, 'Память рекомендаций', items);

}

function appendRunReportSection(container, process, previousSuccessfulProcess = null, previousSummary = null) {

    const items = buildRunReportItems(process, previousSuccessfulProcess, previousSummary);

    appendAdvisorSection(container, 'Run Advisor v1: отчёт по прогону', items);

    return items;

}

function appendRunAdvisorSection(container, process, previousSuccessfulProcess = null, previousSummary = null) {

    const items = buildRunAdvisorItems(process, previousSuccessfulProcess, previousSummary);

    appendAdvisorSection(container, 'Run Advisor: рекомендации', items);

    return items;

}

function appendNextRunPlanSection(container, process, previousSuccessfulProcess = null, previousSummary = null) {

    const items = buildNextRunPlanItems(process, previousSuccessfulProcess, previousSummary);

    appendAdvisorSection(container, 'План следующего запуска', items);

    return items;

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
    appendRunReportSection(resultsGrid, process, previousSuccessfulProcess, previousSummary);
    appendNextRunPlanSection(resultsGrid, process, previousSuccessfulProcess, previousSummary);
    appendAdvisorFollowUpSection(resultsGrid, process, previousSuccessfulProcess);
    const advisorItems = appendRunAdvisorSection(resultsGrid, process, previousSuccessfulProcess, previousSummary);
    appendIndicatorSummarySection(resultsGrid, process);
    appendSafetyTimelineSection(resultsGrid, process);
    appendEventSection(resultsGrid, 'Ошибки и аварии', process.results?.errors || [], 'error');
    appendEventSection(resultsGrid, 'Предупреждения', process.results?.warnings || [], 'warning');
    appendNotesSection(resultsGrid, process.notes);



    // Привязать обработчики к кнопкам экспорта

    const exportCsvBtn = document.getElementById('modal-export-csv');

    const exportJsonBtn = document.getElementById('modal-export-json');
    const exportAnonymizedBtn = document.getElementById('modal-export-anonymized');
    const compareBaselineBtn = document.getElementById('modal-compare-baseline');



    if (exportCsvBtn) {

        exportCsvBtn.onclick = () => exportHistoryCSV(process.id);

    }



    if (exportJsonBtn) {

        exportJsonBtn.onclick = () => exportHistoryJSON(process.id);

    }

    if (exportAnonymizedBtn) {

        exportAnonymizedBtn.onclick = () => window.exportHistoryAnonymized?.(process.id, process);

    }

    if (compareBaselineBtn) {
        const hasBaseline = Boolean(previousSuccessfulProcess?.id || previousSummary?.id);

        compareBaselineBtn.style.display = hasBaseline ? '' : 'none';
        compareBaselineBtn.disabled = !hasBaseline;
        compareBaselineBtn.onclick = hasBaseline
            ? () => window.compareProcessWithBaseline?.(process, {
                previousSummary,
                previousSuccessfulProcess
            })
            : null;
    }



    // Показать модальное окно

    document.getElementById('history-modal').classList.add('active');

    document.body.style.overflow = 'hidden';

    persistAdvisorSnapshotIfNeeded(process, previousSuccessfulProcess, previousSummary, advisorItems).catch((error) => {
        console.warn('Не удалось сохранить advisor snapshot:', error);
    });

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
