import { selectedProcesses } from './list.js';
import { addLog } from '../core/logs.js';
import { loadHistoryList } from './list.js';

export function toggleProcessSelection(processId) {

    if (selectedProcesses.has(processId)) {

        selectedProcesses.delete(processId);

    } else {

        selectedProcesses.add(processId);

    }

    updateCompareButton();

}



export function updateCompareButton() {

    const compareBtn = document.getElementById('compare-processes-btn');

    if (compareBtn) {

        compareBtn.disabled = selectedProcesses.size < 2;

        compareBtn.textContent = `📊 Сравнить выбранные (${selectedProcesses.size})`;

    }

}



export function updateHistoryStats(processes) {

    const totalEl = document.getElementById('hist-stat-total');

    const completedEl = document.getElementById('hist-stat-completed');

    const timeEl = document.getElementById('hist-stat-time');

    const energyEl = document.getElementById('hist-stat-energy');



    if (!totalEl) return;



    const total = processes.length;

    const completed = processes.filter(p => p.status === 'completed').length;

    const totalTime = processes.reduce((sum, p) => sum + (p.duration || 0), 0);

    const totalEnergy = 0; // Будет реализовано позже, когда появится поле energy в процессах



    totalEl.textContent = total;

    completedEl.textContent = completed;

    timeEl.textContent = (totalTime / 3600).toFixed(1) + ' ч';

    energyEl.textContent = totalEnergy.toFixed(1) + ' кВт·ч';

}



export async function clearHistory() {

    if (!confirm('Удалить ВСЮ историю процессов? Это действие необратимо!')) {

        return;

    }



    try {

        const response = await fetch('/api/history', {

            method: 'DELETE',

            headers: {

                'Content-Type': 'application/json'

            }

        });



        if (!response.ok) {

            throw new Error('Failed to clear history');

        }



        addLog('🗑️ История полностью очищена', 'info');

        await loadHistoryList();

    } catch (error) {

        console.error('Error clearing history:', error);

        addLog('❌ Ошибка при очистке истории', 'error');

        alert('Ошибка при очистке истории');

    }

}



export async function loadPublicDemoDataset() {

    const replaceExisting = confirm('Перезагрузить demo dataset заново?\n\nOK — сначала удалить старые demo-запуски и загрузить их заново.\nОтмена — просто добавить недостающие demo-запуски.');

    try {

        const response = await fetch('/api/history/demo', {

            method: 'POST',

            headers: {

                'Content-Type': 'application/json'

            },

            body: JSON.stringify({ replace: replaceExisting })

        });



        if (!response.ok) {

            throw new Error('Failed to load public demo dataset');

        }



        const data = await response.json();

        addLog(`🧪 Demo dataset: +${data.imported || 0}, пропущено ${data.skipped || 0}, всего demo ${data.demoCount || 0}`, 'info');

        await loadHistoryList();

    } catch (error) {

        console.error('Error loading public demo dataset:', error);

        addLog('❌ Ошибка загрузки demo dataset', 'error');

        alert('Ошибка загрузки demo dataset');

    }

}



export async function clearPublicDemoDataset() {

    if (!confirm('Удалить только demo dataset из истории?\n\nРеальные процессы останутся нетронутыми.')) {

        return;

    }



    try {

        const response = await fetch('/api/history/demo', {

            method: 'DELETE',

            headers: {

                'Content-Type': 'application/json'

            }

        });



        if (!response.ok) {

            throw new Error('Failed to clear public demo dataset');

        }



        const data = await response.json();

        addLog(`🧹 Demo dataset удалён: ${data.removed || 0} запусков`, 'info');

        await loadHistoryList();

    } catch (error) {

        console.error('Error clearing public demo dataset:', error);

        addLog('❌ Ошибка удаления demo dataset', 'error');

        alert('Ошибка удаления demo dataset');

    }

}



export async function deleteHistoryItem(id) {

    if (!confirm('Удалить этот процесс из истории?')) {

        return;

    }



    try {

        const response = await fetch(`/api/history/${id}`, {

            method: 'DELETE',

            headers: {

                'Content-Type': 'application/json'

            }

        });



        if (!response.ok) {

            throw new Error('Failed to delete history item');

        }



        addLog(`🗑️ Процесс ${id} удалён из истории`, 'info');

        await loadHistoryList();

    } catch (error) {

        console.error('Error deleting history item:', error);

        addLog('❌ Ошибка при удалении процесса', 'error');

        alert('Ошибка при удалении процесса');

    }

}
