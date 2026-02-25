/**
 * Скрипт разбивки app.js на модули
 * Запуск: node scripts/split-app.mjs
 */
import fs from 'fs';
import path from 'path';

const SRC = 'data/app.js.bak';
const OUT = 'src/web';

// Читаем исходник
const lines = fs.readFileSync(SRC, 'utf-8').replace(/\r\n/g, '\n').split('\n');

// Таблица модулей: [файл, startLine, endLine] (1-indexed, inclusive)
const modules = [
    // globals.js: глобальные переменные и константы (строки 1-150)
    ['globals.js', 1, 150],
    // runtime/helpers.js: утилиты toFinite, clampPercent, escapeHtml, formatDuration, normalizeAbv
    ['runtime/helpers.js', 152, 183],
    // runtime/abv.js: ABV-related
    ['runtime/abv.js', 185, 240],
    // runtime/state.js: updateRuntimeStateFrom*, estimateRectTargets
    ['runtime/state.js', 242, 340],
    // runtime/bars.js: renderRuntimeBars, updateManualTiles, renderModeRuntimeCard, initRuntimeMonitorUi
    ['runtime/bars.js', 342, 573],
    // runtime/edit-modal.js: getRuntimeEditConfig, open/close/submit modal, onUnitToggle
    ['runtime/edit-modal.js', 575, 818],
    // ui/landing.js: updateLandingUi + status polling
    ['ui/landing.js', 820, 896],
    // core/cloud-detect.js: detectCloudProxyMode, setCloudOnlyUiVisible
    ['core/cloud-detect.js', 897, 937],
    // core/top-menu.js: closeTopMenu, positionTopMenuDropdown, toggleTopMenu, initTopMenu
    ['core/top-menu.js', 939, 1007],
    // ui/operator-view.js: setOperatorView, toggleOperatorView, getAutoOperatorView, initOperatorViewToggle
    ['ui/operator-view.js', 1008, 1068],
    // main-init.js: DOMContentLoaded handler (будет вынесен в main.js)
    ['_main-init.js', 1070, 1115],
    // core/websocket.js: connectWebSocket, sendCommand, updateConnectionStatus
    ['core/websocket.js', 1119, 1282],
    // ui/mini-chart.js: initMiniChart, updateMiniChart
    ['ui/mini-chart.js', 1283, 1529],
    // ui/update-ui.js: updateUI, formatUptime, pad
    ['ui/update-ui.js', 1530, 1813],
    // core/tabs.js: initTabs, activateTabById
    ['core/tabs.js', 1814, 1887],
    // modes/common.js: confirmModeSwitch
    ['modes/common.js', 1888, 1918],
    // modes/rectification.js: RECT_FEEDSTOCK_DEFAULTS и всё до startDistillation
    ['modes/rectification.js', 1919, 2130],
    // modes/distillation.js: startDistillation
    ['modes/distillation.js', 2134, 2186],
    // modes/mashing-hold.js: initMashingHoldControls, createStepRow, addMashStep, etc.
    ['modes/mashing-hold.js', 2187, 2438],
    // cloud/cloud-config.js: updateCloudUiFromStatus, saveCloudConfig, generateCloudClaim
    ['cloud/cloud-config.js', 2439, 2527],
    // modes/manual.js: stopProcess, pauseProcess, resumeProcess, updateHeater, updatePump, toggleValve
    ['modes/manual.js', 2529, 2661],
    // core/status.js: loadStatus, updateUIFromStatus, updateButtonStates, updateHeaterSlider
    ['core/status.js', 2662, 3098],
    // settings/wifi.js
    ['settings/wifi.js', 3099, 3131],
    // settings/equipment.js: saveEquipment
    ['settings/equipment.js', 3132, 3223],
    // settings/mqtt.js: toggleMqttFields, saveMqtt
    ['settings/mqtt.js', 3224, 3234],
    // settings/telegram.js: toggleTelegramFields, loadTelegramSettings, saveTelegramSettings, sendTelegramTest
    ['settings/telegram.js', 3236, 3424],
    // settings/mqtt-save.js: saveMqtt
    ['settings/mqtt-save.js', 3426, 3465],
    // settings/security.js: toggleAuthFields, saveSecurity
    ['settings/security.js', 3466, 3506],
    // settings/demo.js: toggleDemoMode, loadDemoMode, rebootController
    ['settings/demo.js', 3507, 3618],
    // core/theme.js: setTheme, loadTheme
    ['core/theme.js', 3622, 3660],
    // core/logs.js: addLog, clearLogs, downloadLogs
    ['core/logs.js', 3661, 3730],
    // core/memory.js: updateMemoryStats, toggleMemoryStats, loadMemoryStatsPreference
    ['core/memory.js', 3731, 3842],
    // settings/pump-version.js: loadPumpInfo, loadVersionInfo
    ['settings/pump-version.js', 3843, 4014],
    // history/list.js: historyData, loadHistoryList, applyHistoryFilters, renderHistoryList, renderHistoryItem
    ['history/list.js', 4015, 4304],
    // history/selection.js: selectedProcesses, toggleProcessSelection, updateCompareButton, updateHistoryStats, clearHistory, deleteHistoryItem
    ['history/selection.js', 4305, 4480],
    // history/details.js: viewHistoryDetails, showHistoryDetailsModal, closeHistoryModal, renderTempChart, renderPowerChart, renderPhases
    ['history/details.js', 4484, 5147],
    // history/export.js: exportHistory, exportHistoryCSV, exportHistoryJSON
    ['history/export.js', 5148, 5200],
    // history/compare.js: compareSelected, showCompareModal, closeCompareModal, render*Compare*
    ['history/compare.js', 5201, 5770],
    // profiles/list.js: loadProfilesList, renderProfilesList, renderProfileItem, updateProfilesStats
    ['profiles/list.js', 5771, 6006],
    // profiles/crud.js: showCreateProfileModal, closeProfileModal, saveProfile, viewProfile, showProfileViewModal, closeProfileViewModal, quickLoadProfile, loadProfileToSettings, deleteProfile, clearUserProfiles
    ['profiles/crud.js', 6007, 6516],
    // profiles/import-export.js: exportProfile, exportAllProfiles, showImportModal, closeImportModal, doImportProfiles
    ['profiles/import-export.js', 6517, 6778],
    // cloud/user.js: close-compare-modal listener + loadUserInfo, toggleUserMenu, logout, switchAccount
    ['cloud/user.js', 6779, 6968],
    // cloud/devices.js: loadDiscoveredDevices, claimDeviceToAccount, loadESP32Devices, etc.
    ['cloud/devices.js', 6969, 7742],
    // pwa/service-worker-init.js: initServiceWorker
    ['pwa/service-worker-init.js', 7743, 7760],
    // tools/calculators.js: fetchCurrentTempForCalc, calculateAbvCorrection, calculateDilution
    ['tools/calculators.js', 7761, 7828],
    // ui/scheme.js: updateInteractiveScheme
    ['ui/scheme.js', 7829, 8110],
];

// Создаем папки
const dirs = new Set(modules.map(m => path.dirname(path.join(OUT, m[0]))));
dirs.forEach(d => fs.mkdirSync(d, { recursive: true }));

// Генерируем файлы
let totalBytes = 0;
modules.forEach(([file, startLine, endLine]) => {
    const chunk = lines.slice(startLine - 1, endLine);
    // Убираем пустые строки в начале и конце
    while (chunk.length && chunk[0].trim() === '') chunk.shift();
    while (chunk.length && chunk[chunk.length - 1].trim() === '') chunk.pop();

    const content = chunk.join('\n') + '\n';
    const outPath = path.join(OUT, file);
    fs.writeFileSync(outPath, content, 'utf-8');
    totalBytes += content.length;
    console.log(`  ${outPath} (${chunk.length} lines)`);
});

console.log(`\n✅ Created ${modules.length} modules (${(totalBytes / 1024).toFixed(1)} KB total)`);
console.log('Next: add imports/exports and create main.js');
