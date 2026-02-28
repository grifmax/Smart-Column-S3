import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';
import { sendCommand } from '../core/websocket.js';

export async function stopProcess() {

    if (!confirm('Остановить процесс?')) return;



    try {

        const response = await fetch('/api/process/stop', {

            method: 'POST'

        });



        if (response.ok) {

            addLog('✓ Процесс остановлен', 'warning');

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            addLog('✗ Ошибка остановки', 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка: ' + e.message, 'error');

    }

}



export async function pauseProcess() {

    try {

        const response = await fetch('/api/process/pause', {

            method: 'POST'

        });



        if (response.ok) {

            addLog('✓ Процесс приостановлен', 'info');

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            addLog('✗ Ошибка паузы', 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка: ' + e.message, 'error');

    }

}



export async function resumeProcess() {

    try {

        const response = await fetch('/api/process/resume', {

            method: 'POST'

        });



        if (response.ok) {

            addLog('✓ Процесс возобновлен', 'info');

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            addLog('✗ Ошибка возобновления', 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка: ' + e.message, 'error');

    }

}



export function updateHeater(value) {

    document.getElementById('heater-value').textContent = value;

    sendCommand('heater', 'power', parseInt(value));

}



export function updatePump(value) {

    document.getElementById('pump-value').textContent = value;

    sendCommand('pump', 'speed', parseInt(value));

}



export function toggleValve(name) {

    sendCommand('valve', name, 1);

    addLog(`🔄 Переключение клапана: ${name}`);

}
