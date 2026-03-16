/**
 * Скрипт добавления export перед каждой функцией и создания main.js
 * Запуск: node scripts/wire-modules.mjs
 */
import fs from 'fs';
import path from 'path';

const SRC_DIR = 'src/web';

// Пропускаем _main-init.js (будет встроен в main.js) и globals.js (обработаем отдельно)
const SKIP = ['_main-init.js'];

// 1. Обработаем globals.js — экспортируем переменные и константы
const globalsPath = path.join(SRC_DIR, 'globals.js');
let globalsContent = fs.readFileSync(globalsPath, 'utf-8');

// Добавим export перед let/const объявлениями верхнего уровня
globalsContent = globalsContent
    .replace(/^(let )/gm, 'export $1')
    .replace(/^(const )/gm, 'export $1');

// Добавим export перед function
globalsContent = globalsContent
    .replace(/^function /gm, 'export function ')
    .replace(/^async function /gm, 'export async function ');

fs.writeFileSync(globalsPath, globalsContent, 'utf-8');
console.log('  globals.js: added exports');

// 2. Обработаем все остальные модули
function processModuleFile(filePath) {
    let content = fs.readFileSync(filePath, 'utf-8');

    // Добавляем export перед function объявлениями верхнего уровня (не вложенными)
    // Считаем, что top-level функции начинаются с начала строки
    content = content.replace(/^function /gm, 'export function ');
    content = content.replace(/^async function /gm, 'export async function ');

    // Экспортируем let/const верхнего уровня (не внутри функций)
    // Простая эвристика: строка начинается с let/const
    content = content.replace(/^(let )/gm, 'export $1');
    content = content.replace(/^(const )/gm, 'export $1');

    fs.writeFileSync(filePath, content, 'utf-8');
}

// Рекурсивно находим все .js файлы
function findJsFiles(dir) {
    const files = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const fullPath = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            files.push(...findJsFiles(fullPath));
        } else if (entry.name.endsWith('.js') && !SKIP.includes(entry.name) && entry.name !== 'globals.js' && entry.name !== 'main.js') {
            files.push(fullPath);
        }
    }
    return files;
}

const allModules = findJsFiles(SRC_DIR);
allModules.forEach(f => {
    processModuleFile(f);
    console.log(`  ${f}: added exports`);
});

// 3. Собираем список всех экспортированных функций для main.js
function extractExports(filePath) {
    const content = fs.readFileSync(filePath, 'utf-8');
    const exports = [];
    const funcRegex = /^export (?:async )?function (\w+)/gm;
    let match;
    while ((match = funcRegex.exec(content)) !== null) {
        exports.push(match[1]);
    }
    return exports;
}

// Функции вызываемые из HTML (onclick, data-edit-param, etc.)
// Эти нужно привязать к window.*
const HTML_ONCLICK_FUNCTIONS = [
    // modes
    'confirmModeSwitch', 'startRectification', 'startManual', 'startDistillation',
    'startMashing', 'startHold', 'stopProcess', 'pauseProcess', 'resumeProcess',
    'openRectificationStartModal', 'confirmStartRectification', 'closeRectificationStartModal',
    'updateRectificationFractionsSum', 'applyRectificationFeedstockDefaults',
    // settings
    'saveWiFi', 'saveEquipment', 'saveMqtt', 'saveSecurity',
    'toggleMqttFields', 'toggleAuthFields',
    'toggleDemoMode', 'rebootController',
    // control
    'updateHeater', 'updatePump', 'toggleValve',
    // theme
    'setTheme',
    // logs
    'addLog', 'clearLogs', 'downloadLogs',
    // memory
    'toggleMemoryStats',
    // ui
    'toggleOperatorView', 'toggleTopMenu',
    // runtime edit
    'openRuntimeEditModal', 'closeRuntimeEditModal', 'submitRuntimeEditModal', 'onRuntimeEditUnitToggle',
    // history
    'loadHistoryList', 'applyHistoryFilters', 'clearHistory', 'deleteHistoryItem',
    'viewHistoryDetails', 'closeHistoryModal', 'exportHistory', 'exportHistoryCSV', 'exportHistoryJSON',
    'compareSelected', 'closeCompareModal',
    'toggleProcessSelection',
    // profiles
    'loadProfilesList', 'showCreateProfileModal', 'closeProfileModal', 'saveProfile',
    'viewProfile', 'closeProfileViewModal', 'quickLoadProfile', 'loadProfileToSettings',
    'deleteProfile', 'clearUserProfiles', 'exportProfile', 'exportAllProfiles',
    'showImportModal', 'closeImportModal', 'doImportProfiles',
    // cloud
    'saveCloudConfig', 'generateCloudClaim',
    'toggleUserMenu', 'logout', 'switchAccount',
    'loadESP32Devices', 'loadESP32Device', 'showAddDeviceForm', 'loadESP32Config',
    'toggleESP32Fields', 'saveESP32Device', 'saveESP32Config',
    'activateESP32Device', 'deleteESP32Device', 'testESP32Connection',
    'claimDeviceToAccount',
    // tools
    'calculateAbvCorrection', 'calculateDilution', 'fetchCurrentTempForCalc',
    // mashing
    'addMashStep', 'addHoldStep',
    // abv
    'renderAbvValue',
    // pump/version/sensors
    'loadPumpInfo', 'loadVersionInfo',
];

// Создаем main.js
const mainInitContent = fs.readFileSync(path.join(SRC_DIR, '_main-init.js'), 'utf-8');

// Построим map: функция -> файл модуля
const funcToModule = new Map();
const allFiles = [globalsPath, ...allModules];
allFiles.forEach(f => {
    const exports = extractExports(f);
    const relPath = './' + path.relative(SRC_DIR, f).replace(/\\/g, '/').replace(/\.js$/, '.js');
    exports.forEach(name => funcToModule.set(name, relPath));
});

// Группируем HTML_ONCLICK_FUNCTIONS по модулю
const moduleImports = new Map();
HTML_ONCLICK_FUNCTIONS.forEach(fn => {
    const mod = funcToModule.get(fn);
    if (!mod) {
        console.warn(`  ⚠ Function '${fn}' not found in any module`);
        return;
    }
    if (!moduleImports.has(mod)) moduleImports.set(mod, []);
    moduleImports.get(mod).push(fn);
});

// Генерируем main.js
let mainContent = `// Smart-Column S3 - Web UI (модульная сборка)
// Точка входа — собирается ESBuild в data/app.js

`;

// Сначала импорт всех модулей (side-effects: регистрация event listeners)
const allModulePaths = allFiles.map(f => './' + path.relative(SRC_DIR, f).replace(/\\/g, '/'));

// Импорт с именованными экспортами для window binding
const sortedModules = [...moduleImports.entries()].sort((a, b) => a[0].localeCompare(b[0]));
sortedModules.forEach(([mod, fns]) => {
    mainContent += `import { ${fns.join(', ')} } from '${mod}';\n`;
});

// Также нужны side-effect импорты для модулей, которые регистрируют DOMContentLoaded
// и прочие listener'ы
mainContent += `\n// Side-effect imports (event listeners, DOMContentLoaded handlers)\n`;

// Все модули, которые содержат addEventListener
const sideEffectModules = allModules.filter(f => {
    const content = fs.readFileSync(f, 'utf-8');
    return content.includes('addEventListener') && !moduleImports.has('./' + path.relative(SRC_DIR, f).replace(/\\/g, '/'));
});
sideEffectModules.forEach(f => {
    const rel = './' + path.relative(SRC_DIR, f).replace(/\\/g, '/');
    mainContent += `import '${rel}';\n`;
});

// Нужны импорты для DOMContentLoaded init функций
mainContent += `
// Функции, нужные для инициализации
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
import { loadESP32Devices } from './cloud/devices.js';
import { loadDiscoveredDevices } from './cloud/devices.js';
import { isCloudProxyMode } from './core/cloud-detect.js';

`;

// Window bindings
mainContent += `// ============================================================================\n`;
mainContent += `// Window bindings (для onclick в HTML)\n`;
mainContent += `// ============================================================================\n\n`;

sortedModules.forEach(([mod, fns]) => {
    fns.forEach(fn => {
        mainContent += `window.${fn} = ${fn};\n`;
    });
});

// DOMContentLoaded
mainContent += `
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

    loadTheme();
    loadDemoMode();
    initMiniChart();
    loadMemoryStatsPreference();
    loadPumpInfo();
    loadVersionInfo();
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
`;

fs.writeFileSync(path.join(SRC_DIR, 'main.js'), mainContent, 'utf-8');
console.log(`\n✅ Created main.js with ${HTML_ONCLICK_FUNCTIONS.length} window bindings`);
console.log('Run: npm run build');
