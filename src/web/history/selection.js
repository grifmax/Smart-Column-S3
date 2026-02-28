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
