import { ws, setWs, isConnected, setIsConnected, reconnectInterval, setReconnectInterval } from '../globals.js';
import { startStatusPolling, stopStatusPolling } from '../ui/landing.js';
import { updateUI } from '../ui/update-ui.js';
import { addLog } from './logs.js';

// ============================================================================

// WebSocket

// ============================================================================



export function connectWebSocket() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return;
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;



    addLog('Подключение к WebSocket...');



    try {

        const socket = new WebSocket(wsUrl);
        setWs(socket);

        socket.onopen = function () {

            setIsConnected(true);
            stopStatusPolling();

            updateConnectionStatus(true);

            addLog('✓ Подключено к контроллеру', 'info');



            // Остановить попытки переподключения

            if (reconnectInterval) {

                clearInterval(reconnectInterval);

                setReconnectInterval(null);

            }

        };



        socket.onmessage = function (event) {

            try {

                const data = JSON.parse(event.data);

                updateUI(data);

            } catch (e) {

                console.error('Ошибка парсинга JSON:', e);

            }

        };



        socket.onerror = function (error) {

            console.error('WebSocket error:', error);

            addLog('✗ Ошибка подключения', 'error');

        };



        socket.onclose = function () {

            setIsConnected(false);
            if (ws === socket) {
                setWs(null);
            }
            startStatusPolling(true);

            updateConnectionStatus(false);

            addLog('⚠️ Соединение разорвано. Переподключение...', 'warning');



            // Попытка переподключения каждые 5 секунд

            if (!reconnectInterval) {

                setReconnectInterval(setInterval(() => {

                    if (!isConnected) {

                        connectWebSocket();

                    }

                }, 5000));

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

    if (!statusDot || !statusText) return;



    if (connected) {

        statusDot.className = 'status-dot online';

        statusText.textContent = 'Подключено';

    } else {

        statusDot.className = 'status-dot offline';

        statusText.textContent = 'Отключено';

    }

}
