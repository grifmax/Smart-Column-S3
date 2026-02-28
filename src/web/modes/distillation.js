import { MODE_DIST } from '../globals.js';
import { confirmModeSwitch } from './common.js';
import { loadStatus } from '../core/status.js';
import { addLog } from '../core/logs.js';

export async function startDistillation() {

    if (!confirmModeSwitch(MODE_DIST, 'Distillation')) return;

    try {

        addLog('📤 Отправка команды запуска дистилляции...', 'info');



        const response = await fetch('/api/process/start', {

            method: 'POST',

            headers: { 'Content-Type': 'application/json' },

            body: JSON.stringify({ mode: 'distillation' })

        });



        if (response.ok) {

            const data = await response.json();

            addLog('✅ Дистилляция запущена', 'success');

            if (data.warning) {

                addLog('⚠️ ' + data.warning, 'warning');

            }

            setTimeout(loadStatus, 500); // Обновить статус

        } else {

            const error = await response.text();

            addLog('✗ Ошибка (' + response.status + '): ' + error, 'error');

        }

    } catch (e) {

        addLog('✗ Ошибка сети: ' + e.message, 'error');

        console.error('Start distillation error:', e);

    }

}
