import { updateHistoryStats } from './selection.js';
import { addLog } from '../core/logs.js';

// ============================================================================

// История процессов

// ============================================================================



export let historyData = [];



export async function loadHistoryList() {

    try {

        const response = await fetch('/api/history');

        if (!response.ok) {

            throw new Error('Failed to load history');

        }



        const data = await response.json();

        historyData = data.processes || [];



        // Применить фильтры

        applyHistoryFilters();



        addLog(`📚 Загружено процессов: ${historyData.length}`, 'info');

    } catch (error) {

        console.error('Error loading history:', error);

        addLog('❌ Ошибка загрузки истории', 'error');



        // Показать пустой список

        const historyListEl = document.getElementById('history-list');

        if (historyListEl) {

            historyListEl.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--text-secondary);">Нет данных или ошибка загрузки</div>';

        }

    }

}



export function applyHistoryFilters() {

    const typeFilter = document.getElementById('history-filter-type')?.value || 'all';

    const sortBy = document.getElementById('history-sort')?.value || 'date-desc';



    let filtered = [...historyData];



    // Фильтр по типу

    if (typeFilter !== 'all') {

        filtered = filtered.filter(p => p.type === typeFilter);

    }



    // Сортировка

    filtered.sort((a, b) => {

        switch (sortBy) {

            case 'date-desc':

                return b.startTime - a.startTime;

            case 'date-asc':

                return a.startTime - b.startTime;

            case 'duration-desc':

                return b.duration - a.duration;

            case 'duration-asc':

                return a.duration - b.duration;

            default:

                return 0;

        }

    });



    // Отрисовать список

    renderHistoryList(filtered);



    // Обновить статистику

    updateHistoryStats(filtered);

}



export function renderHistoryList(processes) {

    const historyListEl = document.getElementById('history-list');

    if (!historyListEl) return;



    if (processes.length === 0) {

        historyListEl.innerHTML = '<div style="text-align: center; padding: 20px; color: var(--text-secondary);">Нет процессов для отображения</div>';

        return;

    }



    historyListEl.innerHTML = '';



    processes.forEach(process => {

        const itemEl = renderHistoryItem(process);

        historyListEl.appendChild(itemEl);

    });

}



export let selectedProcesses = new Set();

function escapeHtml(text) {

    return String(text ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');

}

function getSafetyBadgeTitle(process) {

    const parts = [];

    if (process.safetyTrip) parts.push('trip');
    if (process.safetyAck) parts.push('ack');
    if (process.safetyRecovery) parts.push('recovery');
    if (process.safetyReset) parts.push('reset');
    if (process.safetyLimited) parts.push('limited');

    return parts.length > 0 ? `Safety: ${parts.join(' -> ')}` : '';

}

function formatReasonCode(reasonCode) {

    const raw = String(reasonCode || '').trim();

    if (!raw || raw === 'RC_NONE' || raw === 'RC_UNSPECIFIED') {

        return '';

    }

    return raw
        .replace(/^RC_/, '')
        .toLowerCase()
        .split('_')
        .map((chunk) => chunk ? chunk[0].toUpperCase() + chunk.slice(1) : '')
        .join(' ');

}

function formatPhaseName(phaseName) {

    const phaseNames = {
        heating: 'Нагрев',
        stabilization: 'Стабилизация',
        post_heads_stabilization: 'Пост-стабилизация',
        heads: 'Отбор голов',
        body: 'Отбор тела',
        tails: 'Отбор хвостов',
        purge: 'Продувка',
        finish: 'Завершение',
        completed: 'Завершено',
        working: 'Работа',
        running: 'Выполнение',
        acid_rest: 'Кислотная пауза',
        protein_rest: 'Белковая пауза',
        beta_amylase: 'Бета-амилаза',
        alpha_amylase: 'Альфа-амилаза',
        mash_out: 'Мэш-аут',
        hold_step: 'Шаг выдержки'
    };

    const raw = String(phaseName || '').trim();

    if (!raw) {

        return '';

    }

    return phaseNames[raw] || raw;

}

function buildOutcomeSummary(process) {

    const phaseName = formatPhaseName(process.lastPhaseName);
    const operatorMessage = String(process.lastOperatorMessage || '').trim();
    const reason = formatReasonCode(process.lastReasonCode);

    if (!phaseName && !operatorMessage && !reason) {

        return '';

    }

    const detail = operatorMessage || reason;

    return detail ? `${phaseName || 'Итог'}: ${detail}` : phaseName;

}



export function renderHistoryItem(process) {

    const div = document.createElement('div');

    div.className = 'history-item';

    div.dataset.processId = process.id;



    const typeNames = {

        rectification: 'Ректификация',

        distillation: 'Дистилляция',

        mashing: 'Затирка',

        hold: 'Выдержка',

        nbk: 'НБК',

        fermentation: 'Ферментация'

    };



    const statusNames = {

        completed: 'Завершен',

        stopped: 'Остановлен',

        error: 'Ошибка'

    };



    const typeName = typeNames[process.type] || process.type;

    const statusName = statusNames[process.status] || process.status;



    const startDate = new Date(process.startTime * 1000);

    const durationHours = (process.duration / 3600).toFixed(1);

    const isSelected = selectedProcesses.has(process.id);
    const safetySummary = String(process.safetySummary || '').trim();
    const safetyState = String(process.safetyState || 'none').trim() || 'none';
    const safetyBadgeTitle = getSafetyBadgeTitle(process);
    const safetyBadge = safetySummary
        ? `<span class="history-safety-badge history-safety-${safetyState}" title="${escapeHtml(safetyBadgeTitle)}">Safety ${escapeHtml(safetySummary)}</span>`
        : '';
    const outcomeSummary = buildOutcomeSummary(process);
    const outcomeLine = outcomeSummary
        ? `<div class="history-outcome" title="${escapeHtml(outcomeSummary)}">${escapeHtml(outcomeSummary)}</div>`
        : '';



    div.innerHTML = `

        <div class="history-header">

            <div style="display: flex; align-items: center; gap: 10px;">

                <input type="checkbox"

                       class="history-checkbox"

                       data-process-id="${process.id}"

                       ${isSelected ? 'checked' : ''}

                       onchange="toggleProcessSelection('${process.id}')">

                <div>

                    <span class="history-type history-type-${process.type}">${typeName}</span>

                    <span class="history-status history-status-${process.status}">${statusName}</span>

                    ${safetyBadge}

                    ${outcomeLine}

                </div>

            </div>

            <div class="history-date">${startDate.toLocaleString('ru-RU')}</div>

        </div>

        <div class="history-info">

            <div class="history-metric">

                <span class="metric-label">⏱️ Длительность:</span>

                <span class="metric-value">${durationHours} ч</span>

            </div>

            <div class="history-metric">

                <span class="metric-label">💧 Объём:</span>

                <span class="metric-value">${process.totalVolume || 0} мл</span>

            </div>

        </div>

        <div class="history-actions">

            <button class="btn-secondary" onclick="viewHistoryDetails('${process.id}')">👁️ Подробно</button>

            <button class="btn-secondary" onclick="exportHistory('${process.id}')">📥 Экспорт</button>

            <button class="btn-danger" onclick="deleteHistoryItem('${process.id}')">🗑️ Удалить</button>

        </div>

    `;



    return div;

}
