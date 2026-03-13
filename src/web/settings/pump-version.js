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

        const setNodeText = (id, value) => {
            const el = document.getElementById(id);
            if (el) el.textContent = value;
        };



        // Обновить информацию о прошивке

        if (data.firmware) {

            const firmwareVersion = data.firmware.version || 'Unknown';

            setNodeText('firmware-version', firmwareVersion);

            setNodeText('firmware-build-date', data.firmware.buildDate || 'Unknown');

            setNodeText('firmware-build-time', data.firmware.buildTime || 'Unknown');

            setNodeText('sidebar-version', `v${firmwareVersion}`);

            setNodeText('footer-firmware-version', firmwareVersion);

        }



        if (data.board) {

            const flashMB = (data.board.flashSize / (1024 * 1024)).toFixed(0);

            const psramMB = (data.board.psramSize / (1024 * 1024)).toFixed(0);

            setNodeText(
                'board-chip',
                `${data.board.chip} (Flash: ${flashMB}MB, PSRAM: ${psramMB}MB)`
            );

        }



        // Обновить информацию о фронтенде

        if (data.frontend) {

            setNodeText(
                'frontend-build-date',
                data.frontend.buildDate || data.frontend.note || 'Unknown'
            );

            setNodeText(
                'frontend-build-time',
                data.frontend.buildTime || '-'
            );

        }



        addLog('✔ Информация о версиях обновлена', 'success');

    } catch (error) {

        console.error('Error loading version info:', error);

        const setNodeText = (id, value) => {
            const el = document.getElementById(id);
            if (el) el.textContent = value;
        };

        setNodeText('firmware-version', 'Ошибка загрузки');

        setNodeText('frontend-build-date', 'Ошибка загрузки');

        setNodeText('sidebar-version', 'v?');

        setNodeText('footer-firmware-version', '?');

        addLog('✗ Ошибка загрузки версий', 'error');

    }

}
