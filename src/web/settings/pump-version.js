// ============================================================================

// Загрузка информации о насосе

// ============================================================================



export async function loadPumpInfo() {

    try {

        const response = await fetch('/api/calibration');

        if (!response.ok) {

            throw new Error('Failed to load calibration data');

        }



        const data = await response.json();



        // Обновить информацию о насосе

        const mlPerRevEl = document.getElementById('pump-ml-per-rev');

        const stepsPerRevEl = document.getElementById('pump-steps-per-rev');



        if (mlPerRevEl && data.pump && data.pump.mlPerRev !== undefined && data.pump.mlPerRev !== null) {

            // Теперь это input поле, устанавливаем value

            mlPerRevEl.value = data.pump.mlPerRev.toFixed(3);

            mlPerRevEl.placeholder = 'Загрузка...';

        } else if (mlPerRevEl) {

            mlPerRevEl.value = '';

            mlPerRevEl.placeholder = 'Загрузка...';

        }



        if (stepsPerRevEl && data.pump && data.pump.stepsPerRev !== undefined && data.pump.stepsPerRev !== null) {

            // Показываем общее количество шагов

            const totalSteps = (data.pump.stepsPerRev || 0) * (data.pump.microsteps || 1);

            stepsPerRevEl.value = totalSteps;

            stepsPerRevEl.placeholder = 'Загрузка...';

        } else if (stepsPerRevEl) {

            stepsPerRevEl.value = '';

            stepsPerRevEl.placeholder = 'Загрузка...';

        }

    } catch (error) {

        console.error('Error loading pump info:', error);

        const mlPerRevEl = document.getElementById('pump-ml-per-rev');

        const stepsPerRevEl = document.getElementById('pump-steps-per-rev');



        if (mlPerRevEl) mlPerRevEl.placeholder = 'Ошибка загрузки';

        if (stepsPerRevEl) stepsPerRevEl.placeholder = 'Ошибка загрузки';

    }

}



// Загрузка информации о версиях

export async function loadVersionInfo() {

    try {

        const response = await fetch('/api/version');

        if (!response.ok) {

            throw new Error('Failed to load version info');

        }



        const data = await response.json();



        // Обновить информацию о прошивке

        if (data.firmware) {

            document.getElementById('firmware-version').textContent = data.firmware.version || 'Unknown';

            document.getElementById('firmware-build-date').textContent = data.firmware.buildDate || 'Unknown';

            document.getElementById('firmware-build-time').textContent = data.firmware.buildTime || 'Unknown';

        }



        if (data.board) {

            const flashMB = (data.board.flashSize / (1024 * 1024)).toFixed(0);

            const psramMB = (data.board.psramSize / (1024 * 1024)).toFixed(0);

            document.getElementById('board-chip').textContent =

                `${data.board.chip} (Flash: ${flashMB}MB, PSRAM: ${psramMB}MB)`;

        }



        // Обновить информацию о фронтенде

        if (data.frontend) {

            document.getElementById('frontend-build-date').textContent =

                data.frontend.buildDate || data.frontend.note || 'Unknown';

            document.getElementById('frontend-build-time').textContent =

                data.frontend.buildTime || '-';

        }



        addLog('✔ Информация о версиях обновлена', 'success');

    } catch (error) {

        console.error('Error loading version info:', error);

        document.getElementById('firmware-version').textContent = 'Ошибка загрузки';

        document.getElementById('frontend-build-date').textContent = 'Ошибка загрузки';

        addLog('✗ Ошибка загрузки версий', 'error');

    }

}
