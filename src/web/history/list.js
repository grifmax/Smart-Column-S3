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
