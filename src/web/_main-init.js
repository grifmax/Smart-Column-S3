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
    loadTelegramSettings();

    initServiceWorker();

    // Определяем режим: локально на ESP32 или через cloud-proxy кабинет
    isCloudProxyMode = await detectCloudProxyMode();
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
