// Smart-Column S3 - Web UI (модульная сборка)
// Точка входа — собирается ESBuild в data/app.js

import { saveCloudConfig, generateCloudClaim } from './cloud/cloud-config.js';
import { loadESP32Devices, loadESP32Device, showAddDeviceForm, loadESP32Config, toggleESP32Fields, saveESP32Device, saveESP32Config, activateESP32Device, deleteESP32Device, testESP32Connection, claimDeviceToAccount } from './cloud/devices.js';
import { toggleUserMenu, logout, switchAccount } from './cloud/user.js';
import { addLog, clearLogs, downloadLogs } from './core/logs.js';
import { toggleMemoryStats } from './core/memory.js';
import { setTheme } from './core/theme.js';
import { toggleTopMenu } from './core/top-menu.js';
import { compareSelected, closeCompareModal } from './history/compare.js';
import { viewHistoryDetails, closeHistoryModal } from './history/details.js';
import { exportHistory, exportHistoryCSV, exportHistoryJSON } from './history/export.js';
import { loadHistoryList, applyHistoryFilters } from './history/list.js';
import { clearHistory, deleteHistoryItem, toggleProcessSelection } from './history/selection.js';
import { confirmModeSwitch } from './modes/common.js';
import { startDistillation } from './modes/distillation.js';
import { stopProcess, pauseProcess, resumeProcess, updateHeater, updatePump, toggleValve } from './modes/manual.js';
import { startMashing, startHold, addMashStep, addHoldStep } from './modes/mashing-hold.js';
import { startRectification, startManual, openRectificationStartModal, confirmStartRectification, closeRectificationStartModal, updateRectificationFractionsSum, applyRectificationFeedstockDefaults } from './modes/rectification.js';
import {
    initControlModePanel,
    selectControlMode,
    startSelectedMode,
    initManualRectSettings,
    saveManualRectSettings,
    loadManualRectSettings,
    updateManualHeadsMode,
    calcManualHeadsSpeed,
    calcManualHeadsTime,
    updateManualBodyToTailsMode,
    updateManualTailsMode,
    updateManualTailsStopMode,
    updateManualTailsPwmMode
} from './modes/control-panel.js';
import { showCreateProfileModal, closeProfileModal, saveProfile, viewProfile, closeProfileViewModal, quickLoadProfile, loadProfileToSettings, deleteProfile, clearUserProfiles } from './profiles/crud.js';
import { exportProfile, exportAllProfiles, showImportModal, closeImportModal, doImportProfiles } from './profiles/import-export.js';
import { loadProfilesList } from './profiles/list.js';
import { renderAbvValue } from './runtime/abv.js';
import { openRuntimeEditModal, closeRuntimeEditModal, submitRuntimeEditModal, onRuntimeEditUnitToggle } from './runtime/edit-modal.js';
import { toggleDemoMode, rebootController } from './settings/demo.js';
import { saveEquipment } from './settings/equipment.js';
import { loadMqttSettings, saveMqtt, sendMqttTest } from './settings/mqtt-save.js';
import { toggleMqttFields } from './settings/mqtt.js';
import { loadPumpInfo, loadVersionInfo } from './settings/pump-version.js';
import { saveSecurity, toggleAuthFields } from './settings/security.js';
import { saveTelegramSettings, sendTelegramTest, toggleTelegramFields, loadTelegramSettings } from './settings/telegram.js';
import { saveWiFi } from './settings/wifi.js';
import { calculateAbvCorrection, calculateDilution, fetchCurrentTempForCalc } from './tools/calculators.js';
import { toggleOperatorView } from './ui/operator-view.js';

// Функции для инициализации
import { initTabs } from './core/tabs.js';
import { initTopMenu } from './core/top-menu.js';
import { initOperatorViewToggle } from './ui/operator-view.js';
import { initRuntimeMonitorUi } from './runtime/bars.js';
import { loadPlannedAbv } from './runtime/abv.js';
import { initMashingHoldControls } from './modes/mashing-hold.js';
import { initRectificationStartModal } from './modes/rectification.js';
import { loadTheme } from './core/theme.js';
import { loadDemoMode } from './settings/demo.js';
import { initMiniChart } from './ui/mini-chart.js';
import { loadMemoryStatsPreference } from './core/memory.js';
import { initServiceWorker } from './pwa/service-worker-init.js';
import { detectCloudProxyMode, setCloudOnlyUiVisible } from './core/cloud-detect.js';
import { connectWebSocket } from './core/websocket.js';
import { startStatusPolling } from './ui/landing.js';
import { loadUserInfo } from './cloud/user.js';
import { loadDiscoveredDevices } from './cloud/devices.js';
import { initNotifications, requestNotificationPermission, showNotification, toggleBrowserNotifications, testBrowserNotification } from './core/notifications.js';

// ============================================================================
// Window bindings (для onclick в HTML)
// ============================================================================

window.saveCloudConfig = saveCloudConfig;
window.generateCloudClaim = generateCloudClaim;
window.loadESP32Devices = loadESP32Devices;
window.loadESP32Device = loadESP32Device;
window.showAddDeviceForm = showAddDeviceForm;
window.loadESP32Config = loadESP32Config;
window.toggleESP32Fields = toggleESP32Fields;
window.saveESP32Device = saveESP32Device;
window.saveESP32Config = saveESP32Config;
window.activateESP32Device = activateESP32Device;
window.deleteESP32Device = deleteESP32Device;
window.testESP32Connection = testESP32Connection;
window.claimDeviceToAccount = claimDeviceToAccount;
window.toggleUserMenu = toggleUserMenu;
window.logout = logout;
window.switchAccount = switchAccount;
window.addLog = addLog;
window.clearLogs = clearLogs;
window.downloadLogs = downloadLogs;
window.toggleMemoryStats = toggleMemoryStats;
window.setTheme = setTheme;
window.toggleTopMenu = toggleTopMenu;
window.compareSelected = compareSelected;
window.closeCompareModal = closeCompareModal;
window.viewHistoryDetails = viewHistoryDetails;
window.closeHistoryModal = closeHistoryModal;
window.exportHistory = exportHistory;
window.exportHistoryCSV = exportHistoryCSV;
window.exportHistoryJSON = exportHistoryJSON;
window.loadHistoryList = loadHistoryList;
window.applyHistoryFilters = applyHistoryFilters;
window.clearHistory = clearHistory;
window.deleteHistoryItem = deleteHistoryItem;
window.toggleProcessSelection = toggleProcessSelection;
window.confirmModeSwitch = confirmModeSwitch;
window.startDistillation = startDistillation;
window.stopProcess = stopProcess;
window.pauseProcess = pauseProcess;
window.resumeProcess = resumeProcess;
window.updateHeater = updateHeater;
window.updatePump = updatePump;
window.toggleValve = toggleValve;
window.startMashing = startMashing;
window.startHold = startHold;
window.addMashStep = addMashStep;
window.addHoldStep = addHoldStep;
window.startRectification = startRectification;
window.startManual = startManual;
window.selectControlMode = selectControlMode;
window.startSelectedMode = startSelectedMode;
window.saveManualRectSettings = saveManualRectSettings;
window.loadManualRectSettings = loadManualRectSettings;
window.updateManualHeadsMode = updateManualHeadsMode;
window.calcManualHeadsSpeed = calcManualHeadsSpeed;
window.calcManualHeadsTime = calcManualHeadsTime;
window.updateManualBodyToTailsMode = updateManualBodyToTailsMode;
window.updateManualTailsMode = updateManualTailsMode;
window.updateManualTailsStopMode = updateManualTailsStopMode;
window.updateManualTailsPwmMode = updateManualTailsPwmMode;
window.openRectificationStartModal = openRectificationStartModal;
window.confirmStartRectification = confirmStartRectification;
window.closeRectificationStartModal = closeRectificationStartModal;
window.updateRectificationFractionsSum = updateRectificationFractionsSum;
window.applyRectificationFeedstockDefaults = applyRectificationFeedstockDefaults;
window.showCreateProfileModal = showCreateProfileModal;
window.closeProfileModal = closeProfileModal;
window.saveProfile = saveProfile;
window.viewProfile = viewProfile;
window.closeProfileViewModal = closeProfileViewModal;
window.quickLoadProfile = quickLoadProfile;
window.loadProfileToSettings = loadProfileToSettings;
window.deleteProfile = deleteProfile;
window.clearUserProfiles = clearUserProfiles;
window.exportProfile = exportProfile;
window.exportAllProfiles = exportAllProfiles;
window.showImportModal = showImportModal;
window.closeImportModal = closeImportModal;
window.doImportProfiles = doImportProfiles;
window.loadProfilesList = loadProfilesList;
window.renderAbvValue = renderAbvValue;
window.openRuntimeEditModal = openRuntimeEditModal;
window.closeRuntimeEditModal = closeRuntimeEditModal;
window.submitRuntimeEditModal = submitRuntimeEditModal;
window.onRuntimeEditUnitToggle = onRuntimeEditUnitToggle;
window.toggleDemoMode = toggleDemoMode;
window.rebootController = rebootController;
window.saveEquipment = saveEquipment;
window.saveMqtt = saveMqtt;
window.sendMqttTest = sendMqttTest;
window.toggleMqttFields = toggleMqttFields;
window.loadPumpInfo = loadPumpInfo;
window.loadVersionInfo = loadVersionInfo;
window.saveSecurity = saveSecurity;
window.toggleAuthFields = toggleAuthFields;
window.saveTelegramSettings = saveTelegramSettings;
window.sendTelegramTest = sendTelegramTest;
window.toggleTelegramFields = toggleTelegramFields;
window.loadTelegramSettings = loadTelegramSettings;
window.saveWiFi = saveWiFi;
window.calculateAbvCorrection = calculateAbvCorrection;
window.calculateDilution = calculateDilution;
window.fetchCurrentTempForCalc = fetchCurrentTempForCalc;
window.toggleOperatorView = toggleOperatorView;
window.toggleBrowserNotifications = toggleBrowserNotifications;
window.testBrowserNotification = testBrowserNotification;
window.showNotification = showNotification;

// ============================================================================
// Инициализация при загрузке страницы
// ============================================================================

document.addEventListener('DOMContentLoaded', async function () {
    initTabs();
    initTopMenu();
    initOperatorViewToggle();
    initRuntimeMonitorUi();
    loadPlannedAbv();
    renderAbvValue();

    initMashingHoldControls();
    initRectificationStartModal();
    initManualRectSettings();
    await initControlModePanel();

    loadTheme();
    loadDemoMode();
    initMiniChart();
    loadMemoryStatsPreference();
    loadPumpInfo();
    loadVersionInfo();
    loadMqttSettings();
    loadTelegramSettings();
    initNotifications();
    initServiceWorker();

    // Определяем режим: локально на ESP32 или через cloud-proxy кабинет
    const cloudMode = await detectCloudProxyMode();
    setCloudOnlyUiVisible(cloudMode);

    if (cloudMode) {
        loadUserInfo();
        loadESP32Devices();
        loadDiscoveredDevices();
        setInterval(loadDiscoveredDevices, 30000);
    }

    // Запускаем fallback polling сразу, после подключения WS он будет остановлен
    startStatusPolling(true);
    connectWebSocket();
});
