import { addLog } from '../core/logs.js';
import { initEquipmentNumberSteppers } from './number-stepper.js';

const STORAGE_KEY = 'equipment.activeSection';
const SETTINGS_SECTION_STORAGE_KEY = 'settings.activeSection';
const PARAMETERS_CARD_STORAGE_KEY = 'equipment.parameters.activeCard';
const CALIBRATION_CARD_STORAGE_KEY = 'equipment.calibration.activeCard';
const TESTING_CARD_STORAGE_KEY = 'equipment.testing.activeCard';
const TESTING_STATUS_URL = '/api/testing/status';
const TESTING_POLL_MS = 1500;
const PRESSURE_SUCCESS_DELTA = 1.0;
const TESTING_LAYOUT_MEDIA = '(max-width: 900px)';

const TESTING_GROUPS = [
    { id: 'actuators', label: 'Исполнительные узлы' },
    { id: 'sensors', label: 'Датчики и отклик' },
    { id: 'service', label: 'Сервис и питание' },
];

const TESTING_CARD_DEFS = [
    {
        id: 'pump',
        selector: '#equipment-test-pump-toggle',
        group: 'actuators',
        icon: '🧪',
        title: 'Насос',
        shortTitle: 'Насос',
        description: 'Ручной запуск, контроль скорости и быстрый переход к калибровке.',
    },
    {
        id: 'stirrer',
        selector: '#equipment-test-stirrer-start',
        group: 'actuators',
        icon: '🌀',
        title: 'Мешалка куба',
        shortTitle: 'Мешалка',
        description: 'Ручной запуск 0-10 В, оперативная смена скорости и остановка без перехода на главный экран.',
    },
    {
        id: 'valves',
        selector: '#equipment-test-water-toggle',
        group: 'actuators',
        icon: '🚰',
        title: 'Клапаны',
        shortTitle: 'Клапаны',
        description: 'Открытие по одному и короткие импульсы по гидравлическим каналам.',
    },
    {
        id: 'servo',
        selector: '#equipment-test-servo-angle-apply',
        group: 'actuators',
        icon: '🎯',
        title: 'Сервопривод фракционника',
        shortTitle: 'Сервопривод',
        description: 'Быстрые позиции, ручной угол и сохранение сервисных пресетов.',
    },
    {
        id: 'heater',
        selector: '#equipment-test-heater-start',
        group: 'service',
        icon: '⚡',
        title: 'Нагрев',
        shortTitle: 'Нагрев',
        description: 'Основной TRIAC, booster SSR, zero-cross, PZEM и ручной тест в одной силовой панели.',
    },
    {
        id: 'service-overview',
        selector: '#equipment-test-stop-all',
        group: 'service',
        icon: '🛠️',
        title: 'Сервисное тестирование оборудования',
        shortTitle: 'Сервис',
        description: 'Общий сервисный контур, стоп всех тестов, пусконаладка и автосамопроверка железа в одном инженерном окне.',
    },
    {
        id: 'temps',
        selector: '#equipment-test-temps-refresh',
        group: 'sensors',
        icon: '🌡️',
        title: 'Термометры',
        shortTitle: 'Термометры',
        description: 'Текущие показания всех датчиков температуры рядом с сервисными действиями.',
    },
    {
        id: 'pressure',
        selector: '#equipment-test-pressure-start',
        group: 'sensors',
        icon: '💨',
        title: 'Датчик давления',
        shortTitle: 'Давление',
        description: 'Проверка отклика датчика продувкой с подсветкой изменения сигнала.',
    },
    {
        id: 'hydrometer',
        selector: '#equipment-test-hydrometer-badge',
        group: 'sensors',
        icon: '🧫',
        title: 'Ареометр',
        shortTitle: 'Ареометр',
        description: 'Живые данные ареометра и быстрый переход к его калибровочной таблице.',
    },
];

const PARAMETERS_GROUPS = [
    { id: 'core', label: 'Базовые узлы' },
    { id: 'process', label: 'Куб и процесс' },
    { id: 'automation', label: 'Автоматика' },
    { id: 'diagnostics', label: 'Шина и модули' },
];

const PARAMETERS_CARD_DEFS = [
    {
        id: 'pump-settings',
        selector: '#pump-ml-per-rev',
        group: 'core',
        icon: '🧪',
        title: 'Насос и дозирование',
        shortTitle: 'Насос',
        description: 'Шаги, объём на оборот и базовые параметры дозирования для стабильной подачи.',
    },
    {
        id: 'cube-settings',
        selector: '#heater-power-w',
        group: 'process',
        icon: '🥃',
        title: 'Куб, ТЭН и колонна',
        shortTitle: 'Куб и ТЭН',
        description: 'Мощность нагрева, геометрия колонны и рабочий объём куба в одном компактном блоке.',
    },
    {
        id: 'cooling-settings',
        selector: '#water-autostart-cube-temp',
        group: 'process',
        icon: '💧',
        title: 'Охлаждение и автостарт',
        shortTitle: 'Охлаждение',
        description: 'Порог автоматического запуска воды и связанные технологические настройки.',
    },
    {
        id: 'stirrer-settings',
        selector: '#stirrer-enabled',
        group: 'automation',
        icon: '🌀',
        title: 'Мешалка куба',
        shortTitle: 'Мешалка',
        description: 'Ручное включение, скорость по умолчанию и автозапуск для режимов, где перемешивание нужно постоянно.',
    },
    {
        id: 'hardware-status',
        selector: '#hardware-modules-list',
        group: 'diagnostics',
        icon: 'HW',
        title: 'Шина и модули',
        shortTitle: 'Модули',
        description: 'Статусы I2C/UART модулей, адреса и роль каждого датчика или платы в железе.',
    },
];

const CALIBRATION_GROUPS = [
    { id: 'actuators', label: 'Исполнительные узлы' },
    { id: 'sensors', label: 'Датчики' },
];

const SETTINGS_SECTION_DEFS = [
    {
        id: 'connection',
        label: 'Подключение',
        title: 'Подключение',
        subtitle: 'Облако и привязка контроллера без длинной ленты карточек.',
        storageKey: 'settings.connection.activeCard',
        stateKey: 'activeSettingsConnectionCard',
        defs: [
            { id: 'cloud', selector: '#cloud-enabled', group: 'main', icon: '☁️', title: 'Облако', shortTitle: 'Облако', description: 'WSS-туннель и статус привязки устройства.' },
            { id: 'esp32', selector: '#esp32-device-select', group: 'main', icon: '📡', title: 'Подключение к ESP32', shortTitle: 'ESP32', description: 'Привязка устройства, список контроллеров и сетевые параметры.' },
        ],
    },
    {
        id: 'integrations',
        label: 'Интеграции',
        title: 'Интеграции',
        subtitle: 'MQTT и уведомления собраны в один компактный рабочий раздел.',
        storageKey: 'settings.integrations.activeCard',
        stateKey: 'activeSettingsIntegrationsCard',
        defs: [
            { id: 'mqtt', selector: '#mqtt-enabled', group: 'main', icon: '📡', title: 'MQTT', shortTitle: 'MQTT', description: 'Брокер, discovery и публикация состояний.' },
            { id: 'notifications', selector: '#browser-notifications-enabled', group: 'main', icon: '🔔', title: 'Браузерные уведомления', shortTitle: 'Уведомления', description: 'Локальные push-уведомления в браузере.' },
        ],
    },
    {
        id: 'access',
        label: 'Доступ',
        title: 'Доступ и защита',
        subtitle: 'Управление аутентификацией и ограничением запросов.',
        storageKey: 'settings.access.activeCard',
        stateKey: 'activeSettingsAccessCard',
        defs: [
            { id: 'security', selector: '#auth-enabled', group: 'main', icon: '🔒', title: 'Безопасность', shortTitle: 'Безопасность', description: 'Аутентификация и rate limit.' },
        ],
    },
    {
        id: 'interface',
        label: 'Интерфейс',
        title: 'Интерфейс',
        subtitle: 'Тема и визуальное поведение интерфейса без перегруженного экрана.',
        storageKey: 'settings.interface.activeCard',
        stateKey: 'activeSettingsInterfaceCard',
        defs: [
            { id: 'theme', selector: 'button[onclick="setTheme(\'light\')"]', group: 'main', icon: '🎨', title: 'Тема интерфейса', shortTitle: 'Тема', description: 'Переключение цветовой схемы.' },
            { id: 'display', selector: '#show-memory-stats', group: 'main', icon: '🖥️', title: 'Отображение', shortTitle: 'Отображение', description: 'Небольшие системные опции отображения.' },
        ],
    },
    {
        id: 'system',
        label: 'Система',
        title: 'Система',
        subtitle: 'Сервисные системные карточки с быстрым доступом к ключевым действиям.',
        storageKey: 'settings.system.activeCard',
        stateKey: 'activeSettingsSystemCard',
        defs: [
            { id: 'demo', selector: '#demo-mode-enabled', group: 'main', icon: '🧪', title: 'Демо-режим', shortTitle: 'Демо', description: 'Симуляция оборудования без реального железа.' },
            { id: 'reboot', selector: 'button[onclick="rebootController()"]', group: 'main', icon: '🔄', title: 'Перезагрузка', shortTitle: 'Перезагрузка', description: 'Безопасный перезапуск контроллера.' },
            { id: 'versions', selector: '#firmware-version', group: 'main', icon: 'ℹ️', title: 'Информация о версиях', shortTitle: 'Версии', description: 'Backend/frontend build info и плата.' },
        ],
    },
];

const CALIBRATION_CARD_DEFS = [
    {
        id: 'pump-calibration',
        selector: '#cal-speed',
        group: 'actuators',
        icon: '🧪',
        title: 'Калибровка насоса',
        shortTitle: 'Насос',
        description: 'Налив по таре, расчёт времени и обновление коэффициента подачи без лишних переходов.',
    },
    {
        id: 'temp-calibration',
        selector: '#sensorList',
        group: 'sensors',
        icon: '🌡️',
        title: 'Калибровка термометров',
        shortTitle: 'Термометры',
        description: 'Сканирование датчиков, ручная коррекция и быстрый сервисный доступ к каждому термометру.',
    },
    {
        id: 'pressure-calibration',
        selector: '#pressureCurrent',
        group: 'sensors',
        icon: '🎛️',
        title: 'Калибровка манометра',
        shortTitle: 'Манометр',
        description: 'Таблица давления для ADS1115 A1 по эталонному манометру: живое напряжение, ADC и сохранённые точки интерполяции.',
    },
    {
        id: 'hydrometer-calibration',
        selector: '#hydrometerCurrent',
        group: 'sensors',
        icon: '🧪',
        title: 'Калибровка ареометра',
        shortTitle: 'Ареометр',
        description: 'Таблица ABV по сигналу попугая, текущее показание и сервисная подстройка без JSON-ручек.',
    },
    {
        id: 'calibration-backup',
        selector: '#calibration-import-preview',
        group: 'sensors',
        icon: '🗂️',
        title: 'Резерв и восстановление',
        shortTitle: 'Snapshot',
        description: 'Экспорт и импорт полного calibration snapshot для переноса или быстрого отката.',
    },
];

const state = {
    activeSection: 'parameters',
    activeParameterCard: 'pump-settings',
    activeCalibrationCard: 'pump-calibration',
    activeTestingCard: 'pump',
    pollingHandle: null,
    lastStatus: null,
    pendingStirrerSpeed: null,
    heaterPendingPower: 0,
    pressureTest: {
        active: false,
        baseline: null,
        min: null,
        max: null,
        success: false
    },
    commissioning: {
        manual: {
            pump: false,
            valves: false,
            heater: false
        }
    },
    selfCheckHistory: [],
    selfCheckLastSignature: '',
    selfCheckLastRecordedAt: 0,
    activeSettingsSection: 'connection'
};

const paneWorkbenchControllers = new Map();
let testingWorkbenchController = null;

const TESTING_TEMPLATE = `
    <div class="equipment-testing-stack">
        <div class="card equipment-card equipment-test-status-card">
            <div class="equipment-test-status-head">
                <div>
                    <h2>Сервисное тестирование оборудования</h2>
                    <p class="equipment-subtitle">Каждый физический узел вынесен в отдельную группу. Сначала проверь блокировки и только потом запускай исполнительные тесты.</p>
                </div>
                <button class="btn btn-danger" type="button" id="equipment-test-stop-all">Остановить все тесты</button>
            </div>
            <div class="equipment-test-chip-row">
                <span class="equipment-status-badge muted" id="equipment-test-allow-badge">Загрузка…</span>
                <span class="equipment-status-badge muted" id="equipment-test-demo-badge">—</span>
                <span class="equipment-status-badge muted" id="equipment-test-process-badge">—</span>
                <span class="equipment-status-badge muted" id="equipment-test-alarm-badge">—</span>
            </div>
            <div class="equipment-inline-stats equipment-test-summary-stats" id="equipment-test-active-summary"></div>
            <div class="equipment-test-alert" id="equipment-test-availability-hint">Загрузка сервисного статуса…</div>
            <div class="equipment-service-contour-card">
                <div class="equipment-service-contour-head">
                    <div>
                        <h3>Сервисный контур</h3>
                        <p class="equipment-hint">Быстрый снимок по ключевым узлам перед ручными тестами: термошина, АЦП, барометрия, кубовый манометр, zero-cross и силовой монитор.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-service-contour-badge">Проверяем…</span>
                </div>
                <div class="equipment-test-alert subtle" id="equipment-service-contour-hint">Ожидаем данные по сервисному контуру…</div>
                <div class="equipment-service-contour-grid" id="equipment-service-contour-grid">
                    <div class="equipment-test-journal-empty">Контур появится после загрузки сервисного статуса.</div>
                </div>
            </div>
            <div class="equipment-commissioning-card">
                <div class="equipment-commissioning-head">
                    <div>
                        <h3>Мастер пусконаладки</h3>
                        <p class="equipment-hint">Короткий чек-лист по текущему железу: что уже подтверждено, а что ещё нужно руками добить.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-commissioning-overall-badge">Проверяем…</span>
                </div>
                <div class="equipment-inline-stats equipment-commissioning-summary" id="equipment-commissioning-summary"></div>
                <div class="equipment-test-alert subtle" id="equipment-commissioning-next-step">Ожидаем сервисный статус…</div>
                <div class="equipment-commissioning-steps" id="equipment-commissioning-steps">
                    <div class="equipment-test-journal-empty">Чек-лист появится после загрузки статуса.</div>
                </div>
            </div>
            <div class="equipment-selfcheck-card">
                <div class="equipment-selfcheck-head">
                    <div>
                        <h3>Автосамопроверка железа</h3>
                        <p class="equipment-hint">Короткий снимок по health и обязательным модулям: что контроллер реально видит прямо сейчас.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-selfcheck-badge">Проверяем…</span>
                </div>
                <div class="equipment-inline-stats equipment-selfcheck-summary" id="equipment-selfcheck-summary"></div>
                <div class="equipment-test-alert subtle" id="equipment-selfcheck-hint">Ожидаем данные по модулям и health…</div>
                <div class="equipment-selfcheck-actions">
                    <button class="btn btn-sm btn-secondary" type="button" id="equipment-selfcheck-open-modules">Шина и модули</button>
                    <button class="btn btn-sm btn-secondary" type="button" id="equipment-selfcheck-open-temps">Термометры</button>
                </div>
                <div class="equipment-selfcheck-modules" id="equipment-selfcheck-modules">
                    <div class="equipment-test-journal-empty">Снимок модулей появится после загрузки статуса.</div>
                </div>
                <div class="equipment-selfcheck-history">
                    <div class="equipment-test-journal-head">
                        <h3>Недавние изменения precheck</h3>
                        <span class="equipment-hint">Локальная история сервиса, пока страница открыта</span>
                    </div>
                    <div class="equipment-test-journal-list" id="equipment-selfcheck-history">
                        <div class="equipment-test-journal-empty">История появится после первых изменений состояния.</div>
                    </div>
                </div>
            </div>
            <div class="equipment-test-journal">
                <div class="equipment-test-journal-head">
                    <h3>Последние действия оператора</h3>
                    <span class="equipment-hint">Последние ручные команды из сервисного экрана</span>
                </div>
                <div class="equipment-test-journal-list" id="equipment-test-action-journal">
                    <div class="equipment-test-journal-empty">Сервисных действий пока не было.</div>
                </div>
            </div>
        </div>

        <div class="equipment-testing-grid">
            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Насос</h2>
                        <p class="equipment-subtitle">Ручной запуск, контроль скорости, объёма и быстрый переход к калибровке.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-pump-badge">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Цель</span><strong id="equipment-test-pump-target">--</strong></div>
                    <div class="equipment-test-metric"><span>Факт</span><strong id="equipment-test-pump-applied">--</strong></div>
                    <div class="equipment-test-metric"><span>Объём</span><strong id="equipment-test-pump-volume">--</strong></div>
                </div>
                <div class="form-group">
                    <label for="equipment-test-pump-speed">Скорость насоса, мл/ч</label>
                    <input type="number" id="equipment-test-pump-speed" value="1200" min="1" max="5000" step="50" data-stepper-mode="pair" data-stepper-step="50">
                </div>
                <div class="equipment-quick-actions">
                    <button type="button" class="btn btn-sm btn-secondary" data-pump-speed-preset="300">300</button>
                    <button type="button" class="btn btn-sm btn-secondary" data-pump-speed-preset="800">800</button>
                    <button type="button" class="btn btn-sm btn-secondary" data-pump-speed-preset="1500">1500</button>
                    <button type="button" class="btn btn-sm btn-secondary" data-pump-speed-preset="3000">3000</button>
                    <button type="button" class="btn btn-sm btn-secondary" data-pump-speed-preset="5000">5000</button>
                </div>
                <div class="equipment-inline-stats">
                    <div class="equipment-inline-stat"><span>Задача</span><strong id="equipment-test-pump-task">--</strong></div>
                    <div class="equipment-inline-stat"><span>Циклы</span><strong id="equipment-test-pump-loops">--</strong></div>
                    <div class="equipment-inline-stat"><span>Таймауты</span><strong id="equipment-test-pump-locks">--</strong></div>
                    <div class="equipment-actuator-row">
                        <div>
                            <strong>PWM охлаждения</strong>
                            <div class="info-text">Ручная подача duty на пропорциональный канал охлаждения</div>
                            <div class="equipment-actuator-meta" id="equipment-test-start-stop-hint">Настройте рабочее окно в параметрах оборудования, затем проверьте duty здесь.</div>
                        </div>
                        <span class="equipment-status-badge muted" id="equipment-test-start-stop-badge">—</span>
                        <div class="equipment-inline-row">
                            <input type="number" id="equipment-test-start-stop-duty" value="96" min="0" max="255" step="1" data-stepper-mode="pair" data-stepper-step="1">
                            <button class="btn btn-secondary" type="button" id="equipment-test-start-stop-apply">Применить</button>
                            <button class="btn btn-outline-secondary" type="button" id="equipment-test-start-stop-startup">Стартовое</button>
                            <button class="btn btn-danger" type="button" id="equipment-test-start-stop-stop">0</button>
                        </div>
                    </div>
                </div>
                <div class="controls equipment-actions">
                    <button class="btn btn-success" type="button" id="equipment-test-pump-toggle">Запустить насос</button>
                    <button class="btn btn-secondary" type="button" id="equipment-test-pump-open-calibration">К калибровке</button>
                </div>
            </div>

            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Мешалка куба</h2>
                        <p class="equipment-subtitle">Ручной тест выхода 0-10 В, смена скорости на лету и быстрый переход в режим ручного управления.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-stirrer-badge">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Скорость</span><strong id="equipment-test-stirrer-speed-live">--</strong></div>
                    <div class="equipment-test-metric"><span>Режим</span><strong id="equipment-test-stirrer-mode">--</strong></div>
                    <div class="equipment-test-metric"><span>DAC</span><strong id="equipment-test-stirrer-available">--</strong></div>
                </div>
                <div class="form-group">
                    <label for="equipment-test-stirrer-speed">Скорость мешалки, %</label>
                    <input type="number" id="equipment-test-stirrer-speed" value="50" min="1" max="100" step="1" data-stepper-mode="pair" data-stepper-step="1">
                    <small class="equipment-hint" id="equipment-test-stirrer-hint">Загрузка статуса мешалки…</small>
                </div>
                <div class="equipment-quick-actions">
                    <button type="button" class="btn btn-sm btn-secondary" data-stirrer-speed-preset="25">25%</button>
                    <button type="button" class="btn btn-sm btn-secondary" data-stirrer-speed-preset="50">50%</button>
                    <button type="button" class="btn btn-sm btn-secondary" data-stirrer-speed-preset="75">75%</button>
                    <button type="button" class="btn btn-sm btn-secondary" data-stirrer-speed-preset="100">100%</button>
                </div>
                <div class="controls equipment-actions">
                    <button class="btn btn-success" type="button" id="equipment-test-stirrer-start">Запустить мешалку</button>
                    <button class="btn btn-secondary" type="button" id="equipment-test-stirrer-apply">Применить скорость</button>
                    <button class="btn btn-danger" type="button" id="equipment-test-stirrer-stop">Стоп</button>
                </div>
            </div>

            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Клапаны</h2>
                        <p class="equipment-subtitle">Открытие по одному, импульс на заданное время и быстрый общий сброс.</p>
                    </div>
                </div>
                <div class="form-group">
                    <label for="equipment-test-valve-pulse-duration">Длительность импульса, мс</label>
                    <input type="number" id="equipment-test-valve-pulse-duration" value="1200" min="100" max="10000" step="100" data-stepper-mode="pair" data-stepper-step="100">
                </div>
                <div class="equipment-actuator-list">
                    <div class="equipment-actuator-row">
                        <div>
                            <strong>Вода</strong>
                            <div class="info-text">Подача охлаждения</div>
                            <div class="equipment-actuator-meta" id="equipment-test-water-toggle-pulse-hint">Импульсный тест готов.</div>
                        </div>
                        <span class="equipment-status-badge muted" id="equipment-test-water-toggle-badge">—</span>
                        <div class="equipment-actuator-actions">
                            <button class="btn btn-secondary" type="button" id="equipment-test-water-toggle">Открыть воду</button>
                            <button class="btn btn-outline-secondary" type="button" id="equipment-test-water-pulse">Импульс</button>
                        </div>
                    </div>
                    <div class="equipment-actuator-row">
                        <div>
                            <strong>Головы</strong>
                            <div class="info-text">Отбор голов</div>
                            <div class="equipment-actuator-meta" id="equipment-test-heads-toggle-pulse-hint">Импульсный тест готов.</div>
                        </div>
                        <span class="equipment-status-badge muted" id="equipment-test-heads-toggle-badge">—</span>
                        <div class="equipment-actuator-actions">
                            <button class="btn btn-secondary" type="button" id="equipment-test-heads-toggle">Открыть головы</button>
                            <button class="btn btn-outline-secondary" type="button" id="equipment-test-heads-pulse">Импульс</button>
                        </div>
                    </div>
                    <div class="equipment-actuator-row">
                        <div>
                            <strong>УНО</strong>
                            <div class="info-text">Непрерывный отбор</div>
                            <div class="equipment-actuator-meta" id="equipment-test-uno-toggle-pulse-hint">Импульсный тест готов.</div>
                        </div>
                        <span class="equipment-status-badge muted" id="equipment-test-uno-toggle-badge">—</span>
                        <div class="equipment-actuator-actions">
                            <button class="btn btn-secondary" type="button" id="equipment-test-uno-toggle">Открыть УНО</button>
                            <button class="btn btn-outline-secondary" type="button" id="equipment-test-uno-pulse">Импульс</button>
                        </div>
                    </div>
                </div>
                <div class="controls equipment-actions">
                    <button class="btn btn-danger" type="button" id="equipment-test-valves-close-all">Закрыть все клапаны</button>
                </div>
            </div>

            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Сервопривод фракционника</h2>
                        <p class="equipment-subtitle">Быстрые позиции, ручной угол и сохранение сервисных пресетов.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-servo-badge">—</span>
                </div>
                <div class="equipment-inline-stats">
                    <div class="equipment-inline-stat"><span>Позиция</span><strong id="equipment-test-servo-fraction">—</strong></div>
                    <div class="equipment-inline-stat"><span>Угол</span><strong id="equipment-test-servo-angle-live">—</strong></div>
                </div>
                <div class="equipment-test-alert subtle" id="equipment-test-servo-status">Статус сервопривода будет показан после загрузки.</div>
                <div class="equipment-preset-grid">
                    <button type="button" class="equipment-preset-button" data-servo-preset="heads"><span>Головы</span><small class="equipment-preset-angle">0°</small></button>
                    <button type="button" class="equipment-preset-button" data-servo-preset="subheads"><span>Подголовники</span><small class="equipment-preset-angle">0°</small></button>
                    <button type="button" class="equipment-preset-button" data-servo-preset="body"><span>Тело</span><small class="equipment-preset-angle">0°</small></button>
                    <button type="button" class="equipment-preset-button" data-servo-preset="pretails"><span>Предхвостье</span><small class="equipment-preset-angle">0°</small></button>
                    <button type="button" class="equipment-preset-button" data-servo-preset="tails"><span>Хвосты</span><small class="equipment-preset-angle">0°</small></button>
                </div>
                <div class="equipment-inline-row equipment-servo-manual-row">
                    <input type="number" id="equipment-test-servo-angle" min="0" max="180" step="1" value="0" data-stepper-mode="pair" data-stepper-step="1">
                    <button class="btn btn-secondary" type="button" id="equipment-test-servo-angle-apply">Перевести в угол</button>
                </div>
                <div class="equipment-servo-config-grid">
                    <div class="equipment-servo-config-row">
                        <span>Головы</span>
                        <label><input type="checkbox" id="equipment-servo-enabled-0" checked> Активно</label>
                        <input type="number" id="equipment-servo-angle-0" min="0" max="180" step="1" value="0" data-stepper-mode="pair" data-stepper-step="1">
                    </div>
                    <div class="equipment-servo-config-row">
                        <span>Подголовники</span>
                        <label><input type="checkbox" id="equipment-servo-enabled-1"> Активно</label>
                        <input type="number" id="equipment-servo-angle-1" min="0" max="180" step="1" value="36" data-stepper-mode="pair" data-stepper-step="1">
                    </div>
                    <div class="equipment-servo-config-row">
                        <span>Тело</span>
                        <label><input type="checkbox" id="equipment-servo-enabled-2" checked> Активно</label>
                        <input type="number" id="equipment-servo-angle-2" min="0" max="180" step="1" value="72" data-stepper-mode="pair" data-stepper-step="1">
                    </div>
                    <div class="equipment-servo-config-row">
                        <span>Предхвостье</span>
                        <label><input type="checkbox" id="equipment-servo-enabled-3"> Активно</label>
                        <input type="number" id="equipment-servo-angle-3" min="0" max="180" step="1" value="108" data-stepper-mode="pair" data-stepper-step="1">
                    </div>
                    <div class="equipment-servo-config-row">
                        <span>Хвосты</span>
                        <label><input type="checkbox" id="equipment-servo-enabled-4" checked> Активно</label>
                        <input type="number" id="equipment-servo-angle-4" min="0" max="180" step="1" value="144" data-stepper-mode="pair" data-stepper-step="1">
                    </div>
                </div>
                <div class="controls equipment-actions">
                    <button class="btn btn-primary" type="button" id="equipment-test-servo-save">Сохранить позиции</button>
                </div>
            </div>

            <div class="card equipment-card equipment-test-card equipment-test-card-danger">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Нагрев</h2>
                        <p class="equipment-subtitle">Полная силовая панель: основной TRIAC, booster SSR, синхронизация zero-cross, PZEM и ручной тест нагрева.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-heater-badge">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Текущая мощность</span><strong id="equipment-test-heater-power">--</strong></div>
                    <div class="equipment-test-metric"><span>Сетпоинт</span><strong id="equipment-test-heater-setpoint">--</strong></div>
                    <div class="equipment-test-metric"><span>Мин. погружение</span><strong id="equipment-test-heater-submerge">--</strong></div>
                    <div class="equipment-test-metric"><span>Основной ТЭН</span><strong id="equipment-test-heater-main-power">--</strong></div>
                    <div class="equipment-test-metric"><span>Booster SSR</span><strong id="equipment-test-heater-booster-power">--</strong></div>
                    <div class="equipment-test-metric"><span>Стоп booster</span><strong id="equipment-test-heater-booster-stop">--</strong></div>
                </div>
                <div class="equipment-test-chip-row">
                    <span class="equipment-status-badge muted" id="equipment-test-heater-backend">—</span>
                    <span class="equipment-status-badge muted" id="equipment-test-heater-booster">—</span>
                    <span class="equipment-status-badge muted" id="equipment-test-heater-zc">—</span>
                    <span class="equipment-status-badge muted" id="equipment-test-heater-pzem">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Фазовая задержка</span><strong id="equipment-test-heater-delay">--</strong></div>
                    <div class="equipment-test-metric"><span>Zero-cross</span><strong id="equipment-test-heater-zc-count">--</strong></div>
                    <div class="equipment-test-metric"><span>Gate pulses</span><strong id="equipment-test-heater-fire-count">--</strong></div>
                    <div class="equipment-test-metric"><span>PZEM мощность</span><strong id="equipment-test-heater-real-power">--</strong></div>
                    <div class="equipment-test-metric"><span>Напряжение</span><strong id="equipment-test-heater-voltage">--</strong></div>
                    <div class="equipment-test-metric"><span>Ток</span><strong id="equipment-test-heater-current">--</strong></div>
                    <div class="equipment-test-metric"><span>Частота</span><strong id="equipment-test-heater-frequency">--</strong></div>
                    <div class="equipment-test-metric"><span>cos φ</span><strong id="equipment-test-heater-pf">--</strong></div>
                </div>
                <div class="form-group">
                    <label for="equipment-test-heater-power-input">Мощность теста, %</label>
                    <input type="number" id="equipment-test-heater-power-input" value="40" min="1" max="100" step="1" data-stepper-mode="pair" data-stepper-step="1">
                </div>
                <div class="equipment-test-alert subtle" id="equipment-test-heater-diag">Диагностика контура нагрева появится после загрузки статуса.</div>
                <div class="equipment-test-alert subtle">Памятка по монтажу: для phase-control используйте MOC3021/MOC3023; G силового симистора должен идти в ветку A2, а не A1.</div>
                <div class="equipment-test-alert danger">Перед стартом ТЭН должен быть полностью погружен в жидкость. Без подтверждения запуск не выполняется.</div>
                <div class="controls equipment-actions">
                    <button class="btn btn-danger" type="button" id="equipment-test-heater-start">Запустить нагрев</button>
                    <button class="btn btn-secondary" type="button" id="equipment-test-heater-stop">Остановить нагрев</button>
                </div>
            </div>

            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Термометры</h2>
                        <p class="equipment-subtitle">Текущие показания всех датчиков температуры рядом с быстрыми сервисными действиями.</p>
                    </div>
                </div>
                <div class="controls equipment-actions">
                    <button class="btn btn-secondary" type="button" id="equipment-test-temps-refresh">Обновить</button>
                    <button class="btn btn-secondary" type="button" id="equipment-test-temps-open-calibration">К калибровке</button>
                </div>
                <div class="equipment-test-sensors-grid" id="equipment-testing-temps-list">
                    <div class="equipment-test-sensor">Загрузка…</div>
                </div>
            </div>

            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Датчик давления</h2>
                        <p class="equipment-subtitle">Попроси оператора слегка подуть в датчик. При заметном изменении давления карточка подтвердит отклик.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-pressure-badge">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Куб</span><strong id="equipment-test-pressure-value">--</strong></div>
                </div>
                <div class="equipment-inline-stats" id="equipment-test-pressure-summary"></div>
                <div class="equipment-test-alert subtle" id="equipment-test-pressure-hint">Для теста нажмите кнопку ниже.</div>
                <div class="controls equipment-actions">
                    <button class="btn btn-secondary" type="button" id="equipment-test-pressure-start">Начать тест продувки</button>
                </div>
            </div>

            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Ареометр</h2>
                        <p class="equipment-subtitle">Живые показания ареометра и быстрый переход к таблице его калибровки.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-hydrometer-badge">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Давление</span><strong id="equipment-test-hydrometer-pressure">--</strong></div>
                    <div class="equipment-test-metric"><span>Плотность</span><strong id="equipment-test-hydrometer-density">--</strong></div>
                    <div class="equipment-test-metric"><span>ABV</span><strong id="equipment-test-hydrometer-abv">--</strong></div>
                </div>
                <div class="equipment-test-alert subtle">Если показания плавают или не сходятся с реальным продуктом, открой таблицу калибровки и задай 2-5 опорных точек.</div>
                <div class="controls equipment-actions">
                    <button class="btn btn-secondary" type="button" id="equipment-test-hydrometer-open-calibration">К калибровке</button>
                </div>
            </div>

        </div>
    </div>
`;

const HEATER_MODAL_TEMPLATE = `
    <div id="equipment-heater-confirm-modal" class="modal-overlay">
        <div class="modal-content" style="max-width: 520px;">
            <div class="modal-header">
                <div class="modal-title">Подтверждение запуска ТЭНа</div>
                <button class="modal-close" type="button" id="equipment-test-heater-close">&times;</button>
            </div>
            <div class="modal-body">
                <div class="modal-section">
                    <div class="modal-section-title">Опасное действие</div>
                    <p class="equipment-subtitle" style="margin-bottom: 0;">Перед включением убедись, что ТЭН полностью погружен в воду или жидкость, а силовой канал подключен корректно.</p>
                </div>
                <div class="equipment-test-alert danger">
                    Будет отправлена команда на <strong id="equipment-test-heater-confirm-power">0%</strong> мощности.
                </div>
            </div>
            <div class="modal-footer">
                <button class="btn" type="button" id="equipment-test-heater-cancel">Отмена</button>
                <button class="btn btn-danger" type="button" id="equipment-test-heater-confirm">Подтверждаю запуск</button>
            </div>
        </div>
    </div>
`;

function byId(id) {
    return document.getElementById(id);
}

function qs(selector, root = document) {
    return root.querySelector(selector);
}

function qsa(selector, root = document) {
    return [...root.querySelectorAll(selector)];
}

function createElement(tag, className, textContent) {
    const element = document.createElement(tag);
    if (className) {
        element.className = className;
    }
    if (typeof textContent === 'string') {
        element.textContent = textContent;
    }
    return element;
}

function toNumber(value, fallback = 0) {
    const parsed = Number(String(value ?? '').trim().replace(',', '.'));
    return Number.isFinite(parsed) ? parsed : fallback;
}

function clamp(value, min, max, fallback = min) {
    const parsed = toNumber(value, fallback);
    if (parsed < min) return min;
    if (parsed > max) return max;
    return parsed;
}

function setText(id, value) {
    const el = byId(id);
    if (el) {
        el.textContent = value;
    }
}

function setHtml(id, value) {
    const el = byId(id);
    if (el) {
        el.innerHTML = value;
    }
}

function updateBadge(el, label, tone = 'neutral') {
    if (!el) return;
    el.textContent = label;
    el.className = `equipment-status-badge ${tone}`;
}

function formatBool(value, yes = 'Да', no = 'Нет') {
    return value ? yes : no;
}

function formatNumber(value, digits = 1, suffix = '') {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return `--${suffix}`;
    return `${parsed.toFixed(digits)}${suffix}`;
}

function formatSensorValue(sensor) {
    if (!sensor?.valid) return 'Нет сигнала';
    return formatNumber(sensor.value, 2, ' °C');
}

function getTestingGroupLabel(groupId) {
    return TESTING_GROUPS.find((group) => group.id === groupId)?.label ?? 'Тестирование';
}

function getWorkbenchGroupLabel(groups, groupId, fallback = 'Раздел оборудования') {
    return groups.find((group) => group.id === groupId)?.label ?? fallback;
}

function buildTestingMobileToggle(meta) {
    const button = createElement('button', 'equipment-test-mobile-toggle');
    button.type = 'button';

    const icon = createElement('span', 'equipment-test-mobile-icon', meta.icon);
    icon.setAttribute('aria-hidden', 'true');

    const copy = createElement('span', 'equipment-test-mobile-copy');
    copy.append(
        createElement('span', 'equipment-test-mobile-title', meta.shortTitle),
        createElement('span', 'equipment-test-mobile-description', meta.description),
    );

    const chevron = createElement('span', 'equipment-test-mobile-chevron', '▾');
    chevron.setAttribute('aria-hidden', 'true');

    button.append(icon, copy, chevron);
    return button;
}

function buildWorkbenchMobileToggle(meta) {
    return buildTestingMobileToggle(meta);
}

function buildTestingDesktopNav(cards) {
    const nav = createElement('aside', 'equipment-testing-sidebar');
    nav.setAttribute('aria-label', 'Навигация по сервисным тестам');

    const navHeader = createElement('div', 'equipment-testing-sidebar-header');
    navHeader.append(
        createElement('div', 'equipment-testing-sidebar-title', 'Тестирование'),
        createElement('div', 'equipment-testing-sidebar-subtitle', 'Открыт один сервисный узел, остальные доступны через компактное меню.'),
    );
    nav.appendChild(navHeader);

    const buttonsById = new Map();

    for (const group of TESTING_GROUPS) {
        const groupCards = group.id
            ? cards.filter((card) => card.meta.group === group.id)
            : [];

        if (!groupCards.length) continue;

        nav.appendChild(createElement('div', 'sidebar-section-title', group.label));

        for (const card of groupCards) {
            const button = createElement('button', 'sidebar-item equipment-testing-nav-item');
            button.type = 'button';
            button.dataset.testingCardId = card.meta.id;
            button.append(
                createElement('span', 'icon', card.meta.icon),
                createElement('span', 'label', card.meta.shortTitle),
            );
            nav.appendChild(button);
            buttonsById.set(card.meta.id, button);
        }
    }

    return { nav, buttonsById };
}

function buildWorkbenchDesktopNav(cards, groups, title, subtitle) {
    const nav = createElement('aside', 'equipment-testing-sidebar');
    nav.setAttribute('aria-label', `${title}: навигация по карточкам`);

    const navHeader = createElement('div', 'equipment-testing-sidebar-header');
    navHeader.append(
        createElement('div', 'equipment-testing-sidebar-title', title),
        createElement('div', 'equipment-testing-sidebar-subtitle', subtitle),
    );
    nav.appendChild(navHeader);

    const buttonsById = new Map();

    for (const group of groups) {
        const groupCards = cards.filter((card) => card.meta.group === group.id);
        if (!groupCards.length) continue;

        if (group.label) {
            nav.appendChild(createElement('div', 'sidebar-section-title', group.label));
        }

        for (const card of groupCards) {
            const button = createElement('button', 'sidebar-item equipment-testing-nav-item');
            button.type = 'button';
            button.dataset.equipmentWorkbenchCardId = card.meta.id;
            button.append(
                createElement('span', 'icon', card.meta.icon),
                createElement('span', 'label', card.meta.shortTitle),
            );
            nav.appendChild(button);
            buttonsById.set(card.meta.id, button);
        }
    }

    return { nav, buttonsById };
}

function enhanceTestingCard(card, meta) {
    if (card.dataset.testingEnhanced === '1') {
        return {
            card,
            meta,
            body: card.querySelector('.equipment-test-card-body'),
            toggle: card.querySelector('.equipment-test-mobile-toggle'),
        };
    }

    card.dataset.testingEnhanced = '1';
    card.dataset.testingCardId = meta.id;
    card.dataset.testingCardGroup = meta.group;

    const children = [...card.childNodes];
    const body = createElement('div', 'equipment-test-card-body');
    for (const child of children) {
        body.appendChild(child);
    }

    const titleEl = body.querySelector('h2');
    if (titleEl) {
        titleEl.classList.add('equipment-test-card-title');
        titleEl.textContent = '';

        const titleIcon = createElement('span', 'equipment-test-card-title-icon', meta.icon);
        titleIcon.setAttribute('aria-hidden', 'true');
        const titleText = createElement('span', 'equipment-test-card-title-text', meta.title);
        titleEl.append(titleIcon, titleText);

        const groupBadge = createElement('div', 'equipment-test-card-group-badge', getTestingGroupLabel(meta.group));
        titleEl.before(groupBadge);
    }

    const descriptionEl = body.querySelector('.equipment-subtitle');
    if (descriptionEl) {
        descriptionEl.textContent = meta.description;
        descriptionEl.classList.add('equipment-test-card-description');
    }

    const toggle = buildTestingMobileToggle(meta);
    card.textContent = '';
    card.append(toggle, body);

    return { card, meta, body, toggle };
}

function enhanceWorkbenchCard(card, meta, groups) {
    if (card.dataset.equipmentWorkbenchEnhanced === '1') {
        return {
            card,
            meta,
            body: card.querySelector('.equipment-test-card-body'),
            toggle: card.querySelector('.equipment-test-mobile-toggle'),
        };
    }

    card.dataset.equipmentWorkbenchEnhanced = '1';
    card.dataset.equipmentWorkbenchCardId = meta.id;
    card.dataset.equipmentWorkbenchGroup = meta.group;
    card.classList.add('equipment-test-card');

    const children = [...card.childNodes];
    const body = createElement('div', 'equipment-test-card-body');
    for (const child of children) {
        body.appendChild(child);
    }

    const titleEl = body.querySelector('h2');
    if (titleEl) {
        titleEl.classList.add('equipment-test-card-title');
        titleEl.textContent = '';

        const titleIcon = createElement('span', 'equipment-test-card-title-icon', meta.icon);
        titleIcon.setAttribute('aria-hidden', 'true');
        const titleText = createElement('span', 'equipment-test-card-title-text', meta.title);
        titleEl.append(titleIcon, titleText);

        const groupBadge = createElement(
            'div',
            'equipment-test-card-group-badge',
            getWorkbenchGroupLabel(groups, meta.group),
        );
        titleEl.before(groupBadge);
    }

    const descriptionEl = body.querySelector('.equipment-subtitle');
    if (descriptionEl) {
        descriptionEl.textContent = meta.description;
        descriptionEl.classList.add('equipment-test-card-description');
    }

    const toggle = buildWorkbenchMobileToggle(meta);
    card.textContent = '';
    card.append(toggle, body);

    return { card, meta, body, toggle };
}

function resolveTestingCards(cards) {
    const resolved = [];

    for (const meta of TESTING_CARD_DEFS) {
        const card = cards.find((candidate) => candidate.querySelector(meta.selector));
        if (!card) continue;
        resolved.push(enhanceTestingCard(card, meta));
    }

    return resolved;
}

function resolveWorkbenchCards(cards, defs, groups) {
    const resolved = [];

    for (const meta of defs) {
        const card = cards.find((candidate) => candidate.querySelector(meta.selector));
        if (!card) continue;
        resolved.push(enhanceWorkbenchCard(card, meta, groups));
    }

    return resolved;
}

function buildEquipmentCard({ className = '', title, subtitle, actionsHtml = '' }) {
    const card = document.createElement('div');
    card.className = `card equipment-card ${className}`.trim();
    card.innerHTML = `
        <h2>${title}</h2>
        <p class="equipment-subtitle">${subtitle}</p>
        <div class="equipment-grid"></div>
        ${actionsHtml}
    `;
    return card;
}

function findGroupByInputId(groups, inputId) {
    return groups.find((group) => group.querySelector(`#${inputId}`)) ?? null;
}

function appendUniqueGroups(target, groups, inputIds) {
    const used = new Set();
    for (const inputId of inputIds) {
        const group = findGroupByInputId(groups, inputId);
        if (!group || used.has(group)) continue;
        used.add(group);
        target.appendChild(group);
    }
}

function ensureParameterWorkbenchCards() {
    const cardsHost = qs('[data-equipment-section-pane="parameters"] .cards');
    const sourceCard = qs('.equipment-card-params', cardsHost);
    if (!cardsHost || !sourceCard || cardsHost.dataset.parametersPrepared === '1') return;

    const sourceGrid = qs('.equipment-grid', sourceCard);
    if (!sourceGrid) return;

    cardsHost.dataset.parametersPrepared = '1';

    const formGroups = [...sourceGrid.querySelectorAll('.form-group')];

    const pumpCard = buildEquipmentCard({
        className: 'equipment-pane-card equipment-pane-card-parameters',
        title: 'Насос и дозирование',
        subtitle: 'Коэффициенты подачи и шаги на оборот, которые влияют на точность насоса во всех режимах.',
        actionsHtml: `
            <div class="controls equipment-actions">
                <button class="btn btn-primary" type="button" onclick="saveEquipment()">Сохранить параметры</button>
            </div>
        `,
    });
    appendUniqueGroups(qs('.equipment-grid', pumpCard), formGroups, [
        'pump-ml-per-rev',
        'pump-steps-per-rev',
    ]);

    const cubeCard = buildEquipmentCard({
        className: 'equipment-pane-card equipment-pane-card-parameters',
        title: 'Куб, ТЭН и колонна',
        subtitle: 'Габариты, мощность и рабочий объём, от которых зависит поведение нагрева и ограничения процесса.',
        actionsHtml: `
            <div class="controls equipment-actions">
                <button class="btn btn-primary" type="button" onclick="saveEquipment()">Сохранить параметры</button>
            </div>
        `,
    });
    appendUniqueGroups(qs('.equipment-grid', cubeCard), formGroups, [
        'heater-power-w',
        'booster-heater-enabled',
        'booster-heater-power-w',
        'booster-heater-stop-cube-temp',
        'column-height',
        'cube-volume-l',
        'cube-extender-add-l',
    ]);

    const coolingCard = buildEquipmentCard({
        className: 'equipment-pane-card equipment-pane-card-parameters',
        title: 'Охлаждение и автостарт',
        subtitle: 'Порог автоматического запуска воды и сервисные настройки, которые оператор обычно ищет перед запуском.',
        actionsHtml: `
            <div class="controls equipment-actions">
                <button class="btn btn-primary" type="button" onclick="saveEquipment()">Сохранить параметры</button>
            </div>
        `,
    });
    appendUniqueGroups(qs('.equipment-grid', coolingCard), formGroups, [
        'water-autostart-cube-temp',
        'cooling-pwm-enabled',
        'cooling-pwm-min-duty',
        'cooling-pwm-max-duty',
        'cooling-pwm-startup-duty',
    ]);

    const stirrerCard = buildEquipmentCard({
        className: 'equipment-pane-card equipment-pane-card-parameters',
        title: 'Мешалка куба',
        subtitle: 'Включение, скорость по умолчанию и автозапуск для затирки, НБК и ферментации.',
        actionsHtml: `
            <div class="controls equipment-actions">
                <button class="btn btn-primary" type="button" onclick="saveStirrerSettings()">Сохранить мешалку</button>
            </div>
        `,
    });
    appendUniqueGroups(qs('.equipment-grid', stirrerCard), formGroups, [
        'stirrer-settings-state',
        'stirrer-enabled',
        'stirrer-default-speed',
        'stirrer-auto-mashing',
        'stirrer-auto-nbk',
        'stirrer-auto-fermentation',
    ]);

    const hardwareCard = buildEquipmentCard({
        className: 'equipment-pane-card equipment-pane-card-parameters',
        title: 'Шина и модули',
        subtitle: 'Показывает, что именно ESP32-S3 видит на I2C и UART: датчики, DAC мешалки и монитор питания.',
        actionsHtml: `
            <div class="controls equipment-actions">
                <button class="btn btn-secondary" type="button" onclick="loadEquipmentSettings()">Обновить статусы</button>
            </div>
        `,
    });
    appendUniqueGroups(qs('.equipment-grid', hardwareCard), formGroups, [
        'hardware-modules-list',
        'pzem-settings-state',
    ]);

    cardsHost.innerHTML = '';
    cardsHost.append(pumpCard, cubeCard, coolingCard, stirrerCard, hardwareCard);
}

function readSavedWorkbenchCard(storageKey, defs, fallbackId) {
    try {
        const stored = localStorage.getItem(storageKey);
        if (defs.some((card) => card.id === stored)) {
            return stored;
        }
    } catch {
        // ignore storage failures
    }
    return fallbackId;
}

function saveWorkbenchCard(storageKey, cardId) {
    try {
        localStorage.setItem(storageKey, cardId);
    } catch {
        // ignore storage failures
    }
}

function initEquipmentPaneWorkbench({
    sectionId,
    paneSelector,
    storageKey,
    groups,
    defs,
    title,
    subtitle,
    stateKey,
}) {
    const pane = qs(paneSelector);
    const cardsHost = qs('.cards', pane);
    if (!pane || !cardsHost || cardsHost.dataset.equipmentWorkbench === '1') return;

    const cards = [...cardsHost.querySelectorAll('.equipment-card')];
    const resolvedCards = resolveWorkbenchCards(cards, defs, groups);
    if (!resolvedCards.length) return;

    cardsHost.dataset.equipmentWorkbench = '1';

    const shell = createElement('div', 'equipment-testing-shell equipment-pane-workbench-shell');
    const main = createElement('div', 'equipment-testing-main equipment-pane-workbench-main');
    const desktopWrapper = createElement('div', 'equipment-testing-card-stack equipment-pane-workbench-stack');
    const { nav, buttonsById } = buildWorkbenchDesktopNav(resolvedCards, groups, title, subtitle);

    cardsHost.parentNode.insertBefore(shell, cardsHost);
    shell.append(nav, main);
    main.appendChild(desktopWrapper);
    desktopWrapper.appendChild(cardsHost);

    const mediaQuery = window.matchMedia(TESTING_LAYOUT_MEDIA);
    state[stateKey] = readSavedWorkbenchCard(storageKey, defs, defs[0]?.id || null);

    function scrollToggleIntoView(cardId) {
        const card = resolvedCards.find((entry) => entry.meta.id === cardId);
        if (!card) return;
        requestAnimationFrame(() => {
            card.toggle.scrollIntoView({ block: 'start', behavior: 'smooth' });
        });
    }

    function syncLayout() {
        const isMobile = mediaQuery.matches;
        pane.dataset.equipmentWorkbenchLayout = isMobile ? 'mobile' : 'desktop';
        nav.hidden = isMobile;

        if (!isMobile && !state[stateKey]) {
            state[stateKey] = resolvedCards[0]?.meta.id || null;
        }

        for (const card of resolvedCards) {
            const isActive = card.meta.id === state[stateKey];
            card.card.classList.toggle('is-active', isActive);
            card.toggle.classList.toggle('is-active', isActive);
            card.toggle.setAttribute('aria-expanded', String(isActive));
            card.body.hidden = isMobile ? !isActive : false;
            card.card.hidden = isMobile ? false : !isActive;

            const navButton = buttonsById.get(card.meta.id);
            if (navButton) {
                navButton.classList.toggle('active', isActive);
                navButton.setAttribute('aria-current', isActive ? 'true' : 'false');
            }
        }
    }

    function setActiveCard(cardId) {
        if (cardId !== null && !resolvedCards.some((entry) => entry.meta.id === cardId)) {
            return;
        }

        state[stateKey] = cardId;
        if (cardId) {
            saveWorkbenchCard(storageKey, cardId);
        }
        syncLayout();
    }

    paneWorkbenchControllers.set(sectionId, {
        setActiveCard,
        firstCardId: defs[0]?.id || null,
    });

    for (const card of resolvedCards) {
        card.toggle.addEventListener('click', () => {
            const isMobile = mediaQuery.matches;
            const isSame = state[stateKey] === card.meta.id;

            if (isMobile && isSame) {
                setActiveCard(null);
                return;
            }

            setActiveCard(card.meta.id);
            if (isMobile) {
                scrollToggleIntoView(card.meta.id);
            }
        });
    }

    for (const [cardId, button] of buttonsById.entries()) {
        button.addEventListener('click', () => {
            setActiveCard(cardId);
            main.scrollIntoView({ block: 'start', behavior: 'smooth' });
        });
    }

    if (typeof mediaQuery.addEventListener === 'function') {
        mediaQuery.addEventListener('change', syncLayout);
    } else if (typeof mediaQuery.addListener === 'function') {
        mediaQuery.addListener(syncLayout);
    }

    syncLayout();
}

function setEquipmentPaneCard(sectionId, cardId) {
    const controller = paneWorkbenchControllers.get(sectionId);
    if (controller) {
        controller.setActiveCard(cardId ?? controller.firstCardId);
    }
}

function ensureEquipmentShell() {
    const root = byId('equipment');
    if (!root || qs('.equipment-local-nav', root)) return;

    const cards = qs('.cards', root);
    const paramsCard = qs('.equipment-card-params', cards);
    const pumpCalibrationCard = qs('.equipment-card-pump', cards);
    const tempCalibrationCard = qs('.equipment-card-temp', cards);
    const pressureCalibrationCard = qs('.equipment-card-pressure', cards);
    const hydrometerCalibrationCard = qs('.equipment-card-hydrometer', cards);
    if (!cards || !paramsCard || !pumpCalibrationCard || !tempCalibrationCard || !pressureCalibrationCard || !hydrometerCalibrationCard) return;

    const shell = document.createElement('div');
    shell.className = 'equipment-shell';
    shell.innerHTML = `
        <div class="equipment-local-nav" role="tablist" aria-label="Подразделы оборудования">
            <button class="equipment-local-nav-btn active" type="button" data-equipment-section-btn="parameters">Параметры</button>
            <button class="equipment-local-nav-btn" type="button" data-equipment-section-btn="calibration">Калибровка</button>
            <button class="equipment-local-nav-btn" type="button" data-equipment-section-btn="testing">Тестирование</button>
        </div>
        <div class="equipment-pane active" data-equipment-section-pane="parameters"><div class="cards"></div></div>
        <div class="equipment-pane" data-equipment-section-pane="calibration"><div class="cards"></div></div>
        <div class="equipment-pane" data-equipment-section-pane="testing"></div>
    `;

    qs('[data-equipment-section-pane="parameters"] .cards', shell)?.appendChild(paramsCard);
    const calibrationCards = qs('[data-equipment-section-pane="calibration"] .cards', shell);
    calibrationCards?.appendChild(pumpCalibrationCard);
    calibrationCards?.appendChild(tempCalibrationCard);
    calibrationCards?.appendChild(pressureCalibrationCard);
    calibrationCards?.appendChild(hydrometerCalibrationCard);

    const testingPane = qs('[data-equipment-section-pane="testing"]', shell);
    if (testingPane) {
        testingPane.innerHTML = TESTING_TEMPLATE;
    }

    root.innerHTML = '';
    root.appendChild(shell);
}

function ensureSettingsShell() {
    const root = byId('settings');
    if (!root || qs('.settings-shell', root)) return;

    const cardsHost = qs('.cards', root);
    if (!cardsHost) return;

    const cards = [...cardsHost.querySelectorAll('.card')];
    if (!cards.length) return;

    const shell = document.createElement('div');
    shell.className = 'equipment-shell settings-shell';

    const nav = document.createElement('div');
    nav.className = 'equipment-local-nav settings-local-nav';
    nav.setAttribute('role', 'tablist');
    nav.setAttribute('aria-label', 'Подразделы настроек');

    const paneHost = document.createElement('div');
    paneHost.className = 'settings-pane-host';

    for (const section of SETTINGS_SECTION_DEFS) {
        const button = createElement('button', 'equipment-local-nav-btn');
        button.type = 'button';
        button.dataset.settingsSectionBtn = section.id;
        button.textContent = section.label;
        nav.appendChild(button);

        const pane = createElement('div', 'equipment-pane settings-pane');
        pane.dataset.settingsSectionPane = section.id;
        pane.innerHTML = '<div class="cards"></div>';
        const paneCards = qs('.cards', pane);

        for (const meta of section.defs) {
            const card = cards.find((candidate) => candidate.querySelector(meta.selector));
            if (card) {
                paneCards.appendChild(card);
            }
        }

        paneHost.appendChild(pane);
    }

    shell.append(nav, paneHost);
    root.innerHTML = '';
    root.appendChild(shell);
}

function initEquipmentTestingWorkbench() {
    const testingPane = qs('[data-equipment-section-pane="testing"]');
    const grid = qs('.equipment-testing-grid', testingPane);
    if (!testingPane || !grid || grid.dataset.testingWorkbench === '1') return;
    const statusCard = qs('.equipment-test-status-card', testingPane);
    if (statusCard) {
        statusCard.classList.add('equipment-test-card');
    }

    const cards = [statusCard, ...grid.querySelectorAll('.equipment-test-card')].filter(Boolean);
    const resolvedCards = resolveTestingCards(cards);
    if (!resolvedCards.length) return;

    grid.dataset.testingWorkbench = '1';

    const shell = createElement('div', 'equipment-testing-shell');
    const main = createElement('div', 'equipment-testing-main');
    const desktopWrapper = createElement('div', 'equipment-testing-card-stack');
    const { nav, buttonsById } = buildTestingDesktopNav(resolvedCards);

    grid.parentNode.insertBefore(shell, statusCard || grid);
    shell.append(nav, main);
    main.appendChild(desktopWrapper);
    if (statusCard) {
        desktopWrapper.append(statusCard, grid);
    } else {
        desktopWrapper.appendChild(grid);
    }
    const mediaQuery = window.matchMedia(TESTING_LAYOUT_MEDIA);
    state.activeTestingCard = readSavedTestingCard();

    function scrollToggleIntoView(cardId) {
        const card = resolvedCards.find((entry) => entry.meta.id === cardId);
        if (!card) return;
        requestAnimationFrame(() => {
            card.toggle.scrollIntoView({ block: 'start', behavior: 'smooth' });
        });
    }

    function syncTestingLayout() {
        const isMobile = mediaQuery.matches;
        testingPane.dataset.testingLayout = isMobile ? 'mobile' : 'desktop';
        nav.hidden = isMobile;

        if (!isMobile && !state.activeTestingCard) {
            state.activeTestingCard = resolvedCards[0]?.meta.id || null;
        }

        for (const card of resolvedCards) {
            const isActive = card.meta.id === state.activeTestingCard;
            card.card.classList.toggle('is-active', isActive);
            card.toggle.classList.toggle('is-active', isActive);
            card.toggle.setAttribute('aria-expanded', String(isActive));
            card.body.hidden = isMobile ? !isActive : false;
            card.card.hidden = isMobile ? false : !isActive;

            const navButton = buttonsById.get(card.meta.id);
            if (navButton) {
                navButton.classList.toggle('active', isActive);
                navButton.setAttribute('aria-current', isActive ? 'true' : 'false');
            }
        }
    }

    function setActiveTestingCard(cardId) {
        if (cardId !== null && !resolvedCards.some((entry) => entry.meta.id === cardId)) {
            return;
        }

        state.activeTestingCard = cardId;
        if (cardId) {
            saveActiveTestingCard(cardId);
        }
        syncTestingLayout();
    }

    testingWorkbenchController = {
        setActiveCard: setActiveTestingCard,
        firstCardId: TESTING_CARD_DEFS[0]?.id || null
    };

    for (const card of resolvedCards) {
        card.toggle.addEventListener('click', () => {
            const isMobile = mediaQuery.matches;
            const isSame = state.activeTestingCard === card.meta.id;

            if (isMobile && isSame) {
                setActiveTestingCard(null);
                return;
            }

            setActiveTestingCard(card.meta.id);
            if (isMobile) {
                scrollToggleIntoView(card.meta.id);
            }
        });
    }

    for (const [cardId, button] of buttonsById.entries()) {
        button.addEventListener('click', () => {
            setActiveTestingCard(cardId);
            main.scrollIntoView({ block: 'start', behavior: 'smooth' });
        });
    }

    if (typeof mediaQuery.addEventListener === 'function') {
        mediaQuery.addEventListener('change', syncTestingLayout);
    } else if (typeof mediaQuery.addListener === 'function') {
        mediaQuery.addListener(syncTestingLayout);
    }

    syncTestingLayout();
}

function initEquipmentParametersWorkbench() {
    ensureParameterWorkbenchCards();
    initEquipmentPaneWorkbench({
        sectionId: 'parameters',
        paneSelector: '[data-equipment-section-pane="parameters"]',
        storageKey: PARAMETERS_CARD_STORAGE_KEY,
        groups: PARAMETERS_GROUPS,
        defs: PARAMETERS_CARD_DEFS,
        title: 'Параметры',
        subtitle: 'Открыт один компактный блок настроек, остальные доступны через меню без длинной простыни форм.',
        stateKey: 'activeParameterCard',
    });
}

function initEquipmentCalibrationWorkbench() {
    initEquipmentPaneWorkbench({
        sectionId: 'calibration',
        paneSelector: '[data-equipment-section-pane="calibration"]',
        storageKey: CALIBRATION_CARD_STORAGE_KEY,
        groups: CALIBRATION_GROUPS,
        defs: CALIBRATION_CARD_DEFS,
        title: 'Калибровка',
        subtitle: 'Открыт один сервисный мастер, остальные остаются под рукой через компактное меню.',
        stateKey: 'activeCalibrationCard',
    });
}

function ensureHeaterConfirmModal() {
    if (byId('equipment-heater-confirm-modal')) return;
    document.body.insertAdjacentHTML('beforeend', HEATER_MODAL_TEMPLATE);
}

function isEquipmentTestingVisible() {
    return byId('equipment')?.classList.contains('active') && state.activeSection === 'testing';
}

function readSavedSection() {
    try {
        const stored = localStorage.getItem(STORAGE_KEY);
        if (stored === 'parameters' || stored === 'calibration' || stored === 'testing') {
            return stored;
        }
    } catch {
        // ignore storage failures
    }
    return 'parameters';
}

function saveActiveSection(sectionId) {
    try {
        localStorage.setItem(STORAGE_KEY, sectionId);
    } catch {
        // ignore storage failures
    }
}

function readSavedTestingCard() {
    try {
        const stored = localStorage.getItem(TESTING_CARD_STORAGE_KEY);
        if (TESTING_CARD_DEFS.some((card) => card.id === stored)) {
            return stored;
        }
    } catch {
        // ignore storage failures
    }
    return TESTING_CARD_DEFS[0]?.id || 'pump';
}

function saveActiveTestingCard(cardId) {
    try {
        localStorage.setItem(TESTING_CARD_STORAGE_KEY, cardId);
    } catch {
        // ignore storage failures
    }
}

function resetCommissioningManualChecks() {
    state.commissioning.manual = {
        pump: false,
        valves: false,
        heater: false
    };
}

function setCommissioningManualCheck(target, nextValue = true) {
    if (!Object.prototype.hasOwnProperty.call(state.commissioning.manual, target)) {
        return;
    }
    state.commissioning.manual[target] = Boolean(nextValue);
}

function getCommissioningManualTargets() {
    return [
        { id: 'pump', label: 'насос' },
        { id: 'valves', label: 'клапаны' },
        { id: 'heater', label: 'нагрев' }
    ];
}

function getTestingCardMeta(cardId) {
    return TESTING_CARD_DEFS.find((card) => card.id === cardId) || null;
}

function setTestingCard(cardId) {
    testingWorkbenchController?.setActiveCard(cardId ?? testingWorkbenchController?.firstCardId ?? null);
}

function openEquipmentWorkbench(sectionId, cardId) {
    setEquipmentSection(sectionId);
    if (sectionId === 'testing') {
        setTestingCard(cardId);
        return;
    }
    setEquipmentPaneCard(sectionId, cardId);
}

export function setEquipmentSection(sectionId) {
    state.activeSection = sectionId;
    saveActiveSection(sectionId);

    qsa('[data-equipment-section-btn]').forEach((button) => {
        button.classList.toggle('active', button.dataset.equipmentSectionBtn === sectionId);
    });

    qsa('[data-equipment-section-pane]').forEach((pane) => {
        pane.classList.toggle('active', pane.dataset.equipmentSectionPane === sectionId);
    });

    setEquipmentPaneCard(sectionId, null);

    if (sectionId === 'calibration') {
        window.loadCalibrationData?.();
    } else if (sectionId === 'testing') {
        void refreshEquipmentTestingStatus();
    }
}

function readSavedSettingsSection() {
    try {
        const stored = localStorage.getItem(SETTINGS_SECTION_STORAGE_KEY);
        if (SETTINGS_SECTION_DEFS.some((section) => section.id === stored)) {
            return stored;
        }
    } catch {
        // ignore storage failures
    }
    return SETTINGS_SECTION_DEFS[0]?.id || 'connection';
}

function saveSettingsSection(sectionId) {
    try {
        localStorage.setItem(SETTINGS_SECTION_STORAGE_KEY, sectionId);
    } catch {
        // ignore storage failures
    }
}

function bindSettingsSectionNav() {
    qsa('[data-settings-section-btn]').forEach((button) => {
        button.addEventListener('click', () => {
            setSettingsSection(button.dataset.settingsSectionBtn);
        });
    });
}

export function setSettingsSection(sectionId) {
    state.activeSettingsSection = sectionId;
    saveSettingsSection(sectionId);

    qsa('[data-settings-section-btn]').forEach((button) => {
        button.classList.toggle('active', button.dataset.settingsSectionBtn === sectionId);
    });

    qsa('[data-settings-section-pane]').forEach((pane) => {
        pane.classList.toggle('active', pane.dataset.settingsSectionPane === sectionId);
    });

    setEquipmentPaneCard(`settings:${sectionId}`, null);
}

function getActiveTestsSummary(activeTests = {}) {
    const active = [];
    if (activeTests.pump) active.push('Насос');
    if (activeTests.stirrer) active.push('Мешалка');
    if (activeTests.heater) active.push('ТЭН');
    if (activeTests.waterValve) active.push('Вода');
    if (activeTests.headsValve) active.push('Головы');
    if (activeTests.unoValve) active.push('УНО');
    if (activeTests.servoMoving) active.push('Сервопривод');
    return active.length ? active.join(', ') : 'Ничего не включено';
}

function countValidTemperatures(temperatures = []) {
    if (!Array.isArray(temperatures)) return 0;
    return temperatures.reduce((count, sensor) => count + (sensor?.valid ? 1 : 0), 0);
}

function buildCommissioningSteps(status) {
    const temperatures = Array.isArray(status?.temperatures) ? status.temperatures : [];
    const validTemps = countValidTemperatures(temperatures);
    const pressure = status?.pressure || {};
    const heater = status?.heater || {};
    const power = status?.power || {};
    const manualTargets = getCommissioningManualTargets();
    const manualDone = manualTargets.filter((target) => state.commissioning.manual[target.id]);
    const missingManual = manualTargets.filter((target) => !state.commissioning.manual[target.id]);
    const pressureSpan = state.pressureTest.min !== null && state.pressureTest.max !== null
        ? Math.abs(Number(state.pressureTest.max) - Number(state.pressureTest.min))
        : null;

    const steps = [];

    const safeStep = {
        id: 'safe-window',
        title: 'Безопасное сервисное окно',
        blocking: true,
        tone: 'success',
        label: 'ОК',
        description: 'Контроллер в IDLE, защёлкнутых аварий нет, сервисные тесты разрешены.'
    };
    if (!status?.testingAllowed) {
        safeStep.tone = 'danger';
        safeStep.label = 'Стоп';
        safeStep.description = status?.availabilityReason || 'Сервисное тестирование сейчас заблокировано.';
    } else if (status?.demoMode) {
        safeStep.tone = 'warning';
        safeStep.label = 'Демо';
        safeStep.description = 'Сервисный экран открыт в демо-режиме: UI живой, но реальное железо не проверяется.';
    }
    steps.push(safeStep);

    const tempStep = {
        id: 'temperatures',
        title: 'Термометры и 1-Wire шина',
        blocking: true,
        tone: 'success',
        label: 'ОК',
        description: `Видим ${validTemps} рабочих датч. температуры. Можно сверять живые показания и продолжать пусконаладку.`,
        actions: [
            { type: 'open', label: 'Открыть термометры', section: 'testing', cardId: 'temps' },
            { type: 'open', label: 'К калибровке', section: 'calibration', cardId: 'temp-calibration' }
        ]
    };
    if (validTemps <= 0) {
        tempStep.tone = 'danger';
        tempStep.label = 'Нет';
        tempStep.description = 'Рабочие DS18B20 не подтверждены. Сначала проверьте сканирование, адреса и общую 1-Wire линию.';
    } else if (validTemps === 1) {
        tempStep.tone = 'warning';
        tempStep.label = 'Мало';
        tempStep.description = 'Сейчас подтверждён только один термодатчик. Для нормальной пусконаладки лучше увидеть хотя бы куб и колонну.';
    }
    steps.push(tempStep);

    const pressureStep = {
        id: 'pressure',
        title: 'Манометр куба',
        blocking: Boolean(pressure?.ads1115Available),
        tone: 'success',
        label: 'ОК',
        description: state.pressureTest.success
            ? `Отклик продувкой подтверждён. Диапазон теста ${formatNumber(pressureSpan, 1, ' мм рт.ст.')}.`
            : 'Сигнал давления есть, но продувка ещё не подтверждена.',
        actions: [
            { type: 'open', label: 'Открыть датчик', section: 'testing', cardId: 'pressure' },
            { type: 'pressure-toggle', label: state.pressureTest.active ? 'Сбросить тест' : 'Начать продувку' }
        ]
    };
    if (!pressure?.ads1115Available) {
        pressureStep.tone = 'warning';
        pressureStep.label = 'Нет ADS';
        pressureStep.description = 'ADS1115 A1 сейчас не виден. Шаг оставлен как напоминание, но не считаем его блокером.';
    } else if (!pressure?.ok) {
        pressureStep.tone = 'danger';
        pressureStep.label = 'Нет сигнала';
        pressureStep.description = 'Канал давления не даёт валидного сигнала. Проверьте ADS1115, проводку и сам датчик куба.';
    } else if (!state.pressureTest.success) {
        pressureStep.tone = 'warning';
        pressureStep.label = 'Ждёт продувку';
        pressureStep.description = 'Сигнал есть, но отклик продувкой ещё не подтверждён. Нажмите кнопку и слегка подуйте в сухую линию.';
    }
    steps.push(pressureStep);

    const heaterStep = {
        id: 'heater',
        title: 'Силовой контур нагрева',
        blocking: heater?.backend === 'triac',
        tone: 'success',
        label: 'ОК',
        description: 'TRIAC backend подтверждён, zero-cross виден, PZEM на линии — силовой канал читается штатно.',
        actions: [
            { type: 'open', label: 'Открыть нагрев', section: 'testing', cardId: 'heater' }
        ]
    };
    if (heater?.backend === 'triac') {
        if (!heater?.zeroCrossSeen) {
            heaterStep.tone = 'danger';
            heaterStep.label = 'Нет sync';
            heaterStep.description = 'Основной TRIAC выбран, но zero-cross не виден. Без синхронизации фазовое управление пока не подтверждено.';
        } else if (!power?.available) {
            heaterStep.tone = 'warning';
            heaterStep.label = 'Без PZEM';
            heaterStep.description = 'Zero-cross уже виден, но PZEM сейчас офлайн. Нагрев работает, но силовой мониторинг пока неполный.';
        }
    } else if (heater?.backend === 'ssr') {
        heaterStep.tone = 'warning';
        heaterStep.label = 'SSR';
        heaterStep.description = 'Основной канал сейчас не в TRIAC-режиме. Для фазового управления откройте карточку нагрева и проверьте backend.';
    } else {
        heaterStep.tone = 'warning';
        heaterStep.label = 'Нет данных';
        heaterStep.description = 'Контур нагрева ещё не описал свой backend. Откройте сервисную карточку и проверьте диагностику.';
    }
    steps.push(heaterStep);

    const manualStep = {
        id: 'manual-actuators',
        title: 'Ручное подтверждение исполнительных узлов',
        blocking: true,
        tone: 'success',
        label: 'ОК',
        description: manualDone.length
            ? `Подтверждены: ${manualDone.map((item) => item.label).join(', ')}.`
            : 'После живой проверки отметьте вручную насос, клапаны и нагрев.',
        actions: [
            { type: 'open-missing-manual', label: 'Открыть следующий узел' },
            { type: 'toggle-manual', target: 'pump', label: state.commissioning.manual.pump ? 'Насос OK' : 'Отметить насос' },
            { type: 'toggle-manual', target: 'valves', label: state.commissioning.manual.valves ? 'Клапаны OK' : 'Отметить клапаны' },
            { type: 'toggle-manual', target: 'heater', label: state.commissioning.manual.heater ? 'Нагрев OK' : 'Отметить нагрев' },
            { type: 'reset-manual', label: 'Сброс' }
        ]
    };
    if (missingManual.length) {
        manualStep.tone = 'warning';
        manualStep.label = `${manualDone.length}/${manualTargets.length}`;
        manualStep.description = `После живой проверки отметьте вручную: ${missingManual.map((item) => item.label).join(', ')}.`;
    }
    steps.push(manualStep);

    return steps;
}

function renderCommissioningWizard(status) {
    const stepsHost = byId('equipment-commissioning-steps');
    const summaryHost = byId('equipment-commissioning-summary');
    const nextStepEl = byId('equipment-commissioning-next-step');
    const overallBadge = byId('equipment-commissioning-overall-badge');
    if (!stepsHost || !summaryHost || !nextStepEl || !overallBadge) {
        return;
    }

    const steps = buildCommissioningSteps(status);
    const completed = steps.filter((step) => step.tone === 'success').length;
    const required = steps.filter((step) => step.blocking);
    const requiredDone = required.filter((step) => step.tone === 'success').length;
    const firstBlockingPending = required.find((step) => step.tone !== 'success') || null;
    const firstPending = steps.find((step) => step.tone !== 'success') || null;
    const nextStep = firstBlockingPending || firstPending;
    const manualDone = getCommissioningManualTargets().filter((target) => state.commissioning.manual[target.id]).length;
    const ready = required.length > 0 && required.every((step) => step.tone === 'success');

    updateBadge(
        overallBadge,
        ready ? 'Готово' : (nextStep?.tone === 'danger' ? 'Есть блокеры' : 'Нужна проверка'),
        ready ? 'success' : nextStep?.tone === 'danger' ? 'danger' : 'warning'
    );

    summaryHost.innerHTML = `
        <div class="equipment-inline-stat"><span>Пройдено</span><strong>${completed}/${steps.length}</strong></div>
        <div class="equipment-inline-stat"><span>Обязательные</span><strong>${requiredDone}/${required.length}</strong></div>
        <div class="equipment-inline-stat"><span>Ручные узлы</span><strong>${manualDone}/3</strong></div>
    `;

    nextStepEl.className = `equipment-test-alert${ready ? '' : nextStep?.tone === 'danger' ? ' danger' : ' subtle'}`;
    nextStepEl.textContent = ready
        ? 'Базовая пусконаладка закрыта: можно двигаться дальше к профильным режимам и тонкой настройке.'
        : `Следующий шаг: ${nextStep?.title || 'проверка оборудования'}. ${nextStep?.description || ''}`;

    stepsHost.innerHTML = steps.map((step) => `
        <div class="equipment-commissioning-step tone-${step.tone}">
            <div class="equipment-commissioning-step-main">
                <div class="equipment-commissioning-step-top">
                    <strong>${step.title}</strong>
                    <span class="equipment-status-badge ${step.tone}">${step.label}</span>
                </div>
                <div class="equipment-commissioning-step-text">${step.description}</div>
            </div>
            ${Array.isArray(step.actions) && step.actions.length ? `
                <div class="equipment-commissioning-step-actions">
                    ${step.actions.map((action) => {
                        if (action.type === 'open') {
                            return `<button type="button" class="btn btn-sm btn-secondary" data-commissioning-open="${action.section}:${action.cardId}">${action.label}</button>`;
                        }
                        if (action.type === 'pressure-toggle') {
                            return `<button type="button" class="btn btn-sm btn-secondary" data-commissioning-pressure-toggle="1">${action.label}</button>`;
                        }
                        if (action.type === 'toggle-manual') {
                            const active = state.commissioning.manual[action.target];
                            return `<button type="button" class="btn btn-sm ${active ? 'btn-success' : 'btn-outline-secondary'}" data-commissioning-manual="${action.target}">${action.label}</button>`;
                        }
                        if (action.type === 'reset-manual') {
                            const disabled = getCommissioningManualTargets().every((target) => !state.commissioning.manual[target.id]);
                            return `<button type="button" class="btn btn-sm btn-outline-secondary" ${disabled ? 'disabled' : ''} data-commissioning-manual-reset="1">${action.label}</button>`;
                        }
                        if (action.type === 'open-missing-manual') {
                            const nextTarget = getCommissioningManualTargets().find((target) => !state.commissioning.manual[target.id])?.id || 'pump';
                            const openCardId = nextTarget === 'valves' ? 'valves' : nextTarget === 'heater' ? 'heater' : 'pump';
                            const meta = getTestingCardMeta(openCardId);
                            return `<button type="button" class="btn btn-sm btn-secondary" data-commissioning-open="testing:${openCardId}">${meta?.shortTitle ? `Открыть ${meta.shortTitle.toLowerCase()}` : action.label}</button>`;
                        }
                        return '';
                    }).join('')}
                </div>
            ` : ''}
        </div>
    `).join('');
}

function normalizeTestingHardwareModules(modules = {}) {
    return [
        modules.bmp280Primary && typeof modules.bmp280Primary === 'object' ? modules.bmp280Primary : {},
        modules.bmp280Secondary && typeof modules.bmp280Secondary === 'object' ? modules.bmp280Secondary : {},
        modules.ads1115 && typeof modules.ads1115 === 'object' ? modules.ads1115 : {},
        modules.mcp4725 && typeof modules.mcp4725 === 'object' ? modules.mcp4725 : {},
        modules.pzem004t && typeof modules.pzem004t === 'object' ? modules.pzem004t : {}
    ];
}

function getTestingHardwareModuleMeta(module = {}) {
    const available = Boolean(module.available);
    const expected = module.expected !== false;

    if (available) {
        return { text: 'Онлайн', tone: 'success' };
    }
    if (expected) {
        return { text: 'Нет ответа', tone: 'danger' };
    }
    return { text: 'Опция', tone: 'muted' };
}

function buildTestingHardwareModuleCard(module = {}) {
    const meta = getTestingHardwareModuleMeta(module);
    const pins = Number.isFinite(Number(module.rxPin)) && Number.isFinite(Number(module.txPin))
        ? `GPIO${Number(module.rxPin)} / GPIO${Number(module.txPin)}`
        : '';
    const baudRate = Number.isFinite(Number(module.baudRate)) ? `${Number(module.baudRate)} бод` : '';
    const extras = [pins, baudRate]
        .filter(Boolean)
        .map((item) => `<span>${item}</span>`)
        .join('');

    return `
        <div class="equipment-module-card">
            <div class="equipment-module-card-head">
                <strong>${module.label || 'Модуль'}</strong>
                <span class="equipment-status-badge ${meta.tone}">${meta.text}</span>
            </div>
            <div class="equipment-module-card-meta">
                <span>${module.bus || '—'}</span>
                <span>${module.address || '—'}</span>
                ${extras}
            </div>
            <div class="equipment-module-card-role">${module.role || '—'}</div>
        </div>
    `;
}

function buildServiceContour(status) {
    const modules = status?.modules || {};
    const heater = status?.heater || {};
    const power = status?.power || {};
    const pressure = status?.pressure || {};
    const health = status?.health || {};
    const validTemps = countValidTemperatures(status?.temperatures);
    const tempSensorsOk = Math.max(0, Number(health.tempSensorsOk || validTemps || 0));
    const tempSensorsTotal = Math.max(0, Number(health.tempSensorsTotal || status?.temperatures?.length || 0));
    const adsOnline = Boolean(modules?.ads1115?.available);
    const bmpOnline = Boolean(modules?.bmp280Primary?.available || modules?.bmp280Secondary?.available);
    const pressureOnline = Boolean(pressure?.ok);
    const zeroCrossNeeded = heater?.backend === 'triac';
    const zeroCrossOnline = Boolean(heater?.zeroCrossSeen);
    const pzemOnline = Boolean(power?.available);

    const items = [
        {
            key: 'temps',
            label: 'Термошина',
            tone: tempSensorsOk > 0 ? 'success' : 'danger',
            value: tempSensorsTotal > 0 ? `${tempSensorsOk}/${tempSensorsTotal}` : 'нет линии',
            detail: tempSensorsOk > 0 ? 'DS18B20 отвечают' : 'нет подтверждённых термодатчиков'
        },
        {
            key: 'ads',
            label: 'ADS1115',
            tone: adsOnline ? 'success' : 'danger',
            value: adsOnline ? 'online' : 'offline',
            detail: adsOnline ? 'сервисный АЦП виден на шине' : 'ESP32 не видит сервисный АЦП'
        },
        {
            key: 'bmp',
            label: 'BMP280',
            tone: bmpOnline ? 'success' : 'danger',
            value: bmpOnline ? 'online' : 'offline',
            detail: bmpOnline ? 'барометрический модуль отвечает' : 'нет ответа от барометрии'
        },
        {
            key: 'pressure',
            label: 'Манометр куба',
            tone: pressureOnline ? 'success' : 'danger',
            value: pressureOnline ? formatNumber(pressure?.cubeMmHg, 1, ' мм рт.ст.') : 'нет сигнала',
            detail: pressureOnline ? 'кубовое давление считывается' : 'датчик давления не подтверждён'
        },
        {
            key: 'zero-cross',
            label: 'Zero-cross',
            tone: !zeroCrossNeeded ? 'muted' : zeroCrossOnline ? 'success' : 'danger',
            value: !zeroCrossNeeded ? (heater?.backend ? heater.backend : 'не активен') : zeroCrossOnline ? 'есть' : 'нет',
            detail: !zeroCrossNeeded
                ? 'контроль нуля нужен только для TRIAC backend'
                : zeroCrossOnline
                    ? `${formatNumber(heater?.zeroCrossCount, 0)} переходов, ${formatNumber(heater?.triacFireCount, 0)} gate pulses`
                    : 'нет синхронизации с сетью для фазового управления'
        },
        {
            key: 'pzem',
            label: 'PZEM-004T',
            tone: pzemOnline ? 'success' : 'danger',
            value: pzemOnline ? formatNumber(power?.power, 0, ' Вт') : 'offline',
            detail: pzemOnline
                ? `${formatNumber(power?.voltage, 1, ' В')} / ${formatNumber(power?.current, 2, ' А')}`
                : 'силовой монитор не подтверждён'
        }
    ];

    const blockers = items.filter((item) => item.tone === 'danger').length;
    const warnings = items.filter((item) => item.tone === 'muted').length;

    let tone = 'success';
    let label = 'Контур готов';
    let hint = 'Ключевые сервисные узлы подтверждены, можно переходить к ручным тестам и пусконаладке.';

    if (blockers > 0) {
        tone = 'danger';
        label = `Есть блокеры: ${blockers}`;
        hint = 'Часть сервисного контура не подтверждена. Сначала добей шину, датчики и силовой монитор, потом запускай ручные проверки.';
    } else if (warnings > 0) {
        tone = 'warning';
        label = 'Контур частичный';
        hint = 'Базовый сервисный контур жив, но часть узлов сейчас не обязательна или не активна в текущем backend.';
    }

    return { tone, label, hint, items };
}

function renderServiceContour(status) {
    const badgeEl = byId('equipment-service-contour-badge');
    const hintEl = byId('equipment-service-contour-hint');
    const gridEl = byId('equipment-service-contour-grid');
    if (!badgeEl || !hintEl || !gridEl) {
        return;
    }

    const contour = buildServiceContour(status);
    updateBadge(badgeEl, contour.label, contour.tone);
    hintEl.textContent = contour.hint;
    hintEl.className = `equipment-test-alert${contour.tone === 'danger' ? ' danger' : contour.tone === 'warning' ? '' : ' subtle'}`;
    gridEl.innerHTML = contour.items.map((item) => `
        <div class="equipment-service-contour-item tone-${item.tone}">
            <div class="equipment-service-contour-line">
                <strong>${item.label}</strong>
                <span class="equipment-status-badge ${item.tone}">${item.value}</span>
            </div>
            <div class="equipment-service-contour-detail">${item.detail}</div>
        </div>
    `).join('');
}

function buildSelfCheckSnapshot(status) {
    const modules = normalizeTestingHardwareModules(status?.modules || {});
    const health = status?.health || {};
    const tempSensorsOk = Math.max(0, Number(health.tempSensorsOk || 0));
    const tempSensorsTotal = Math.max(0, Number(health.tempSensorsTotal || 0));
    const overall = Math.max(0, Math.min(100, Number(health.overall || 0)));
    const expectedModules = modules.filter((module) => module.expected !== false);
    const missingExpected = expectedModules.filter((module) => !module.available);
    const pzemSpikes = Math.max(0, Number(health.pzemSpikes || 0));
    const tempErrors = Math.max(0, Number(health.tempErrors || 0));
    const rebootReason = health.rebootReasonStr || '—';

    let tone = 'success';
    let title = 'Железо отвечает штатно';
    const details = [
        `health ${overall}%`,
        `термодатчики ${tempSensorsOk}/${tempSensorsTotal}`,
        `reboot ${rebootReason}`
    ];

    if (missingExpected.length > 0) {
        tone = 'danger';
        title = 'Потеря обязательных модулей';
        details.push(`нет ответа: ${missingExpected.map((module) => module.label || 'модуль').join(', ')}`);
    } else if (tempSensorsOk <= 0) {
        tone = 'danger';
        title = 'Нет подтверждённых термодатчиков';
    } else if (overall < 70 || pzemSpikes > 0 || tempErrors > 0) {
        tone = 'warning';
        title = 'Есть деградация precheck';
        if (pzemSpikes > 0) details.push(`spikes ${pzemSpikes}`);
        if (tempErrors > 0) details.push(`temp errors ${tempErrors}`);
    }

    return {
        tone,
        title,
        detail: details.join(' • '),
        signature: [
            tone,
            overall,
            tempSensorsOk,
            tempSensorsTotal,
            rebootReason,
            pzemSpikes,
            tempErrors,
            missingExpected.map((module) => module.label || 'module').join('|')
        ].join('::')
    };
}

function recordSelfCheckSnapshot(status) {
    const snapshot = buildSelfCheckSnapshot(status);
    const now = Date.now();
    const shouldAppend = !state.selfCheckLastSignature
        || snapshot.signature !== state.selfCheckLastSignature
        || now - state.selfCheckLastRecordedAt >= 60000;

    if (!shouldAppend) {
        return;
    }

    state.selfCheckLastSignature = snapshot.signature;
    state.selfCheckLastRecordedAt = now;
    state.selfCheckHistory.unshift({
        timestampMs: now,
        tone: snapshot.tone,
        title: snapshot.title,
        detail: snapshot.detail
    });
    state.selfCheckHistory = state.selfCheckHistory.slice(0, 10);
}

function renderSelfCheckHistory() {
    const host = byId('equipment-selfcheck-history');
    if (!host) return;

    if (!Array.isArray(state.selfCheckHistory) || !state.selfCheckHistory.length) {
        host.innerHTML = '<div class="equipment-test-journal-empty">История появится после первых изменений состояния.</div>';
        return;
    }

    host.innerHTML = state.selfCheckHistory.map((entry) => `
        <div class="equipment-test-journal-item ${entry.tone || 'neutral'}">
            <div class="equipment-test-journal-line">
                <strong>${entry.title || 'Precheck'}</strong>
                <span>${formatWallClock(entry.timestampMs)}</span>
            </div>
            <div class="equipment-test-journal-detail">${entry.detail || ''}</div>
        </div>
    `).join('');
}

function renderHardwareSelfCheck(status) {
    const badgeEl = byId('equipment-selfcheck-badge');
    const summaryEl = byId('equipment-selfcheck-summary');
    const hintEl = byId('equipment-selfcheck-hint');
    const modulesEl = byId('equipment-selfcheck-modules');
    if (!badgeEl || !summaryEl || !hintEl || !modulesEl) {
        return;
    }

    const modules = normalizeTestingHardwareModules(status?.modules || {});
    const health = status?.health || {};
    const onlineCount = modules.filter((module) => Boolean(module.available)).length;
    const expectedModules = modules.filter((module) => module.expected !== false);
    const expectedOnline = expectedModules.filter((module) => Boolean(module.available)).length;
    const missingExpected = expectedModules.filter((module) => !module.available).length;
    const tempSensorsOk = Math.max(0, Number(health.tempSensorsOk || 0));
    const tempSensorsTotal = Math.max(0, Number(health.tempSensorsTotal || 0));
    const overall = Math.max(0, Math.min(100, Number(health.overall || 0)));
    const pzemSpikes = Math.max(0, Number(health.pzemSpikes || 0));
    const tempErrors = Math.max(0, Number(health.tempErrors || 0));
    const rebootReason = health.rebootReasonStr || '—';

    let tone = 'success';
    let label = 'Готово';
    let hint = `Обязательные модули в онлайне: ${expectedOnline}/${expectedModules.length}.`;

    if (missingExpected > 0 || tempSensorsOk <= 0) {
        tone = 'danger';
        label = 'Есть блокеры';
        hint = missingExpected > 0
            ? `Нет ответа от обязательных модулей: ${missingExpected}/${expectedModules.length}. Сначала добейте шину и питание.`
            : 'Температурная линия сейчас не подтверждена. Без термодатчиков нормальная пусконаладка бессмысленна.';
    } else if (overall < 70 || pzemSpikes > 0 || tempErrors > 0) {
        tone = 'warning';
        label = 'Нужна проверка';
        hint = `Железо в целом отвечает, но есть инженерные маркеры для разбора: health ${overall}%, spikes ${pzemSpikes}, temp errors ${tempErrors}.`;
    }

    updateBadge(badgeEl, label, tone);
    summaryEl.innerHTML = `
        <div class="equipment-inline-stat"><span>Health</span><strong>${overall}%</strong></div>
        <div class="equipment-inline-stat"><span>Термодатчики</span><strong>${tempSensorsOk}/${tempSensorsTotal}</strong></div>
        <div class="equipment-inline-stat"><span>Модули online</span><strong>${onlineCount}/${modules.length}</strong></div>
        <div class="equipment-inline-stat"><span>Reboot</span><strong>${rebootReason}</strong></div>
    `;
    hintEl.textContent = hint;
    modulesEl.innerHTML = modules.map((module) => buildTestingHardwareModuleCard(module)).join('');
    renderSelfCheckHistory();
}

function renderTemperatureList(temperatures = []) {
    const host = byId('equipment-testing-temps-list');
    if (!host) return;

    host.innerHTML = '';
    for (const sensor of temperatures) {
        const item = document.createElement('div');
        item.className = `equipment-test-sensor ${sensor.valid ? 'is-valid' : 'is-invalid'}`;
        item.innerHTML = `
            <div class="equipment-test-sensor-name">${sensor.label}</div>
            <div class="equipment-test-sensor-value">${formatSensorValue(sensor)}</div>
        `;
        host.appendChild(item);
    }
}

function renderPressureStatus(data) {
    const badge = byId('equipment-test-pressure-badge');
    const summary = byId('equipment-test-pressure-summary');
    if (!data) {
        updateBadge(badge, 'Нет данных', 'danger');
        setText('equipment-test-pressure-value', '-- мм рт.ст.');
        setText('equipment-test-pressure-hint', 'Нет актуальных значений давления');
        if (summary) summary.innerHTML = '';
        return;
    }

    updateBadge(badge, data.ok ? 'Онлайн' : 'Нет сигнала', data.ok ? 'success' : 'danger');
    setText('equipment-test-pressure-value', formatNumber(data.cubeMmHg, 1, ' мм рт.ст.'));

    if (!state.pressureTest.active) {
        setText('equipment-test-pressure-hint', 'Для теста нажмите кнопку ниже и слегка подуйте в датчик.');
        if (summary) {
            summary.innerHTML = `
                <div class="equipment-inline-stat"><span>Атмосфера</span><strong>${formatNumber(data.atmosphere, 1, ' гПа')}</strong></div>
                <div class="equipment-inline-stat"><span>Сигнал</span><strong>${formatBool(data.ok, 'Есть', 'Нет')}</strong></div>
            `;
        }
        return;
    }

    if (state.pressureTest.baseline === null && Number.isFinite(Number(data.cubeMmHg))) {
        state.pressureTest.baseline = Number(data.cubeMmHg);
        state.pressureTest.min = Number(data.cubeMmHg);
        state.pressureTest.max = Number(data.cubeMmHg);
    }

    const current = Number(data.cubeMmHg);
    if (Number.isFinite(current)) {
        state.pressureTest.min = state.pressureTest.min === null ? current : Math.min(state.pressureTest.min, current);
        state.pressureTest.max = state.pressureTest.max === null ? current : Math.max(state.pressureTest.max, current);
        if (state.pressureTest.baseline !== null &&
            Math.abs(current - state.pressureTest.baseline) >= PRESSURE_SUCCESS_DELTA) {
            state.pressureTest.success = true;
        }
    }

    const delta = state.pressureTest.baseline === null || !Number.isFinite(current)
        ? null
        : current - state.pressureTest.baseline;

    if (state.pressureTest.success) {
        setText('equipment-test-pressure-hint', 'Изменение давления обнаружено, датчик реагирует.');
    } else {
        setText('equipment-test-pressure-hint', 'Изменение пока не зафиксировано, повторите продувку чуть сильнее.');
    }

    if (summary) {
        summary.innerHTML = `
            <div class="equipment-inline-stat"><span>База</span><strong>${formatNumber(state.pressureTest.baseline, 1, ' мм')}</strong></div>
            <div class="equipment-inline-stat"><span>Δ</span><strong>${delta === null ? '--' : formatNumber(delta, 1, ' мм')}</strong></div>
            <div class="equipment-inline-stat"><span>Диапазон</span><strong>${formatNumber(state.pressureTest.min, 1, '')} .. ${formatNumber(state.pressureTest.max, 1, ' мм')}</strong></div>
        `;
    }
}

function renderPressureStatusLegacy(data) {
    const badge = byId('equipment-test-pressure-badge');
    const summary = byId('equipment-test-pressure-summary');
    if (!data) {
        updateBadge(badge, 'Нет данных', 'danger');
        setText('equipment-test-pressure-value', '-- мм рт.ст.');
        setText('equipment-test-pressure-hint', 'Нет актуальных значений давления');
        if (summary) summary.innerHTML = '';
        return;
    }

    const ads1115Available = data.ads1115Available !== false;
    const sourceLabel = String(data.source || 'ADS1115 A1');
    updateBadge(
        badge,
        !ads1115Available ? 'ADS1115 off' : data.ok ? 'Онлайн' : 'Нет сигнала',
        !ads1115Available ? 'danger' : data.ok ? 'success' : 'danger'
    );
    setText('equipment-test-pressure-value', formatNumber(data.cubeMmHg, 1, ' мм рт.ст.'));

    if (!state.pressureTest.active) {
        setText(
            'equipment-test-pressure-hint',
            ads1115Available
                ? 'Для теста нажмите кнопку ниже и слегка подуйте в датчик.'
                : 'ESP32-S3 сейчас не видит ADS1115 на I2C. Проверьте питание, SDA, SCL и адрес 0x48.'
        );
        if (summary) {
            summary.innerHTML = `
                <div class="equipment-inline-stat"><span>ADC</span><strong>${ads1115Available ? sourceLabel : `${sourceLabel} off`}</strong></div>
                <div class="equipment-inline-stat"><span>Voltage</span><strong>${formatNumber(data.sensorVoltage, 3, ' V')}</strong></div>
                <div class="equipment-inline-stat"><span>ADC raw</span><strong>${Number.isFinite(Number(data.sensorAdc)) ? String(Math.round(Number(data.sensorAdc))) : '--'}</strong></div>
                <div class="equipment-inline-stat"><span>Атмосфера</span><strong>${formatNumber(data.atmosphere, 1, ' гПа')}</strong></div>
                <div class="equipment-inline-stat"><span>Сигнал</span><strong>${formatBool(data.ok, 'Есть', 'Нет')}</strong></div>
            `;
        }
        return;
    }

    if (state.pressureTest.baseline === null && Number.isFinite(Number(data.cubeMmHg))) {
        state.pressureTest.baseline = Number(data.cubeMmHg);
        state.pressureTest.min = Number(data.cubeMmHg);
        state.pressureTest.max = Number(data.cubeMmHg);
    }

    const current = Number(data.cubeMmHg);
    if (Number.isFinite(current)) {
        state.pressureTest.min = state.pressureTest.min === null ? current : Math.min(state.pressureTest.min, current);
        state.pressureTest.max = state.pressureTest.max === null ? current : Math.max(state.pressureTest.max, current);
        if (state.pressureTest.baseline !== null &&
            Math.abs(current - state.pressureTest.baseline) >= PRESSURE_SUCCESS_DELTA) {
            state.pressureTest.success = true;
        }
    }

    const delta = state.pressureTest.baseline === null || !Number.isFinite(current)
        ? null
        : current - state.pressureTest.baseline;

    if (!ads1115Available) {
        setText('equipment-test-pressure-hint', 'Тест продувки бессмысленен, пока ADS1115 не появился на I2C.');
    } else if (state.pressureTest.success) {
        setText('equipment-test-pressure-hint', 'Изменение давления обнаружено, датчик реагирует.');
    } else {
        setText('equipment-test-pressure-hint', 'Изменение пока не зафиксировано, повторите продувку чуть сильнее.');
    }

    if (summary) {
        summary.innerHTML = `
            <div class="equipment-inline-stat"><span>ADC</span><strong>${ads1115Available ? sourceLabel : `${sourceLabel} off`}</strong></div>
            <div class="equipment-inline-stat"><span>Voltage</span><strong>${formatNumber(data.sensorVoltage, 3, ' V')}</strong></div>
            <div class="equipment-inline-stat"><span>ADC raw</span><strong>${Number.isFinite(Number(data.sensorAdc)) ? String(Math.round(Number(data.sensorAdc))) : '--'}</strong></div>
            <div class="equipment-inline-stat"><span>База</span><strong>${formatNumber(state.pressureTest.baseline, 1, ' мм')}</strong></div>
            <div class="equipment-inline-stat"><span>Δ</span><strong>${delta === null ? '--' : formatNumber(delta, 1, ' мм')}</strong></div>
            <div class="equipment-inline-stat"><span>Диапазон</span><strong>${formatNumber(state.pressureTest.min, 1, '')} .. ${formatNumber(state.pressureTest.max, 1, ' мм')}</strong></div>
        `;
    }
}

function renderHydrometerStatus(hydrometer) {
    updateBadge(
        byId('equipment-test-hydrometer-badge'),
        hydrometer?.valid ? 'Есть данные' : 'Нет сигнала',
        hydrometer?.valid ? 'success' : 'muted'
    );
    setText('equipment-test-hydrometer-pressure', formatNumber(hydrometer?.pressure, 1, ''));
    setText('equipment-test-hydrometer-density', formatNumber(hydrometer?.density, 2, ''));
    setText('equipment-test-hydrometer-abv', formatNumber(hydrometer?.abv, 1, ' %'));
}

function renderPowerStatus(power) {
    setText('equipment-test-power-voltage', formatNumber(power?.voltage, 1, ' В'));
    setText('equipment-test-power-current', formatNumber(power?.current, 2, ' А'));
    setText('equipment-test-power-real', formatNumber(power?.power, 0, ' Вт'));
    setText('equipment-test-power-energy', formatNumber(power?.energy, 3, ' кВт·ч'));
    setText('equipment-test-power-frequency', formatNumber(power?.frequency, 1, ' Гц'));
    setText('equipment-test-power-pf', formatNumber(power?.powerFactor, 2, ''));
}

function renderRecentActions(actions = []) {
    const host = byId('equipment-test-action-journal');
    if (!host) return;

    if (!Array.isArray(actions) || !actions.length) {
        host.innerHTML = '<div class="equipment-test-journal-empty">Сервисных действий пока не было.</div>';
        return;
    }

    host.innerHTML = actions.map((action, index) => `
        <div class="equipment-test-journal-item ${action?.tone || 'neutral'}">
            <div class="equipment-test-journal-line">
                <strong>${action?.title || 'Сервисное действие'}</strong>
                <span>${formatActionAge(action?.timestampMs, index)}</span>
            </div>
            <div class="equipment-test-journal-detail">${action?.detail || ''}</div>
        </div>
    `).join('');
}

function formatActionAge(timestampMs, index) {
    const ts = Number(timestampMs || 0);
    if (!Number.isFinite(ts) || ts <= 0) {
        return index === 0 ? 'только что' : `#${index + 1}`;
    }

    const totalSec = Math.max(0, Math.floor(ts / 1000));
    const minutes = Math.floor(totalSec / 60);
    const seconds = totalSec % 60;
    return index === 0
        ? 'только что'
        : `T+${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
}

function formatWallClock(timestampMs) {
    const date = new Date(Number(timestampMs || 0));
    if (Number.isNaN(date.getTime())) {
        return '—';
    }
    return date.toLocaleTimeString('ru-RU', {
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

function renderPumpStatus(pump, testingAllowed, demoMode) {
    updateBadge(
        byId('equipment-test-pump-badge'),
        pump?.running ? (demoMode ? 'Симуляция' : 'Работает') : 'Ожидание',
        pump?.running ? (demoMode ? 'warning' : 'success') : 'muted'
    );
    setText('equipment-test-pump-target', formatNumber(pump?.targetSpeedMlH, 0, ' мл/ч'));
    setText('equipment-test-pump-applied', formatNumber(pump?.appliedSpeedMlH, 0, ' мл/ч'));
    setText('equipment-test-pump-volume', formatNumber(pump?.totalVolumeMl, 2, ' мл'));
    setText('equipment-test-pump-loops', String(Math.round(Number(pump?.taskLoopCount || 0))));
    setText('equipment-test-pump-locks', String(Math.round(Number(pump?.lockTimeoutCount || 0))));
    setText('equipment-test-pump-task', formatBool(pump?.taskAlive, 'Жива', 'Нет'));

    const toggleButton = byId('equipment-test-pump-toggle');
    if (toggleButton) {
        toggleButton.textContent = pump?.running ? 'Остановить насос' : 'Запустить насос';
        toggleButton.classList.toggle('btn-danger', !!pump?.running);
        toggleButton.classList.toggle('btn-success', !pump?.running);
        toggleButton.disabled = !testingAllowed && !pump?.running;
    }
}

function renderStirrerStatus(stirrer, testingAllowed, demoMode) {
    const available = !!stirrer?.available;
    const enabled = stirrer?.enabled !== false;
    const running = !!stirrer?.running;
    const autoMode = !!stirrer?.autoMode;

    updateBadge(
        byId('equipment-test-stirrer-badge'),
        !enabled ? 'Отключена' : !available ? 'Нет DAC' : running ? (demoMode ? 'Симуляция' : (autoMode ? 'Авто' : 'Работает')) : 'Готова',
        !enabled || !available ? 'danger' : running ? (autoMode || demoMode ? 'warning' : 'success') : 'muted'
    );
    setText('equipment-test-stirrer-speed-live', formatNumber(stirrer?.speed ?? stirrer?.speedPercent, 0, ' %'));
    setText('equipment-test-stirrer-mode', !enabled ? 'Выкл' : running ? (autoMode ? 'Авто' : 'Ручной') : 'Ожидание');
    setText('equipment-test-stirrer-available', available ? 'MCP4725 OK' : 'Нет MCP4725');

    const hint = byId('equipment-test-stirrer-hint');
    if (hint) {
        if (!enabled) {
            hint.textContent = 'Включите мешалку в параметрах оборудования.';
        } else if (!available) {
            hint.textContent = 'Контроллер MCP4725 недоступен на I2C.';
        } else if (running && autoMode) {
            hint.textContent = 'Скорость управляется автоматически из FSM.';
        } else if (running) {
            hint.textContent = 'Ручной тест мешалки активен.';
        } else {
            hint.textContent = 'Готова к запуску из сервисного экрана.';
        }
    }

    const speedInput = byId('equipment-test-stirrer-speed');
    if (speedInput && !speedInput.matches(':focus') && state.pendingStirrerSpeed == null) {
        const nextSpeed = running
            ? clamp(stirrer?.speed ?? stirrer?.speedPercent, 1, 100, stirrer?.defaultSpeedPercent || 50)
            : clamp(stirrer?.defaultSpeedPercent, 1, 100, 50);
        speedInput.value = String(nextSpeed);
    }

    const canControl = testingAllowed && enabled && available;

    const startButton = byId('equipment-test-stirrer-start');
    if (startButton) startButton.disabled = !canControl || running;

    const applyButton = byId('equipment-test-stirrer-apply');
    if (applyButton) applyButton.disabled = !canControl || !running;

    const stopButton = byId('equipment-test-stirrer-stop');
    if (stopButton) stopButton.disabled = !running;
}

function renderValveButtonState(id, open, label, testingAllowed, pulse, demoMode) {
    const button = byId(id);
    const badge = byId(`${id}-badge`);
    const pulseActive = !!pulse?.active;
    const remainingMs = Number(pulse?.remainingMs || 0);
    updateBadge(
        badge,
        pulseActive ? 'Импульс' : open ? 'Открыт' : 'Закрыт',
        pulseActive ? 'warning' : open ? 'success' : 'muted'
    );
    if (button) {
        button.textContent = open ? `Закрыть ${label}` : `Открыть ${label}`;
        button.dataset.nextOpen = open ? 'false' : 'true';
        button.disabled = !testingAllowed && !open;
    }

    const pulseButton = byId(id.replace('-toggle', '-pulse'));
    if (pulseButton) {
        pulseButton.disabled = !testingAllowed || pulseActive;
    }

    const hint = byId(`${id}-pulse-hint`);
    if (hint) {
        if (pulseActive) {
            hint.textContent = `${demoMode ? 'Симуляция' : 'Импульс'}: автозакрытие через ${formatNumber(remainingMs / 1000, 1, ' с')}`;
        } else {
            hint.textContent = 'Импульсный тест готов.';
        }
    }
}

function renderStartStopPwmStatus(valves, coolingSettings, testingAllowed, demoMode) {
    const badge = byId('equipment-test-start-stop-badge');
    const input = byId('equipment-test-start-stop-duty');
    const applyButton = byId('equipment-test-start-stop-apply');
    const startupButton = byId('equipment-test-start-stop-startup');
    const stopButton = byId('equipment-test-start-stop-stop');
    const hint = byId('equipment-test-start-stop-hint');

    const enabled = Boolean(coolingSettings?.enabled);
    const minDuty = clamp(coolingSettings?.minDuty, 0, 255, 0);
    const maxDuty = clamp(coolingSettings?.maxDuty, minDuty, 255, 255);
    const startupDuty = clamp(coolingSettings?.startupDuty, minDuty, maxDuty, minDuty);
    const currentDuty = clamp(valves?.startStopDuty, 0, 255, 0);
    const active = currentDuty > 0;

    updateBadge(
        badge,
        !enabled ? 'Отключен' : active ? `${currentDuty}/255` : 'Остановлен',
        !enabled ? 'muted' : active ? (demoMode ? 'warning' : 'success') : 'muted'
    );

    if (input && !input.matches(':focus')) {
        input.value = String(active ? currentDuty : startupDuty);
    }

    if (hint) {
        hint.textContent = !enabled
            ? 'PWM-канал охлаждения отключен в параметрах оборудования.'
            : `Рабочее окно ${minDuty}-${maxDuty}/255, стартовая подача ${startupDuty}/255.` +
                (active ? ` Сейчас ${demoMode ? 'симуляция' : 'подача'} ${currentDuty}/255.` : ' Канал на нуле.');
    }

    const canControl = testingAllowed && enabled;
    if (input) input.disabled = !canControl;
    if (applyButton) applyButton.disabled = !canControl;
    if (startupButton) startupButton.disabled = !canControl;
    if (stopButton) stopButton.disabled = !active;
}

function renderServoStatus(servo, testingAllowed) {
    updateBadge(
        byId('equipment-test-servo-badge'),
        !servo?.enabled ? 'Отключен' : servo?.moving ? 'Движение' : 'Готов',
        !servo?.enabled ? 'muted' : servo?.moving ? 'warning' : 'success'
    );

    setText('equipment-test-servo-fraction', servo?.fractionLabel || '—');
    setText('equipment-test-servo-angle-live', formatNumber(servo?.angle, 0, '°'));
    setText('equipment-test-servo-status', servo?.available ? 'Сервопривод доступен' : 'Фракционник отключен в конфигурации');

    qsa('[data-servo-preset]').forEach((button) => {
        const token = button.dataset.servoPreset;
        const preset = (servo?.presets || []).find((item) => item.token === token);
        button.disabled = !testingAllowed || !servo?.available || !preset?.enabled;
        button.classList.toggle('is-active', servo?.fraction === token);
        const angleEl = button.querySelector('.equipment-preset-angle');
        if (angleEl && preset) {
            angleEl.textContent = `${preset.angle}°`;
        }
    });

    const angleInput = byId('equipment-test-servo-angle');
    const angleButton = byId('equipment-test-servo-angle-apply');
    if (angleInput) {
        angleInput.disabled = !testingAllowed || !servo?.available;
        if (!angleInput.matches(':focus')) {
            angleInput.value = Number.isFinite(Number(servo?.angle)) ? String(Math.round(Number(servo.angle))) : '0';
        }
    }
    if (angleButton) {
        angleButton.disabled = !testingAllowed || !servo?.available;
    }

    for (const preset of servo?.presets || []) {
        const enabledEl = byId(`equipment-servo-enabled-${preset.index}`);
        const angleEl = byId(`equipment-servo-angle-${preset.index}`);
        if (enabledEl) enabledEl.checked = !!preset.enabled;
        if (angleEl && !angleEl.matches(':focus')) angleEl.value = String(Math.round(Number(preset.angle || 0)));
    }

    const saveButton = byId('equipment-test-servo-save');
    if (saveButton) {
        saveButton.disabled = !servo?.enabled;
    }
}

function renderHeaterStatus(heater, power, testingAllowed, demoMode) {
    updateBadge(
        byId('equipment-test-heater-badge'),
        heater?.active ? (demoMode ? 'Симуляция' : 'Вкл') : 'Выкл',
        heater?.active ? (demoMode ? 'warning' : 'danger') : 'muted'
    );
    setText('equipment-test-heater-power', formatNumber(heater?.powerPercent, 0, ' %'));
    setText('equipment-test-heater-setpoint', formatNumber(heater?.powerSetPercent, 0, ' %'));
    setText('equipment-test-heater-submerge', formatNumber(heater?.minSubmergeLiters, 1, ' л'));
    setText('equipment-test-heater-main-power', formatNumber(heater?.mainPowerW, 0, ' Вт'));
    setText(
        'equipment-test-heater-booster-power',
        heater?.boosterConfigured ? formatNumber(heater?.boosterPowerW, 0, ' Вт') : 'Отключен'
    );
    setText(
        'equipment-test-heater-booster-stop',
        heater?.boosterConfigured ? formatNumber(heater?.boosterStopCubeTempC, 1, ' °C') : '—'
    );
    setText('equipment-test-heater-delay', heater?.backend === 'triac'
        ? formatNumber(heater?.triacDelayUs, 0, ' мкс')
        : '—');
    setText('equipment-test-heater-zc-count', heater?.backend === 'triac'
        ? formatNumber(heater?.zeroCrossCount, 0)
        : '—');
    setText('equipment-test-heater-fire-count', heater?.backend === 'triac'
        ? formatNumber(heater?.triacFireCount, 0)
        : '—');
    setText(
        'equipment-test-heater-real-power',
        power?.available ? formatNumber(power?.power, 0, ' Вт') : 'PZEM offline'
    );
    setText(
        'equipment-test-heater-voltage',
        power?.available ? formatNumber(power?.voltage, 1, ' В') : '—'
    );
    setText(
        'equipment-test-heater-current',
        power?.available ? formatNumber(power?.current, 2, ' А') : '—'
    );
    setText(
        'equipment-test-heater-frequency',
        power?.available ? formatNumber(power?.frequency, 1, ' Гц') : '—'
    );
    setText(
        'equipment-test-heater-pf',
        power?.available ? formatNumber(power?.powerFactor, 2, '') : '—'
    );

    const backendTriac = heater?.backend === 'triac';
    const zeroCrossSeen = Boolean(heater?.zeroCrossSeen);
    updateBadge(
        byId('equipment-test-heater-backend'),
        backendTriac ? 'Основной: TRIAC' : 'Основной: SSR',
        backendTriac ? 'neutral' : 'warning'
    );
    updateBadge(
        byId('equipment-test-heater-booster'),
        heater?.boosterEnabled ? 'Booster SSR: вкл' : 'Booster SSR: выкл',
        heater?.boosterEnabled ? (demoMode ? 'warning' : 'danger') : 'muted'
    );
    updateBadge(
        byId('equipment-test-heater-zc'),
        backendTriac
            ? (zeroCrossSeen ? 'Zero-cross: есть' : 'Zero-cross: нет')
            : 'Zero-cross: не нужен',
        backendTriac
            ? (zeroCrossSeen ? 'success' : 'danger')
            : 'muted'
    );
    updateBadge(
        byId('equipment-test-heater-pzem'),
        power?.available ? 'PZEM: онлайн' : 'PZEM: offline',
        power?.available ? 'success' : 'danger'
    );

    const diagEl = byId('equipment-test-heater-diag');
    if (diagEl) {
        let tone = 'subtle';
        let text = 'Ожидаем данные по контуру нагрева.';

        if (backendTriac) {
            if (zeroCrossSeen) {
                text = `ESP32 видит zero-cross. Симистор работает по фазовой задержке ${formatNumber(heater?.triacDelayUs, 0, ' мкс')}, зафиксировано ${formatNumber(heater?.zeroCrossCount, 0)} переходов и ${formatNumber(heater?.triacFireCount, 0)} gate pulses.`;
            } else {
                tone = 'danger';
                text = 'ESP32 не видит zero-cross. Основной симисторный канал сейчас не подтверждает синхронизацию с сетью.';
            }
        } else if (heater?.backend) {
            text = 'Основной нагрев сейчас работает не через симисторный backend. Для фазового управления нужен активный TRIAC backend.';
        }

        if (power?.available) {
            text += ` PZEM видит ${formatNumber(power?.power, 0, ' Вт')} при сети ${formatNumber(power?.voltage, 1, ' В')}.`;
        } else {
            text += ' PZEM сейчас не подтверждает силовые измерения.';
        }

        if (demoMode) {
            tone = 'subtle';
            text = 'Демо-режим: статусы нагрева и zero-cross показываются как симуляция, без проверки реальной сети.';
        }

        diagEl.textContent = text;
        diagEl.className = `equipment-test-alert${tone === 'subtle' ? ' subtle' : tone === 'danger' ? ' danger' : ''}`;
    }

    const startButton = byId('equipment-test-heater-start');
    const stopButton = byId('equipment-test-heater-stop');
    if (startButton) startButton.disabled = !testingAllowed || !!heater?.active;
    if (stopButton) stopButton.disabled = !heater?.active;
}

function renderServiceSummary(status) {
    updateBadge(
        byId('equipment-test-allow-badge'),
        status.testingAllowed ? 'Доступно' : 'Заблокировано',
        status.testingAllowed ? 'success' : 'danger'
    );
    updateBadge(
        byId('equipment-test-demo-badge'),
        status.demoMode ? 'Симуляция' : 'Живой контур',
        status.demoMode ? 'warning' : 'success'
    );
    updateBadge(
        byId('equipment-test-process-badge'),
        status.processActive ? 'Процесс активен' : 'Простой',
        status.processActive ? 'warning' : 'success'
    );
    updateBadge(
        byId('equipment-test-alarm-badge'),
        status.alarmActive || status.safetyLatched ? 'Есть авария' : 'Чисто',
        status.alarmActive || status.safetyLatched ? 'danger' : 'success'
    );
    setHtml(
        'equipment-test-active-summary',
        `
            <div class="equipment-inline-stat"><span>Физическое управление</span><strong>${formatBool(status.physicalActuationAllowed, 'Да', 'Нет')}</strong></div>
            <div class="equipment-inline-stat"><span>Сейчас активны</span><strong>${getActiveTestsSummary(status.activeTests)}</strong></div>
        `
    );
    setText(
        'equipment-test-availability-hint',
        status.testingAllowed
            ? (status.demoMode
                ? 'Демо-режим активен: тесты разрешены, но физическое железо не затрагивается.'
                : 'Сервисные тесты разрешены. Следи за блокировками и останавливай узлы после проверки.')
            : (status.availabilityReason || 'Тестирование сейчас недоступно.')
    );
}

function renderTestingStatus(status) {
    state.lastStatus = status;
    recordSelfCheckSnapshot(status);
    renderServiceSummary(status);
    renderServiceContour(status);
    renderCommissioningWizard(status);
    renderHardwareSelfCheck(status);
    renderPumpStatus(status.pump, status.testingAllowed, status.demoMode);
    renderStirrerStatus(status.stirrer, status.testingAllowed, status.demoMode);
    renderValveButtonState('equipment-test-water-toggle', !!status.valves?.water, 'воду', status.testingAllowed, status.valves?.waterPulse, status.demoMode);
    renderValveButtonState('equipment-test-heads-toggle', !!status.valves?.heads, 'головы', status.testingAllowed, status.valves?.headsPulse, status.demoMode);
    renderValveButtonState('equipment-test-uno-toggle', !!status.valves?.uno, 'УНО', status.testingAllowed, status.valves?.unoPulse, status.demoMode);
    renderStartStopPwmStatus(status.valves, status.coolingSettings, status.testingAllowed, status.demoMode);
    renderServoStatus(status.servo, status.testingAllowed);
    renderHeaterStatus(status.heater, status.power, status.testingAllowed, status.demoMode);
    renderTemperatureList(status.temperatures);
    renderPressureStatus(status.pressure);
    renderHydrometerStatus(status.hydrometer);
    renderRecentActions(status.recentActions);

    const stopAllButton = byId('equipment-test-stop-all');
    if (stopAllButton) {
        const hasActiveTests = Object.values(status.activeTests || {}).some(Boolean);
        stopAllButton.disabled = !hasActiveTests;
    }

    const closeValvesButton = byId('equipment-test-valves-close-all');
    if (closeValvesButton) {
        const hasOpenValve = !!status.valves?.water || !!status.valves?.heads || !!status.valves?.uno || Number(status.valves?.startStopDuty || 0) > 0;
        closeValvesButton.disabled = !status.testingAllowed && !hasOpenValve;
    }
}

async function requestJson(url, options = {}) {
    const response = await fetch(url, options);
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
        const message = payload?.message || payload?.error || `HTTP ${response.status}`;
        throw new Error(message);
    }
    return payload;
}

export async function refreshEquipmentTestingStatus(silent = false) {
    try {
        const status = await requestJson(TESTING_STATUS_URL);
        renderTestingStatus(status);
    } catch (error) {
        if (!silent) {
            addLog(`✗ Ошибка загрузки сервиса тестирования: ${error.message}`, 'error');
        }
        updateBadge(byId('equipment-test-allow-badge'), 'Ошибка', 'danger');
        setText('equipment-test-availability-hint', `Не удалось загрузить статус тестирования: ${error.message}`);
    }
}

function ensurePolling() {
    if (state.pollingHandle) return;
    state.pollingHandle = window.setInterval(() => {
        if (!isEquipmentTestingVisible()) return;
        void refreshEquipmentTestingStatus(true);
    }, TESTING_POLL_MS);
}

function openHeaterConfirmModal(powerPercent) {
    state.heaterPendingPower = powerPercent;
    setText('equipment-test-heater-confirm-power', `${powerPercent}%`);
    byId('equipment-heater-confirm-modal')?.classList.add('active');
}

function closeHeaterConfirmModal() {
    state.heaterPendingPower = 0;
    byId('equipment-heater-confirm-modal')?.classList.remove('active');
}

async function handlePumpToggle() {
    const running = !!state.lastStatus?.pump?.running;
    const payload = running
        ? { action: 'stop' }
        : { action: 'start', speedMlH: clamp(byId('equipment-test-pump-speed')?.value, 1, 5000, 1200) };
    const status = await requestJson('/api/testing/pump', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
    setCommissioningManualCheck('pump', true);
    renderTestingStatus(status);
    addLog(running ? 'Остановлен тест насоса' : `Запущен тест насоса: ${payload.speedMlH} мл/ч`, 'success');
}

async function handleStirrerAction(action) {
    const speedPercent = clamp(
        state.pendingStirrerSpeed ?? byId('equipment-test-stirrer-speed')?.value,
        1,
        100,
        Number(state.lastStatus?.stirrer?.defaultSpeedPercent || 50)
    );
    const payload = action === 'stop' ? { action } : { action, speedPercent };
    if (action !== 'stop') {
        state.pendingStirrerSpeed = speedPercent;
    } else {
        state.pendingStirrerSpeed = null;
    }
    const status = await requestJson('/api/testing/stirrer', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    });
    state.pendingStirrerSpeed = null;
    renderTestingStatus(status);

    if (action === 'start') {
        addLog(`Запущен тест мешалки: ${speedPercent}%`, 'success');
    } else if (action === 'set') {
        addLog(`Скорость мешалки изменена: ${speedPercent}%`, 'info');
    } else {
        addLog('Мешалка остановлена из сервисного экрана', 'warning');
    }
}

async function handleValveToggle(target, nextOpen) {
    const status = await requestJson('/api/testing/valves', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ target, open: nextOpen })
    });
    if (target === 'water' || target === 'heads' || target === 'uno' || target === 'all') {
        setCommissioningManualCheck('valves', true);
    }
    renderTestingStatus(status);
    addLog(`Клапан ${target} ${nextOpen ? 'открыт' : 'закрыт'} через тестовый экран`, 'info');
}

async function handleValvePulse(target) {
    const durationMs = clamp(byId('equipment-test-valve-pulse-duration')?.value, 100, 10000, 1200);
    const status = await requestJson('/api/testing/valves', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ target, action: 'pulse', durationMs })
    });
    setCommissioningManualCheck('valves', true);
    renderTestingStatus(status);
    addLog(`Импульс клапана ${target}: ${durationMs} мс`, 'info');
}

async function handleStartStopDutySet(duty) {
    const nextDuty = clamp(duty, 0, 255, 0);
    const status = await requestJson('/api/testing/valves', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ target: 'startStop', duty: nextDuty })
    });
    renderTestingStatus(status);
    addLog(`PWM-канал охлаждения установлен на ${nextDuty}/255`, nextDuty > 0 ? 'info' : 'warning');
}

async function handleStopAll() {
    const status = await requestJson('/api/testing/stop-all', { method: 'POST' });
    addLog('Все сервисные тесты остановлены', 'warning');
    await refreshEquipmentTestingStatus(true);
    return status;
}

async function handleServoPreset(preset) {
    const status = await requestJson('/api/testing/servo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'preset', preset })
    });
    renderTestingStatus(status);
    addLog(`Сервопривод переведен в позицию ${preset}`, 'success');
}

async function handleServoAngleApply() {
    const angle = clamp(byId('equipment-test-servo-angle')?.value, 0, 180, 0);
    const status = await requestJson('/api/testing/servo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'angle', angle })
    });
    renderTestingStatus(status);
    addLog(`Сервопривод переведен в ручной угол ${angle}°`, 'success');
}

async function handleServoSave() {
    const angles = [];
    const enabled = [];
    for (let index = 0; index < 5; index += 1) {
        angles.push(clamp(byId(`equipment-servo-angle-${index}`)?.value, 0, 180, 0));
        enabled.push(!!byId(`equipment-servo-enabled-${index}`)?.checked);
    }

    const status = await requestJson('/api/testing/servo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'saveConfig', angles, enabled })
    });
    renderTestingStatus(status);
    addLog('Позиции сервопривода сохранены', 'success');
}

async function confirmHeaterStart() {
    const status = await requestJson('/api/testing/heater', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            action: 'start',
            powerPercent: state.heaterPendingPower,
            confirmed: true
        })
    });
    closeHeaterConfirmModal();
    setCommissioningManualCheck('heater', true);
    renderTestingStatus(status);
    addLog(`Тест ТЭНа запущен на ${state.heaterPendingPower}%`, 'warning');
}

async function stopHeater() {
    const status = await requestJson('/api/testing/heater', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'stop' })
    });
    setCommissioningManualCheck('heater', true);
    renderTestingStatus(status);
    addLog('ТЭН остановлен из тестового экрана', 'warning');
}

function startPressureTest() {
    state.pressureTest = {
        active: true,
        baseline: null,
        min: null,
        max: null,
        success: false
    };
    const button = byId('equipment-test-pressure-start');
    if (button) {
        button.textContent = 'Сбросить тест продувки';
    }
    renderPressureStatus(state.lastStatus?.pressure);
}

function resetPressureTest() {
    state.pressureTest = {
        active: false,
        baseline: null,
        min: null,
        max: null,
        success: false
    };
    const button = byId('equipment-test-pressure-start');
    if (button) {
        button.textContent = 'Начать тест продувки';
    }
    renderPressureStatus(state.lastStatus?.pressure);
}

function bindSectionNav() {
    qsa('[data-equipment-section-btn]').forEach((button) => {
        button.addEventListener('click', () => {
            const target = button.dataset.equipmentSectionBtn;
            setEquipmentSection(target);
        });
    });
}

function bindTestingActions() {
    byId('equipment-test-pump-toggle')?.addEventListener('click', () => {
        void handlePumpToggle().catch((error) => addLog(`✗ Насос: ${error.message}`, 'error'));
    });

    qsa('[data-pump-speed-preset]').forEach((button) => {
        button.addEventListener('click', () => {
            const speed = button.dataset.pumpSpeedPreset;
            const input = byId('equipment-test-pump-speed');
            if (input) input.value = String(speed);
        });
    });

    qsa('[data-stirrer-speed-preset]').forEach((button) => {
        button.addEventListener('click', () => {
            const speed = button.dataset.stirrerSpeedPreset;
            const input = byId('equipment-test-stirrer-speed');
            state.pendingStirrerSpeed = clamp(speed, 1, 100, 50);
            if (input) input.value = String(state.pendingStirrerSpeed);
        });
    });

    byId('equipment-test-stirrer-speed')?.addEventListener('input', (event) => {
        state.pendingStirrerSpeed = clamp(event.currentTarget?.value, 1, 100, 50);
    });

    byId('equipment-test-stirrer-start')?.addEventListener('click', () => {
        void handleStirrerAction('start').catch((error) => addLog(`✗ Мешалка: ${error.message}`, 'error'));
    });
    byId('equipment-test-stirrer-apply')?.addEventListener('click', () => {
        void handleStirrerAction('set').catch((error) => addLog(`✗ Мешалка: ${error.message}`, 'error'));
    });
    byId('equipment-test-stirrer-stop')?.addEventListener('click', () => {
        void handleStirrerAction('stop').catch((error) => addLog(`✗ Мешалка: ${error.message}`, 'error'));
    });

    byId('equipment-test-stop-all')?.addEventListener('click', () => {
        void handleStopAll().catch((error) => addLog(`✗ Остановка тестов: ${error.message}`, 'error'));
    });

    byId('equipment-test-water-toggle')?.addEventListener('click', (event) => {
        const nextOpen = event.currentTarget.dataset.nextOpen === 'true';
        void handleValveToggle('water', nextOpen).catch((error) => addLog(`✗ Клапан воды: ${error.message}`, 'error'));
    });
    byId('equipment-test-water-pulse')?.addEventListener('click', () => {
        void handleValvePulse('water').catch((error) => addLog(`✗ Импульс воды: ${error.message}`, 'error'));
    });
    byId('equipment-test-heads-toggle')?.addEventListener('click', (event) => {
        const nextOpen = event.currentTarget.dataset.nextOpen === 'true';
        void handleValveToggle('heads', nextOpen).catch((error) => addLog(`✗ Клапан голов: ${error.message}`, 'error'));
    });
    byId('equipment-test-heads-pulse')?.addEventListener('click', () => {
        void handleValvePulse('heads').catch((error) => addLog(`✗ Импульс голов: ${error.message}`, 'error'));
    });
    byId('equipment-test-uno-toggle')?.addEventListener('click', (event) => {
        const nextOpen = event.currentTarget.dataset.nextOpen === 'true';
        void handleValveToggle('uno', nextOpen).catch((error) => addLog(`✗ Клапан УНО: ${error.message}`, 'error'));
    });
    byId('equipment-test-uno-pulse')?.addEventListener('click', () => {
        void handleValvePulse('uno').catch((error) => addLog(`✗ Импульс УНО: ${error.message}`, 'error'));
    });
    byId('equipment-test-start-stop-apply')?.addEventListener('click', () => {
        const duty = clamp(byId('equipment-test-start-stop-duty')?.value, 0, 255, 0);
        void handleStartStopDutySet(duty).catch((error) => addLog(`✗ PWM охлаждения: ${error.message}`, 'error'));
    });
    byId('equipment-test-start-stop-startup')?.addEventListener('click', () => {
        const duty = clamp(state.lastStatus?.coolingSettings?.startupDuty, 0, 255, 0);
        void handleStartStopDutySet(duty).catch((error) => addLog(`✗ PWM охлаждения: ${error.message}`, 'error'));
    });
    byId('equipment-test-start-stop-stop')?.addEventListener('click', () => {
        void handleStartStopDutySet(0).catch((error) => addLog(`✗ PWM охлаждения: ${error.message}`, 'error'));
    });
    byId('equipment-test-valves-close-all')?.addEventListener('click', () => {
        void handleValveToggle('all', false).catch((error) => addLog(`✗ Клапаны: ${error.message}`, 'error'));
    });

    qsa('[data-servo-preset]').forEach((button) => {
        button.addEventListener('click', () => {
            void handleServoPreset(button.dataset.servoPreset).catch((error) => addLog(`✗ Сервопривод: ${error.message}`, 'error'));
        });
    });

    byId('equipment-test-servo-angle-apply')?.addEventListener('click', () => {
        void handleServoAngleApply().catch((error) => addLog(`✗ Сервопривод: ${error.message}`, 'error'));
    });

    byId('equipment-test-servo-save')?.addEventListener('click', () => {
        void handleServoSave().catch((error) => addLog(`✗ Сохранение сервопривода: ${error.message}`, 'error'));
    });

    byId('equipment-test-heater-start')?.addEventListener('click', () => {
        const power = clamp(byId('equipment-test-heater-power-input')?.value, 1, 100, 40);
        openHeaterConfirmModal(power);
    });

    byId('equipment-test-heater-stop')?.addEventListener('click', () => {
        void stopHeater().catch((error) => addLog(`✗ ТЭН: ${error.message}`, 'error'));
    });

    byId('equipment-test-heater-confirm')?.addEventListener('click', () => {
        void confirmHeaterStart().catch((error) => addLog(`✗ ТЭН: ${error.message}`, 'error'));
    });
    byId('equipment-test-heater-cancel')?.addEventListener('click', closeHeaterConfirmModal);
    byId('equipment-test-heater-close')?.addEventListener('click', closeHeaterConfirmModal);

    byId('equipment-heater-confirm-modal')?.addEventListener('click', (event) => {
        if (event.target === event.currentTarget) {
            closeHeaterConfirmModal();
        }
    });

    byId('equipment-test-temps-refresh')?.addEventListener('click', () => {
        void refreshEquipmentTestingStatus().catch(() => {});
    });
    byId('equipment-test-temps-open-calibration')?.addEventListener('click', () => {
        setEquipmentSection('calibration');
        setEquipmentPaneCard('calibration', 'temp-calibration');
    });
    byId('equipment-test-pump-open-calibration')?.addEventListener('click', () => {
        setEquipmentSection('calibration');
        setEquipmentPaneCard('calibration', 'pump-calibration');
    });
    byId('equipment-test-hydrometer-open-calibration')?.addEventListener('click', () => {
        setEquipmentSection('calibration');
        setEquipmentPaneCard('calibration', 'hydrometer-calibration');
    });
    byId('equipment-selfcheck-open-modules')?.addEventListener('click', () => {
        setEquipmentSection('parameters');
        setEquipmentPaneCard('parameters', 'hardware-status');
    });
    byId('equipment-selfcheck-open-temps')?.addEventListener('click', () => {
        setEquipmentSection('testing');
        setTestingCard('temps');
    });

    byId('equipment-test-pressure-start')?.addEventListener('click', () => {
        if (state.pressureTest.active) {
            resetPressureTest();
        } else {
            startPressureTest();
        }
    });

    byId('equipment-commissioning-steps')?.addEventListener('click', (event) => {
        const openButton = event.target.closest('[data-commissioning-open]');
        if (openButton) {
            const [sectionId, cardId] = String(openButton.dataset.commissioningOpen || '').split(':');
            if (sectionId && cardId) {
                openEquipmentWorkbench(sectionId, cardId);
            }
            return;
        }

        const pressureButton = event.target.closest('[data-commissioning-pressure-toggle]');
        if (pressureButton) {
            if (state.pressureTest.active) {
                resetPressureTest();
            } else {
                startPressureTest();
            }
            return;
        }

        const manualButton = event.target.closest('[data-commissioning-manual]');
        if (manualButton) {
            const target = String(manualButton.dataset.commissioningManual || '');
            if (target) {
                setCommissioningManualCheck(target, !state.commissioning.manual[target]);
                renderCommissioningWizard(state.lastStatus || {});
            }
            return;
        }

        const resetButton = event.target.closest('[data-commissioning-manual-reset]');
        if (resetButton) {
            resetCommissioningManualChecks();
            renderCommissioningWizard(state.lastStatus || {});
        }
    });
}

export function initEquipmentTestingUi() {
    const root = byId('equipment');
    if (!root) return;

    ensureEquipmentShell();
    initEquipmentParametersWorkbench();
    initEquipmentCalibrationWorkbench();
    initEquipmentTestingWorkbench();
    ensureHeaterConfirmModal();
    initEquipmentNumberSteppers(root);
    bindSectionNav();
    bindTestingActions();
    ensurePolling();
    setEquipmentSection(readSavedSection());
    state.activeParameterCard = readSavedWorkbenchCard(
        PARAMETERS_CARD_STORAGE_KEY,
        PARAMETERS_CARD_DEFS,
        PARAMETERS_CARD_DEFS[0]?.id || 'pump-settings',
    );
    state.activeCalibrationCard = readSavedWorkbenchCard(
        CALIBRATION_CARD_STORAGE_KEY,
        CALIBRATION_CARD_DEFS,
        CALIBRATION_CARD_DEFS[0]?.id || 'pump-calibration',
    );
    state.activeTestingCard = readSavedTestingCard();
}

export function initSettingsWorkbenchUi() {
    const root = byId('settings');
    if (!root) return;

    ensureSettingsShell();
    bindSettingsSectionNav();
    setSettingsSection(readSavedSettingsSection());
}
