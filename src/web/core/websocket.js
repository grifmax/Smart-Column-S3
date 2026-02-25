// ============================================================================

// WebSocket

// ============================================================================



export function connectWebSocket() {

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';

    const wsUrl = `${protocol}//${window.location.hostname}/ws`;



    addLog('Подключение к WebSocket...');



    try {

        ws = new WebSocket(wsUrl);



        ws.onopen = function () {

            isConnected = true;
            stopStatusPolling();

            updateConnectionStatus(true);

            addLog('✓ Подключено к контроллеру', 'info');



            // Остановить попытки переподключения

            if (reconnectInterval) {

                clearInterval(reconnectInterval);

                reconnectInterval = null;

            }

        };



        ws.onmessage = function (event) {

            try {

                const data = JSON.parse(event.data);

                updateUI(data);

            } catch (e) {

                console.error('Ошибка парсинга JSON:', e);

            }

        };



        ws.onerror = function (error) {

            console.error('WebSocket error:', error);

            addLog('✗ Ошибка подключения', 'error');

        };



        ws.onclose = function () {

            isConnected = false;
            startStatusPolling(true);

            updateConnectionStatus(false);

            addLog('⚠️ Соединение разорвано. Переподключение...', 'warning');



            // Попытка переподключения каждые 5 секунд

            if (!reconnectInterval) {

                reconnectInterval = setInterval(() => {

                    if (!isConnected) {

                        connectWebSocket();

                    }

                }, 5000);

            }

        };

    } catch (e) {

        console.error('Ошибка создания WebSocket:', e);

        updateConnectionStatus(false);
        startStatusPolling(true);

    }

}



export function sendCommand(action, param = '', value = 0) {

    if (ws && ws.readyState === WebSocket.OPEN) {

        const cmd = { action, param, value };

        ws.send(JSON.stringify(cmd));

        addLog(`📤 Команда: ${action} ${param} ${value}`);

    } else {

        addLog('✗ Нет подключения к контроллеру', 'error');

    }

}



export function updateConnectionStatus(connected) {

    const statusDot = document.getElementById('connection-status');

    const statusText = document.getElementById('connection-text');



    if (connected) {

        statusDot.className = 'status-dot online';

        statusText.textContent = 'Подключено';

    } else {

        statusDot.className = 'status-dot offline';

        statusText.textContent = 'Отключено';

    }

}
