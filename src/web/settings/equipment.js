import { sendCommand } from '../core/websocket.js';
import { addLog } from '../core/logs.js';

export async function saveEquipment() {

    const heaterPower = document.getElementById('heater-power-w').value;

    const columnHeight = document.getElementById('column-height').value;

    const mlPerRev = parseFloat(document.getElementById('pump-ml-per-rev').value);

    const stepsPerRev = parseInt(document.getElementById('pump-steps-per-rev').value);



    // Проверка и сохранение параметров насоса

    const pumpData = {};

    let hasPumpData = false;



    if (mlPerRev && mlPerRev > 0) {

        pumpData.mlPerRev = mlPerRev;

        hasPumpData = true;

    }



    if (stepsPerRev && stepsPerRev > 0) {

        pumpData.stepsPerRev = stepsPerRev;

        hasPumpData = true;

    }



    if (hasPumpData) {

        try {

            const response = await fetch('/api/calibration/pump', {

                method: 'POST',

                headers: { 'Content-Type': 'application/json' },

                body: JSON.stringify(pumpData)

            });



            if (response.ok) {

                let msg = '✓ Параметры насоса сохранены:';

                if (pumpData.mlPerRev) msg += ' ' + pumpData.mlPerRev.toFixed(3) + ' мл/об';

                if (pumpData.stepsPerRev) msg += ', ' + pumpData.stepsPerRev + ' шагов/об';

                addLog(msg, 'success');

            } else {

                addLog('✗ Ошибка сохранения параметров насоса', 'error');

            }

        } catch (error) {

            addLog('✗ Ошибка соединения при сохранении насоса', 'error');

        }

    }



    // Сохранение других параметров оборудования (через WebSocket)

    sendCommand('equipment', 'save', 0);

    addLog('💾 Настройки оборудования сохранены', 'info');

}
