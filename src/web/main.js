// Smart-Column S3 - Web UI (модульная сборка)
// Точка входа — собирается ESBuild в data/app.js

import { saveCloudConfig, generateCloudClaim } from './cloud/cloud-config.js';
import { loadESP32Devices, loadESP32Device, showAddDeviceForm, loadESP32Config, toggleESP32Fields, saveESP32Device, saveESP32Config, activateESP32Device, deleteESP32Device, testESP32Connection, claimDeviceToAccount } from './cloud/devices.js';
import { toggleUserMenu, logout, switchAccount } from './cloud/user.js';
import { addLog, clearLogs, downloadLogs, initLogsPage } from './core/logs.js';
import { toggleMemoryStats } from './core/memory.js';
import { setTheme } from './core/theme.js';
import { toggleTopMenu } from './core/top-menu.js';
import { compareSelected, compareProcessWithBaseline, closeCompareModal } from './history/compare.js';
import { viewHistoryDetails, closeHistoryModal } from './history/details.js';
import { exportHistory, exportHistoryCSV, exportHistoryJSON } from './history/export.js';
import { loadHistoryList, applyHistoryFilters } from './history/list.js';
import { clearHistory, clearPublicDemoDataset, deleteHistoryItem, loadPublicDemoDataset, toggleProcessSelection } from './history/selection.js';
import { confirmModeSwitch } from './modes/common.js';
import { startDistillation } from './modes/distillation.js';
import { stopProcess, pauseProcess, resumeProcess, updateHeater, updatePump, toggleValve } from './modes/manual.js';
import { startMashing, startHold, addMashStep, addHoldStep } from './modes/mashing-hold.js';
import { startRectification, startManual, openRectificationStartModal, confirmStartRectification, closeRectificationStartModal, updateRectificationFractionsSum, applyRectificationFeedstockDefaults } from './modes/rectification.js';
import {
    initControlModePanel,
    selectControlMode,
    startSelectedMode,
    confirmModeStart,
    closeModeStartModal,
    renderControlStartChecklist,
    renderControlStartState,
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
import { showCreateProfileModal, showEditProfileModal, showDuplicateProfileModal, editCurrentProfile, duplicateCurrentProfile, toggleProfileCategoryFields, addProfileMashStep, copyCurrentMashingToProfileForm, closeProfileModal, saveProfile, viewProfile, closeProfileViewModal, quickLoadProfile, loadProfileToSettings, deleteProfile, clearUserProfiles } from './profiles/crud.js';
import { exportProfile, exportAllProfiles, showImportModal, closeImportModal, doImportProfiles } from './profiles/import-export.js';
import { loadProfilesList } from './profiles/list.js';
import { renderAbvValue } from './runtime/abv.js';
import { openRuntimeEditModal, closeRuntimeEditModal, submitRuntimeEditModal, onRuntimeEditUnitToggle } from './runtime/edit-modal.js';
import { toggleDemoMode, rebootController } from './settings/demo.js';
import { saveEquipment, saveStirrerSettings, loadEquipmentSettings, initEquipmentSettingsUi, addCubeExtenderVolume } from './settings/equipment.js';
import { initEquipmentTestingUi, initSettingsWorkbenchUi } from './settings/equipment-testing.js';
import { loadSafetySettings, saveSafetySettings } from './settings/safety-thresholds.js';
import { loadMqttSettings, saveMqtt, sendMqttTest } from './settings/mqtt-save.js';
import { toggleMqttFields } from './settings/mqtt.js';
import { loadPumpInfo, loadVersionInfo } from './settings/pump-version.js';
import { loadSecuritySettings, saveSecurity, toggleAuthFields } from './settings/security.js';
import {
    saveWiFi,
    initWiFiSettings,
    loadWiFiStatus,
    loadWiFiProfiles,
    scanWiFiNetworks,
    saveWiFiProfile,
    connectWiFiNetwork,
    connectSavedWiFiProfile,
    moveWiFiProfile,
    deleteWiFiProfile,
    editWiFiProfile,
    toggleWiFiStaticFields,
    cancelWiFiSelection
} from './settings/wifi.js';
import {
    initCalibrationTab,
    loadCalibrationData,
    scanCalibrationSensors,
    scanCalibrationSensorsRaw,
    scanCalibrationSensorsRawSeries,
    assignTempSensorAddress,
    calibrateTempOffset,
    calibrateTempReference,
    fillPressurePointFromCurrentV2,
    addPressurePointFromCurrentV2,
    applyPressureZeroTrimV2,
    savePressureCalibrationV2,
    clearPressureCalibrationV2,
    clearPressureZeroTrimV2,
    fillHydrometerPointFromCurrent,
    saveHydrometerCalibration,
    clearHydrometerCalibration,
    exportCalibrationSnapshot,
    openCalibrationImportDialog,
    onCalibrationSnapshotFileChange,
    applyCalibrationSnapshot,
    updateCalibrationTime,
    startCalibration,
    stopCalibration,
    applyCalibration,
    cancelCalibration
} from './settings/calibration.js';
import {
    calculateAbvCorrection,
    calculateBlendFractions,
    calculateDensityConverter,
    calculateDilution,
    calculateFermentation,
    calculateHeatingCost,
    calculatePotentialAlcohol,
    calculateReverseBatch,
    calculateSelectionRate,
    calculateYieldFractions,
    fetchCurrentTempForCalc,
    updateFermentationMode,
    updatePotentialAlcoholMode
} from './tools/calculators.js';
import { initToolsWorkbench } from './tools/panel.js';
import { toggleOperatorView } from './ui/operator-view.js';
import { zoomScheme, syncSchemeZoomLayout } from './ui/scheme.js';

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
import { initNotifications, showNotification, toggleBrowserNotifications, testBrowserNotification } from './core/notifications.js';

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
window.compareProcessWithBaseline = compareProcessWithBaseline;
window.closeCompareModal = closeCompareModal;
window.viewHistoryDetails = viewHistoryDetails;
window.closeHistoryModal = closeHistoryModal;
window.exportHistory = exportHistory;
window.exportHistoryCSV = exportHistoryCSV;
window.exportHistoryJSON = exportHistoryJSON;
window.loadHistoryList = loadHistoryList;
window.applyHistoryFilters = applyHistoryFilters;
window.clearHistory = clearHistory;
window.loadPublicDemoDataset = loadPublicDemoDataset;
window.clearPublicDemoDataset = clearPublicDemoDataset;
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
window.confirmModeStart = confirmModeStart;
window.closeModeStartModal = closeModeStartModal;
window.renderControlStartChecklist = renderControlStartChecklist;
window.renderControlStartState = renderControlStartState;
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
window.showEditProfileModal = showEditProfileModal;
window.showDuplicateProfileModal = showDuplicateProfileModal;
window.editCurrentProfile = editCurrentProfile;
window.duplicateCurrentProfile = duplicateCurrentProfile;
window.onProfileCategoryChange = toggleProfileCategoryFields;
window.addProfileMashStep = addProfileMashStep;
window.copyCurrentMashingToProfileForm = copyCurrentMashingToProfileForm;
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
window.saveStirrerSettings = saveStirrerSettings;
window.loadEquipmentSettings = loadEquipmentSettings;
window.loadSafetySettings = loadSafetySettings;
window.saveSafetySettings = saveSafetySettings;
window.addCubeExtenderVolume = addCubeExtenderVolume;
window.saveMqtt = saveMqtt;
window.sendMqttTest = sendMqttTest;
window.toggleMqttFields = toggleMqttFields;
window.loadPumpInfo = loadPumpInfo;
window.loadVersionInfo = loadVersionInfo;
window.saveSecurity = saveSecurity;
window.toggleAuthFields = toggleAuthFields;
window.saveWiFi = saveWiFi;
window.loadWiFiStatus = loadWiFiStatus;
window.loadWiFiProfiles = loadWiFiProfiles;
window.scanWiFiNetworks = scanWiFiNetworks;
window.saveWiFiProfile = saveWiFiProfile;
window.connectWiFiNetwork = connectWiFiNetwork;
window.connectSavedWiFiProfile = connectSavedWiFiProfile;
window.moveWiFiProfile = moveWiFiProfile;
window.deleteWiFiProfile = deleteWiFiProfile;
window.editWiFiProfile = editWiFiProfile;
window.toggleWiFiStaticFields = toggleWiFiStaticFields;
window.cancelWiFiSelection = cancelWiFiSelection;
window.loadCalibrationData = loadCalibrationData;
window.scanCalibrationSensors = scanCalibrationSensors;
window.scanCalibrationSensorsRaw = scanCalibrationSensorsRaw;
window.scanCalibrationSensorsRawSeries = scanCalibrationSensorsRawSeries;
window.assignTempSensorAddress = assignTempSensorAddress;
window.calibrateTempOffset = calibrateTempOffset;
window.calibrateTempReference = calibrateTempReference;
window.fillPressurePointFromCurrent = fillPressurePointFromCurrentV2;
window.addPressurePointFromCurrent = addPressurePointFromCurrentV2;
window.applyPressureZeroTrim = applyPressureZeroTrimV2;
window.savePressureCalibration = savePressureCalibrationV2;
window.clearPressureCalibration = clearPressureCalibrationV2;
window.clearPressureZeroTrim = clearPressureZeroTrimV2;
window.fillHydrometerPointFromCurrent = fillHydrometerPointFromCurrent;
window.saveHydrometerCalibration = saveHydrometerCalibration;
window.clearHydrometerCalibration = clearHydrometerCalibration;
window.exportCalibrationSnapshot = exportCalibrationSnapshot;
window.openCalibrationImportDialog = openCalibrationImportDialog;
window.onCalibrationSnapshotFileChange = onCalibrationSnapshotFileChange;
window.applyCalibrationSnapshot = applyCalibrationSnapshot;
window.updateCalibrationTime = updateCalibrationTime;
window.startCalibration = startCalibration;
window.stopCalibration = stopCalibration;
window.applyCalibration = applyCalibration;
window.cancelCalibration = cancelCalibration;
window.calculateAbvCorrection = calculateAbvCorrection;
window.calculateBlendFractions = calculateBlendFractions;
window.calculateDensityConverter = calculateDensityConverter;
window.calculateDilution = calculateDilution;
window.calculateFermentation = calculateFermentation;
window.calculateHeatingCost = calculateHeatingCost;
window.calculatePotentialAlcohol = calculatePotentialAlcohol;
window.calculateReverseBatch = calculateReverseBatch;
window.calculateSelectionRate = calculateSelectionRate;
window.calculateYieldFractions = calculateYieldFractions;
window.fetchCurrentTempForCalc = fetchCurrentTempForCalc;
window.updateFermentationMode = updateFermentationMode;
window.updatePotentialAlcoholMode = updatePotentialAlcoholMode;
window.toggleOperatorView = toggleOperatorView;
window.toggleBrowserNotifications = toggleBrowserNotifications;
window.testBrowserNotification = testBrowserNotification;
window.showNotification = showNotification;
window.zoomScheme = zoomScheme;

// ============================================================================
// Инициализация при загрузке страницы
// ============================================================================

function initSidebarCollapse() {
    const btn = document.getElementById('sidebar-collapse-btn');
    const sidebar = document.getElementById('main-sidebar');
    if (!btn || !sidebar) return;
    const saved = localStorage.getItem('sidebar-collapsed');
    if (saved === '1') sidebar.classList.add('collapsed');
    btn.addEventListener('click', () => {
        sidebar.classList.toggle('collapsed');
        localStorage.setItem('sidebar-collapsed', sidebar.classList.contains('collapsed') ? '1' : '0');
    });
}

function removeTopBarAbvStatus() {
    const abvEl = document.getElementById('abv');
    const statusItem = abvEl?.closest('.status-item');
    if (!statusItem) return;

    const separator = statusItem.previousElementSibling;
    if (separator?.classList?.contains('separator')) {
        separator.remove();
    }

    statusItem.remove();
}

document.addEventListener('DOMContentLoaded', async function () {
    initTabs();
    initTopMenu();
    initSidebarCollapse();
    initOperatorViewToggle();
    initRuntimeMonitorUi();
    removeTopBarAbvStatus();
    loadPlannedAbv();
    renderAbvValue();

    initMashingHoldControls();
    initRectificationStartModal();
    initManualRectSettings();
    await initControlModePanel();

    loadTheme();
    loadDemoMode();
    initLogsPage();
    initMiniChart();
    syncSchemeZoomLayout();
    window.addEventListener('resize', syncSchemeZoomLayout);
    loadMemoryStatsPreference();
    loadPumpInfo();
    loadVersionInfo();
    updatePotentialAlcoholMode();
    updateFermentationMode();
    initToolsWorkbench();
    initEquipmentSettingsUi();
    initEquipmentTestingUi();
    initSettingsWorkbenchUi();
    loadEquipmentSettings();
    loadSafetySettings();
    loadSecuritySettings();
    loadMqttSettings();
    loadProfilesList();
    initWiFiSettings();
    initCalibrationTab();
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
