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
        group: 'actuators',
        icon: '⚡',
        title: 'ТЭН',
        shortTitle: 'ТЭН',
        description: 'Ручной силовой тест с обязательным подтверждением.',
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
        description: 'Текущие данные и понятная заглушка под будущий активный тест.',
    },
    {
        id: 'power',
        selector: '#equipment-test-power-voltage',
        group: 'service',
        icon: '🔌',
        title: 'Питание и силовая диагностика',
        shortTitle: 'Питание',
        description: 'Напряжение, ток, мощность, частота и cos φ рядом с тестами оборудования.',
    },
];

const PARAMETERS_GROUPS = [
    { id: 'core', label: 'Базовые узлы' },
    { id: 'process', label: 'Куб и процесс' },
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
];

const state = {
    activeSection: 'parameters',
    activeParameterCard: 'pump-settings',
    activeCalibrationCard: 'pump-calibration',
    activeTestingCard: 'pump',
    pollingHandle: null,
    lastStatus: null,
    heaterPendingPower: 0,
    pressureTest: {
        active: false,
        baseline: null,
        min: null,
        max: null,
        success: false
    },
    activeSettingsSection: 'connection'
};

const paneWorkbenchControllers = new Map();

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
                </div>
                <div class="controls equipment-actions">
                    <button class="btn btn-success" type="button" id="equipment-test-pump-toggle">Запустить насос</button>
                    <button class="btn btn-secondary" type="button" id="equipment-test-pump-open-calibration">К калибровке</button>
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
                        <h2>ТЭН</h2>
                        <p class="equipment-subtitle">Ручной силовой тест. Включение только после явного подтверждения.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-heater-badge">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Текущая мощность</span><strong id="equipment-test-heater-power">--</strong></div>
                    <div class="equipment-test-metric"><span>Сетпоинт</span><strong id="equipment-test-heater-setpoint">--</strong></div>
                    <div class="equipment-test-metric"><span>Мин. погружение</span><strong id="equipment-test-heater-submerge">--</strong></div>
                </div>
                <div class="form-group">
                    <label for="equipment-test-heater-power-input">Мощность теста, %</label>
                    <input type="number" id="equipment-test-heater-power-input" value="40" min="1" max="100" step="1" data-stepper-mode="pair" data-stepper-step="1">
                </div>
                <div class="equipment-test-alert danger">Перед стартом ТЭН должен быть полностью погружен в жидкость. Без подтверждения запуск не выполняется.</div>
                <div class="controls equipment-actions">
                    <button class="btn btn-danger" type="button" id="equipment-test-heater-start">Запустить ТЭН</button>
                    <button class="btn btn-secondary" type="button" id="equipment-test-heater-stop">Остановить ТЭН</button>
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
                        <p class="equipment-subtitle">Показываем текущие поля и оставляем место под будущий сценарий активной проверки.</p>
                    </div>
                    <span class="equipment-status-badge muted" id="equipment-test-hydrometer-badge">—</span>
                </div>
                <div class="equipment-test-metrics">
                    <div class="equipment-test-metric"><span>Давление</span><strong id="equipment-test-hydrometer-pressure">--</strong></div>
                    <div class="equipment-test-metric"><span>Плотность</span><strong id="equipment-test-hydrometer-density">--</strong></div>
                    <div class="equipment-test-metric"><span>ABV</span><strong id="equipment-test-hydrometer-abv">--</strong></div>
                </div>
                <div class="equipment-test-alert subtle">Активный сценарий проверки ареометра пока не внедрён. Здесь остаётся осмысленная инженерная заглушка, а не псевдо-функция.</div>
            </div>

            <div class="card equipment-card equipment-test-card">
                <div class="equipment-test-card-head">
                    <div>
                        <h2>Питание и силовая диагностика</h2>
                        <p class="equipment-subtitle">Быстрая электрика рядом с тестами исполнительных устройств: напряжение, ток, мощность и cos φ.</p>
                    </div>
                </div>
                <div class="equipment-test-power-grid">
                    <div class="equipment-test-metric"><span>Напряжение</span><strong id="equipment-test-power-voltage">--</strong></div>
                    <div class="equipment-test-metric"><span>Ток</span><strong id="equipment-test-power-current">--</strong></div>
                    <div class="equipment-test-metric"><span>Мощность</span><strong id="equipment-test-power-real">--</strong></div>
                    <div class="equipment-test-metric"><span>Энергия</span><strong id="equipment-test-power-energy">--</strong></div>
                    <div class="equipment-test-metric"><span>Частота</span><strong id="equipment-test-power-frequency">--</strong></div>
                    <div class="equipment-test-metric"><span>cos φ</span><strong id="equipment-test-power-pf">--</strong></div>
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

    const sidebarExtras = createElement('div', 'equipment-testing-sidebar-extras');
    nav.appendChild(sidebarExtras);

    return { nav, buttonsById, sidebarExtras };
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
    ]);

    cardsHost.innerHTML = '';
    cardsHost.append(pumpCard, cubeCard, coolingCard);
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
    if (!cards || !paramsCard || !pumpCalibrationCard || !tempCalibrationCard) return;

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
    const journal = qs('.equipment-test-journal', testingPane);

    const cards = [...grid.querySelectorAll('.equipment-test-card')];
    const resolvedCards = resolveTestingCards(cards);
    if (!resolvedCards.length) return;

    grid.dataset.testingWorkbench = '1';

    const shell = createElement('div', 'equipment-testing-shell');
    const main = createElement('div', 'equipment-testing-main');
    const desktopWrapper = createElement('div', 'equipment-testing-card-stack');
    const { nav, buttonsById, sidebarExtras } = buildTestingDesktopNav(resolvedCards);

    grid.parentNode.insertBefore(shell, statusCard || grid);
    shell.append(nav, main);
    main.appendChild(desktopWrapper);
    desktopWrapper.appendChild(grid);

    let accordion = null;
    if (journal && statusCard) {
        accordion = createElement('details', 'equipment-test-journal-accordion');
        accordion.open = false;
        accordion.innerHTML = `
            <summary class="equipment-test-journal-summary">
                <span>Последние действия оператора</span>
                <small>Журнал сервисных команд</small>
            </summary>
        `;
        accordion.appendChild(journal);
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

        if (statusCard) {
            if (isMobile) {
                if (statusCard.parentNode !== main) {
                    main.insertBefore(statusCard, desktopWrapper);
                }
            } else if (statusCard.parentNode !== sidebarExtras) {
                sidebarExtras.prepend(statusCard);
            }
        }

        if (accordion) {
            if (isMobile) {
                if (accordion.parentNode !== main) {
                    main.appendChild(accordion);
                }
            } else if (accordion.parentNode !== sidebarExtras) {
                sidebarExtras.appendChild(accordion);
            }
        }

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
    if (activeTests.heater) active.push('ТЭН');
    if (activeTests.waterValve) active.push('Вода');
    if (activeTests.headsValve) active.push('Головы');
    if (activeTests.unoValve) active.push('УНО');
    if (activeTests.servoMoving) active.push('Сервопривод');
    return active.length ? active.join(', ') : 'Ничего не включено';
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

function renderHydrometerStatus(hydrometer) {
    updateBadge(
        byId('equipment-test-hydrometer-badge'),
        hydrometer?.valid ? 'Есть данные' : 'Заглушка',
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

function renderHeaterStatus(heater, testingAllowed, demoMode) {
    updateBadge(
        byId('equipment-test-heater-badge'),
        heater?.active ? (demoMode ? 'Симуляция' : 'Вкл') : 'Выкл',
        heater?.active ? (demoMode ? 'warning' : 'danger') : 'muted'
    );
    setText('equipment-test-heater-power', formatNumber(heater?.powerPercent, 0, ' %'));
    setText('equipment-test-heater-setpoint', formatNumber(heater?.powerSetPercent, 0, ' %'));
    setText('equipment-test-heater-submerge', formatNumber(heater?.minSubmergeLiters, 1, ' л'));

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
    renderServiceSummary(status);
    renderPumpStatus(status.pump, status.testingAllowed, status.demoMode);
    renderValveButtonState('equipment-test-water-toggle', !!status.valves?.water, 'воду', status.testingAllowed, status.valves?.waterPulse, status.demoMode);
    renderValveButtonState('equipment-test-heads-toggle', !!status.valves?.heads, 'головы', status.testingAllowed, status.valves?.headsPulse, status.demoMode);
    renderValveButtonState('equipment-test-uno-toggle', !!status.valves?.uno, 'УНО', status.testingAllowed, status.valves?.unoPulse, status.demoMode);
    renderServoStatus(status.servo, status.testingAllowed);
    renderHeaterStatus(status.heater, status.testingAllowed, status.demoMode);
    renderTemperatureList(status.temperatures);
    renderPressureStatus(status.pressure);
    renderHydrometerStatus(status.hydrometer);
    renderPowerStatus(status.power);
    renderRecentActions(status.recentActions);

    const stopAllButton = byId('equipment-test-stop-all');
    if (stopAllButton) {
        const hasActiveTests = Object.values(status.activeTests || {}).some(Boolean);
        stopAllButton.disabled = !hasActiveTests;
    }

    const closeValvesButton = byId('equipment-test-valves-close-all');
    if (closeValvesButton) {
        const hasOpenValve = !!status.valves?.water || !!status.valves?.heads || !!status.valves?.uno;
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
    renderTestingStatus(status);
    addLog(running ? 'Остановлен тест насоса' : `Запущен тест насоса: ${payload.speedMlH} мл/ч`, 'success');
}

async function handleValveToggle(target, nextOpen) {
    const status = await requestJson('/api/testing/valves', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ target, open: nextOpen })
    });
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
    renderTestingStatus(status);
    addLog(`Импульс клапана ${target}: ${durationMs} мс`, 'info');
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
    renderTestingStatus(status);
    addLog(`Тест ТЭНа запущен на ${state.heaterPendingPower}%`, 'warning');
}

async function stopHeater() {
    const status = await requestJson('/api/testing/heater', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'stop' })
    });
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

    byId('equipment-test-pressure-start')?.addEventListener('click', () => {
        if (state.pressureTest.active) {
            resetPressureTest();
        } else {
            startPressureTest();
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
