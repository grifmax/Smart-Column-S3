import { initTabs } from './core/tabs.js';
import { initTopMenu } from './core/top-menu.js';
import { initOperatorViewToggle } from './ui/operator-view.js';
import { initRuntimeMonitorUi } from './runtime/bars.js';
import { loadPlannedAbv, renderAbvValue } from './runtime/abv.js';
import { initMashingHoldControls } from './modes/mashing-hold.js';
import { initRectificationStartModal } from './modes/rectification.js';
import { loadTheme } from './core/theme.js';
import { loadDemoMode } from './settings/demo.js';
import { initMiniChart } from './ui/mini-chart.js';
import { loadMemoryStatsPreference } from './core/memory.js';
import { initServiceWorker } from './pwa/service-worker-init.js';
import { isCloudProxyMode, detectCloudProxyMode, setCloudOnlyUiVisible, setIsCloudProxyMode } from './core/cloud-detect.js';
import { loadUserInfo } from './cloud/user.js';
import { loadDiscoveredDevices, loadESP32Devices } from './cloud/devices.js';
import { startStatusPolling } from './ui/landing.js';
import { connectWebSocket } from './core/websocket.js';
import { loadPumpInfo, loadVersionInfo } from './settings/pump-version.js';

// Инициализация при загрузке страницы

document.addEventListener('DOMContentLoaded', async function () {

    initTabs();
    initTopMenu();
    initOperatorViewToggle();
    initRuntimeMonitorUi();
    loadPlannedAbv();
    renderAbvValue();

    initMashingHoldControls();
    initRectificationStartModal();

    loadTheme();

    loadDemoMode();  // Загрузить состояние демо-режима

    initMiniChart();

    loadMemoryStatsPreference();

    loadPumpInfo();

    loadVersionInfo();

    initServiceWorker();

    // Определяем режим: локально на ESP32 или через cloud-proxy кабинет
    setIsCloudProxyMode(await detectCloudProxyMode());
    setCloudOnlyUiVisible(isCloudProxyMode);

    if (isCloudProxyMode) {
        loadUserInfo();  // Загрузить информацию о пользователе
        loadESP32Devices();  // Загрузить список устройств ESP32
        loadDiscoveredDevices(); // Устройства с активным PIN
        setInterval(loadDiscoveredDevices, 30000);
    }

    // Запускаем fallback polling сразу, после подключения WS он будет остановлен.
    startStatusPolling(true);
    connectWebSocket();

});
