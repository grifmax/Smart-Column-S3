export async function exportHistory(id, format = null) {

    try {

        // Если формат не указан, спросить у пользователя

        if (!format) {

            const choice = confirm('Выберите формат экспорта:\n\nОК - CSV (таблица)\nОтмена - JSON (данные)');

            format = choice ? 'csv' : 'json';

        }



        addLog(`📥 Экспорт процесса ${id} в формате ${format.toUpperCase()}...`, 'info');



        // Открыть экспорт в новой вкладке

        window.open(`/api/history/${id}/export?format=${format}`, '_blank');



        addLog(`✅ Экспорт процесса ${id} начат`, 'info');

    } catch (error) {

        console.error('Error exporting history:', error);

        addLog('✗ Ошибка экспорта', 'error');

    }

}



export async function exportHistoryCSV(id) {

    await exportHistory(id, 'csv');

}



export async function exportHistoryJSON(id) {

    await exportHistory(id, 'json');

}
