// WebSocket соединение
let socket = null;
const socketUrl = `ws://${window.location.host}/ws`;

// Данные о температуре и мощности для графиков
let temperatureData = {
    labels: [],
    datasets: [
        {
            label: 'Куб',
            data: [],
            borderColor: 'rgba(255, 99, 132, 1)',
            backgroundColor: 'rgba(255, 99, 132, 0.2)',
            yAxisID: 'y-temperature',
            tension: 0.3
        },
        {
            label: 'Колонна',
            data: [],
            borderColor: 'rgba(54, 162, 235, 1)',
            backgroundColor: 'rgba(54, 162, 235, 0.2)',
            yAxisID: 'y-temperature',
            tension: 0.3
        },
        {
            label: 'Продукт',
            data: [],
            borderColor: 'rgba(75, 192, 192, 1)',
            backgroundColor: 'rgba(75, 192, 192, 0.2)',
            yAxisID: 'y-temperature',
            tension: 0.3
        },
        {
            label: 'Мощность (%)',
            data: [],
            borderColor: 'rgba(255, 159, 64, 1)',
            backgroundColor: 'rgba(255, 159, 64, 0.2)',
            yAxisID: 'y-power',
            tension: 0.3
        }
    ]
};

// График температур
let tempChart = null;

// Флаг состояния системы
let isSystemRunning = false;
let isSystemPaused = false;
let currentMode = null;

// Текущие настройки
let systemSettings = {};
let rectificationSettings = {};
let distillationSettings = {};
let pumpSettings = {};
let sensorSettings = [];

// Таймер для обновления данных графика
let chartUpdateTimer = null;

// Инициализация приложения
document.addEventListener('DOMContentLoaded', () => {
    // Инициализация интерфейса
    setupUI();
    
    // Инициализация графика
    initializeChart();
    
    // Инициализация WebSocket
    connectWebSocket();
    
    // Загрузка настроек
    loadSettings();
});

// Настройка пользовательского интерфейса
function setupUI() {
    // Навигация по разделам
    const navLinks = document.querySelectorAll('nav a');
    navLinks.forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            const targetSection = link.getAttribute('data-section');
            
            // Деактивация всех разделов и ссылок
            document.querySelectorAll('.content-section').forEach(section => {
                section.classList.remove('active');
            });
            
            navLinks.forEach(navLink => {
                navLink.classList.remove('active');
            });
            
            // Активация выбранного раздела и ссылки
            document.getElementById(targetSection).classList.add('active');
            link.classList.add('active');
        });
    });
    
    // Переключение вкладок в настройках
    const tabButtons = document.querySelectorAll('.tab-btn');
    tabButtons.forEach(button => {
        button.addEventListener('click', () => {
            const tabId = button.getAttribute('data-tab');
            
            // Деактивация всех вкладок и кнопок
            document.querySelectorAll('.tab-pane').forEach(tab => {
                tab.classList.remove('active');
            });
            
            document.querySelectorAll('.tab-btn').forEach(btn => {
                btn.classList.remove('active');
            });
            
            // Активация выбранной вкладки и кнопки
            document.getElementById(tabId).classList.add('active');
            button.classList.add('active');
        });
    });
    
    // Кнопки управления
    document.getElementById('start-rect-btn').addEventListener('click', showStartRectModal);
    document.getElementById('start-dist-btn').addEventListener('click', showStartDistModal);
    document.getElementById('pause-btn').addEventListener('click', togglePause);
    document.getElementById('stop-btn').addEventListener('click', stopProcess);
    
    // Ползунок мощности
    const powerSlider = document.getElementById('power-slider');
    powerSlider.addEventListener('input', () => {
        const value = powerSlider.value;
        document.getElementById('power-value').textContent = value + '%';
        
        // Рассчитываем мощность в ваттах
        if (systemSettings.maxHeaterPower) {
            const watts = Math.round(systemSettings.maxHeaterPower * value / 100);
            document.getElementById('power-watts').textContent = `(${watts} Вт)`;
        }
    });
    
    powerSlider.addEventListener('change', () => {
        setPower(powerSlider.value);
    });
    
    // Обработчики форм настроек
    document.getElementById('system-settings-form').addEventListener('submit', saveSystemSettings);
    document.getElementById('rect-settings-form').addEventListener('submit', saveRectificationSettings);
    document.getElementById('dist-settings-form').addEventListener('submit', saveDistillationSettings);
    document.getElementById('pump-settings-form').addEventListener('submit', savePumpSettings);
    
    // Обработчик для калибровки насоса
    document.getElementById('calibrate-pump-btn').addEventListener('click', showPumpCalibrationModal);
    
    // Обработчики модальных окон
    document.querySelectorAll('.close-modal').forEach(closeBtn => {
        closeBtn.addEventListener('click', () => {
            document.querySelectorAll('.modal').forEach(modal => {
                modal.style.display = 'none';
            });
        });
    });
    
    // Обработчик клика вне модального окна
    window.addEventListener('click', (e) => {
        document.querySelectorAll('.modal').forEach(modal => {
            if (e.target === modal) {
                modal.style.display = 'none';
            }
        });
    });
    
    // Настройка модального окна для калибровки насоса
    document.getElementById('calibration-next-1').addEventListener('click', () => {
        document.getElementById('calibration-step-1').style.display = 'none';
        document.getElementById('calibration-step-2').style.display = 'block';
    });
    
    document.getElementById('calibration-start').addEventListener('click', startPumpCalibration);
    document.getElementById('calibration-finish').addEventListener('click', finishPumpCalibration);
    document.getElementById('calibration-cancel').addEventListener('click', () => {
        document.getElementById('pump-calibration-modal').style.display = 'none';
    });
    
    // Подтверждение запуска ректификации
    document.getElementById('start-rect-confirm').addEventListener('click', startRectification);
    
    // Подтверждение запуска дистилляции
    document.getElementById('start-dist-confirm').addEventListener('click', startDistillation);
    
    // Сканирование датчиков температуры
    document.getElementById('scan-sensors-btn').addEventListener('click', scanTemperatureSensors);
    
    // Калибровка датчиков температуры
    document.querySelectorAll('.calibrate-sensor-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const sensorId = btn.getAttribute('data-sensor-id');
            const calibrationValue = document.getElementById(`sensor-${sensorId}-calibration`).value;
            calibrateTemperatureSensor(sensorId, calibrationValue);
        });
    });
    
    // Сохранение датчиков
    document.getElementById('save-sensors-btn').addEventListener('click', saveSensorSettings);
    
    // Перезагрузка устройства
    document.getElementById('reboot-device-btn').addEventListener('click', rebootDevice);
    
    // Сброс настроек
    document.getElementById('reset-settings-btn').addEventListener('click', () => {
        if (confirm('Вы уверены, что хотите сбросить все настройки к значениям по умолчанию?')) {
            resetAllSettings();
        }
    });
    
    // Обработчик для выбора модели ректификации
    document.getElementById('rect-model').addEventListener('change', function() {
        const alternativeModelSettings = document.getElementById('alternative-model-settings');
        if (this.value === '1') {
            // Альтернативная модель
            alternativeModelSettings.style.display = 'block';
        } else {
            // Классическая модель
            alternativeModelSettings.style.display = 'none';
        }
    });
    
    // Обработчик для отделения голов при дистилляции
    document.getElementById('dist-separate-heads').addEventListener('change', function() {
        const headsSettings = document.getElementById('dist-heads-settings');
        if (this.checked) {
            headsSettings.style.display = 'block';
        } else {
            headsSettings.style.display = 'none';
        }
    });
}

// Инициализация графика температур
function initializeChart() {
    const ctx = document.getElementById('temp-chart').getContext('2d');
    tempChart = new Chart(ctx, {
        type: 'line',
        data: temperatureData,
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: {
                mode: 'index',
                intersect: false,
            },
            scales: {
                x: {
                    type: 'time',
                    time: {
                        unit: 'minute',
                        displayFormats: {
                            minute: 'HH:mm',
                            hour: 'HH:mm'
                        }
                    },
                    title: {
                        display: true,
                        text: 'Время'
                    }
                },
                'y-temperature': {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Температура (°C)'
                    },
                    min: 20,
                    max: 100
                },
                'y-power': {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: {
                        display: true,
                        text: 'Мощность (%)'
                    },
                    min: 0,
                    max: 100,
                    grid: {
                        drawOnChartArea: false
                    }
                }
            },
            plugins: {
                legend: {
                    position: 'top',
                }
            }
        }
    });
    
    // Настройка обработчиков для отображения серий
    document.getElementById('show-cube-temp').addEventListener('change', function() {
        toggleDataset(0, this.checked);
    });
    
    document.getElementById('show-reflux-temp').addEventListener('change', function() {
        toggleDataset(1, this.checked);
    });
    
    document.getElementById('show-product-temp').addEventListener('change', function() {
        toggleDataset(2, this.checked);
    });
    
    document.getElementById('show-power').addEventListener('change', function() {
        toggleDataset(3, this.checked);
    });
    
    // Установка периода отображения
    document.getElementById('chart-range-select').addEventListener('change', function() {
        updateChartTimeRange(parseInt(this.value));
    });
}

// Переключение отображения серий данных
function toggleDataset(index, visible) {
    tempChart.data.datasets[index].hidden = !visible;
    tempChart.update();
}

// Обновление временного диапазона графика
function updateChartTimeRange(minutes) {
    const now = new Date();
    const minTime = new Date(now.getTime() - minutes * 60000);
    
    tempChart.options.scales.x.min = minTime;
    tempChart.options.scales.x.max = now;
    tempChart.update();
}

// Подключение к WebSocket
function connectWebSocket() {
    if (socket) {
        socket.close();
    }
    
    socket = new WebSocket(socketUrl);
    
    socket.onopen = function(event) {
        console.log('WebSocket соединение установлено');
        updateConnectionStatus(true);
        
        // Запрашиваем статус при подключении
        requestSystemStatus();
    };
    
    socket.onclose = function(event) {
        console.log('WebSocket соединение закрыто');
        updateConnectionStatus(false);
        
        // Попытка переподключения через 5 секунд
        setTimeout(connectWebSocket, 5000);
    };
    
    socket.onerror = function(error) {
        console.error('Ошибка WebSocket:', error);
        updateConnectionStatus(false);
    };
    
    socket.onmessage = function(event) {
        try {
            const message = JSON.parse(event.data);
            handleWebSocketMessage(message);
        } catch (e) {
            console.error('Ошибка при обработке сообщения WebSocket:', e);
        }
    };
}

// Обновление статуса подключения
function updateConnectionStatus(connected) {
    const statusIndicator = document.querySelector('#connection-status .status-indicator');
    const statusText = document.querySelector('#connection-status .status-text');
    
    if (connected) {
        statusIndicator.classList.remove('offline');
        statusIndicator.classList.add('online');
        statusText.textContent = 'Подключено';
    } else {
        statusIndicator.classList.remove('online');
        statusIndicator.classList.add('offline');
        statusText.textContent = 'Отключено';
    }
}

// Обработка сообщений от WebSocket
function handleWebSocketMessage(message) {
    switch (message.type) {
        case 'status':
            updateSystemStatus(message.data);
            break;
        case 'temperatures':
            updateTemperatures(message.data);
            break;
        case 'notification':
            showNotification(message.message, message.notificationType);
            break;
        case 'settings':
            updateSettings(message.data);
            break;
        case 'sensors':
            updateSensorInfo(message.data);
            break;
        default:
            console.log('Получено неизвестное сообщение:', message);
            break;
    }
}

// Обновление статуса системы
function updateSystemStatus(status) {
    isSystemRunning = status.running;
    isSystemPaused = status.paused;
    currentMode = status.mode;
    
    // Обновляем кнопки управления
    const startRectBtn = document.getElementById('start-rect-btn');
    const startDistBtn = document.getElementById('start-dist-btn');
    const pauseBtn = document.getElementById('pause-btn');
    const stopBtn = document.getElementById('stop-btn');
    
    if (isSystemRunning) {
        startRectBtn.disabled = true;
        startDistBtn.disabled = true;
        stopBtn.disabled = false;
        
        if (isSystemPaused) {
            pauseBtn.textContent = 'Продолжить';
            pauseBtn.classList.remove('btn-warning');
            pauseBtn.classList.add('btn-success');
        } else {
            pauseBtn.textContent = 'Пауза';
            pauseBtn.classList.remove('btn-success');
            pauseBtn.classList.add('btn-warning');
        }
        
        pauseBtn.disabled = false;
    } else {
        startRectBtn.disabled = false;
        startDistBtn.disabled = false;
        pauseBtn.disabled = true;
        stopBtn.disabled = true;
    }
    
    // Обновляем ползунок мощности
    const powerSlider = document.getElementById('power-slider');
    powerSlider.value = status.currentPower || 0;
    document.getElementById('power-value').textContent = (status.currentPower || 0) + '%';
    
    // Рассчитываем мощность в ваттах
    if (systemSettings.maxHeaterPower) {
        const watts = Math.round(systemSettings.maxHeaterPower * (status.currentPower || 0) / 100);
        document.getElementById('power-watts').textContent = `(${watts} Вт)`;
    }
    
    // Обновляем статус системы
    document.getElementById('system-status').textContent = 
        `Статус: ${isSystemRunning ? (isSystemPaused ? 'Пауза' : 'Работает') : 'Остановлен'}`;
    
    // Обновляем информацию о режиме
    let modeText = 'Не выбран';
    if (status.mode === 'rectification') {
        modeText = 'Ректификация';
    } else if (status.mode === 'distillation') {
        modeText = 'Дистилляция';
    }
    document.getElementById('mode-value').textContent = modeText;
    
    // Обновляем информацию о фазе
    let phaseText = '-';
    if (status.mode === 'rectification') {
        switch (status.phase) {
            case 'heating':
                phaseText = 'Нагрев';
                break;
            case 'stabilization':
                phaseText = 'Стабилизация';
                break;
            case 'heads':
                phaseText = 'Отбор голов';
                break;
            case 'post_heads_stabilization':
                phaseText = 'Стабилизация после голов';
                break;
            case 'body':
                phaseText = 'Отбор тела';
                break;
            case 'tails':
                phaseText = 'Отбор хвостов';
                break;
            case 'completed':
                phaseText = 'Завершено';
                break;
        }
    } else if (status.mode === 'distillation') {
        switch (status.phase) {
            case 'heating':
                phaseText = 'Нагрев';
                break;
            case 'distillation':
                phaseText = status.inHeadsPhase ? 'Отбор голов' : 'Отбор';
                break;
            case 'completed':
                phaseText = 'Завершено';
                break;
        }
    }
    document.getElementById('phase-value').textContent = phaseText;
    
    // Обновляем время работы
    if (status.uptime) {
        document.getElementById('uptime-value').textContent = formatTime(status.uptime);
    } else {
        document.getElementById('uptime-value').textContent = '00:00:00';
    }
    
    // Обновляем объемы отбора
    if (status.mode === 'rectification') {
        document.getElementById('heads-value').textContent = (status.headsCollected || 0) + ' мл';
        document.getElementById('body-value').textContent = (status.bodyCollected || 0) + ' мл';
        document.getElementById('tails-value').textContent = (status.tailsCollected || 0) + ' мл';
        
        const total = (status.headsCollected || 0) + (status.bodyCollected || 0) + (status.tailsCollected || 0);
        document.getElementById('total-value').textContent = total + ' мл';
    } else if (status.mode === 'distillation') {
        document.getElementById('heads-value').textContent = (status.inHeadsPhase ? status.collected : 0) + ' мл';
        document.getElementById('body-value').textContent = (status.inHeadsPhase ? 0 : status.collected) + ' мл';
        document.getElementById('tails-value').textContent = '0 мл';
        
        document.getElementById('total-value').textContent = (status.collected || 0) + ' мл';
    } else {
        document.getElementById('heads-value').textContent = '0 мл';
        document.getElementById('body-value').textContent = '0 мл';
        document.getElementById('tails-value').textContent = '0 мл';
        document.getElementById('total-value').textContent = '0 мл';
    }
}

// Обновление температур
function updateTemperatures(tempData) {
    // Обновляем отображение текущих температур
    if (tempData.cube !== undefined) {
        document.getElementById('cube-temp').textContent = 
            tempData.cube.toFixed(1) + '°C';
        document.getElementById('sensor-0-temp').textContent = 
            tempData.cube.toFixed(1) + '°C';
    }
    
    if (tempData.reflux !== undefined) {
        document.getElementById('reflux-temp').textContent = 
            tempData.reflux.toFixed(1) + '°C';
        document.getElementById('sensor-1-temp').textContent = 
            tempData.reflux.toFixed(1) + '°C';
    }
    
    if (tempData.product !== undefined) {
        document.getElementById('product-temp').textContent = 
            tempData.product.toFixed(1) + '°C';
        document.getElementById('sensor-2-temp').textContent = 
            tempData.product.toFixed(1) + '°C';
    }
    
    // Обновляем данные для графика
    const timestamp = tempData.timestamp ? new Date(tempData.timestamp) : new Date();
    
    temperatureData.labels.push(timestamp);
    temperatureData.datasets[0].data.push({x: timestamp, y: tempData.cube !== undefined ? tempData.cube : null});
    temperatureData.datasets[1].data.push({x: timestamp, y: tempData.reflux !== undefined ? tempData.reflux : null});
    temperatureData.datasets[2].data.push({x: timestamp, y: tempData.product !== undefined ? tempData.product : null});
    temperatureData.datasets[3].data.push({x: timestamp, y: tempData.power !== undefined ? tempData.power : null});
    
    // Ограничиваем количество точек на графике
    const maxDataPoints = 3600; // максимум данных за час при шаге в 1 секунду
    
    if (temperatureData.labels.length > maxDataPoints) {
        temperatureData.labels.shift();
        temperatureData.datasets.forEach(dataset => {
            dataset.data.shift();
        });
    }
    
    // Обновляем график
    const chartRangeValue = parseInt(document.getElementById('chart-range-select').value);
    updateChartTimeRange(chartRangeValue);
}

// Отображение уведомления
function showNotification(message, type = 'info') {
    const modal = document.getElementById('notification-modal');
    const title = document.getElementById('notification-title');
    const messageElem = document.getElementById('notification-message');
    
    // Устанавливаем заголовок
    switch (type) {
        case 'success':
            title.textContent = 'Успех';
            title.style.color = '#2ecc71';
            break;
        case 'warning':
            title.textContent = 'Предупреждение';
            title.style.color = '#f39c12';
            break;
        case 'error':
            title.textContent = 'Ошибка';
            title.style.color = '#e74c3c';
            break;
        default:
            title.textContent = 'Информация';
            title.style.color = '#3498db';
            break;
    }
    
    // Устанавливаем сообщение
    messageElem.textContent = message;
    
    // Показываем модальное окно
    modal.style.display = 'block';
    
    // Добавляем обработчик для кнопки ОК
    document.getElementById('notification-ok-btn').onclick = function() {
        modal.style.display = 'none';
    };
}

// Загрузка настроек
function loadSettings() {
    // Загрузка системных настроек
    fetch('/api/settings/system')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                systemSettings = data.settings;
                updateSystemSettingsForm(systemSettings);
            } else {
                console.error('Ошибка загрузки системных настроек:', data.message);
            }
        })
        .catch(error => {
            console.error('Ошибка при загрузке системных настроек:', error);
        });
    
    // Загрузка настроек ректификации
    fetch('/api/settings/rectification')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                rectificationSettings = data.settings;
                updateRectificationSettingsForm(rectificationSettings);
            } else {
                console.error('Ошибка загрузки настроек ректификации:', data.message);
            }
        })
        .catch(error => {
            console.error('Ошибка при загрузке настроек ректификации:', error);
        });
    
    // Загрузка настроек дистилляции
    fetch('/api/settings/distillation')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                distillationSettings = data.settings;
                updateDistillationSettingsForm(distillationSettings);
            } else {
                console.error('Ошибка загрузки настроек дистилляции:', data.message);
            }
        })
        .catch(error => {
            console.error('Ошибка при загрузке настроек дистилляции:', error);
        });
    
    // Загрузка настроек насоса
    fetch('/api/settings/pump')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                pumpSettings = data.settings;
                updatePumpSettingsForm(pumpSettings);
            } else {
                console.error('Ошибка загрузки настроек насоса:', data.message);
            }
        })
        .catch(error => {
            console.error('Ошибка при загрузке настроек насоса:', error);
        });
    
    // Загрузка информации о системе
    fetch('/api/system/info')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                updateSystemInfo(data);
            } else {
                console.error('Ошибка загрузки информации о системе:', data.message);
            }
        })
        .catch(error => {
            console.error('Ошибка при загрузке информации о системе:', error);
        });
    
    // Загрузка информации о датчиках
    fetch('/api/sensors/info')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                updateSensorInfo(data.sensors);
            } else {
                console.error('Ошибка загрузки информации о датчиках:', data.message);
            }
        })
        .catch(error => {
            console.error('Ошибка при загрузке информации о датчиках:', error);
        });
}

// Обновление формы системных настроек
function updateSystemSettingsForm(settings) {
    document.getElementById('max-heater-power').value = settings.maxHeaterPower || 3000;
    document.getElementById('power-control-mode').value = settings.powerControlMode || 0;
    document.getElementById('pzem-enabled').checked = settings.pzemEnabled || false;
    document.getElementById('sound-enabled').checked = settings.soundEnabled !== false;
    document.getElementById('sound-volume').value = settings.soundVolume || 50;
    document.getElementById('sound-volume-value').textContent = (settings.soundVolume || 50) + '%';
    
    // Настройки дисплея
    document.getElementById('display-enabled').checked = settings.displayEnabled !== false;
    document.getElementById('display-brightness').value = settings.displayBrightness || 128;
    document.getElementById('display-brightness-value').textContent = settings.displayBrightness || 128;
    
    // Настройки WiFi
    document.getElementById('wifi-ssid').value = settings.wifiSSID || '';
    document.getElementById('wifi-password').value = ''; // Не показываем пароль
}

// Обновление формы настроек ректификации
function updateRectificationSettingsForm(settings) {
    document.getElementById('rect-model').value = settings.model || 0;
    
    // Если модель альтернативная, показываем дополнительные настройки
    if (settings.model === 1) {
        document.getElementById('alternative-model-settings').style.display = 'block';
    } else {
        document.getElementById('alternative-model-settings').style.display = 'none';
    }
    
    // Температуры
    document.getElementById('rect-max-cube-temp').value = settings.maxCubeTemp || 100;
    document.getElementById('rect-heads-temp').value = settings.headsTemp || 70;
    document.getElementById('rect-body-temp').value = settings.bodyTemp || 78;
    document.getElementById('rect-tails-temp').value = settings.tailsTemp || 88;
    document.getElementById('rect-end-temp').value = settings.endTemp || 98;
    
    // Мощность
    document.getElementById('rect-heating-power').value = settings.heatingPowerWatts || 3000;
    document.getElementById('rect-stabilization-power').value = settings.stabilizationPowerWatts || 2000;
    document.getElementById('rect-body-power').value = settings.bodyPowerWatts || 1500;
    document.getElementById('rect-tails-power').value = settings.tailsPowerWatts || 2000;
    
    // Параметры отбора
    document.getElementById('rect-stabilization-time').value = settings.stabilizationTime || 30;
    document.getElementById('rect-heads-volume').value = settings.headsVolume || 100;
    document.getElementById('rect-body-volume').value = settings.bodyVolume || 2000;
    
    // Настройки орошения
    document.getElementById('rect-reflux-ratio').value = settings.refluxRatio || 3;
    document.getElementById('rect-reflux-period').value = settings.refluxPeriod || 60;
    
    // Параметры альтернативной модели
    document.getElementById('rect-heads-target-time').value = settings.headsTargetTime || 30;
    document.getElementById('rect-post-heads-stab-time').value = settings.postHeadsStabilizationTime || 10;
    document.getElementById('rect-body-flow-rate').value = settings.bodyFlowRate || 500;
    document.getElementById('rect-temp-delta-end-body').value = settings.tempDeltaEndBody || 0.2;
    document.getElementById('rect-tails-cube-temp').value = settings.tailsCubeTemp || 95;
    document.getElementById('rect-tails-flow-rate').value = settings.tailsFlowRate || 800;
    document.getElementById('rect-use-same-flow-for-tails').checked = settings.useSameFlowForTails || false;
}

// Обновление формы настроек дистилляции
function updateDistillationSettingsForm(settings) {
    // Температуры
    document.getElementById('dist-max-cube-temp').value = settings.maxCubeTemp || 100;
    document.getElementById('dist-start-collect-temp').value = settings.startCollectingTemp || 70;
    document.getElementById('dist-end-temp').value = settings.endTemp || 98;
    
    // Мощность
    document.getElementById('dist-heating-power').value = settings.heatingPowerWatts || 3000;
    document.getElementById('dist-distillation-power').value = settings.distillationPowerWatts || 2500;
    
    // Параметры отбора
    document.getElementById('dist-flow-rate').value = settings.flowRate || 1000;
    
    // Отбор голов
    document.getElementById('dist-separate-heads').checked = settings.separateHeads || false;
    
    // Если отделять головы, показываем дополнительные настройки
    if (settings.separateHeads) {
        document.getElementById('dist-heads-settings').style.display = 'block';
    } else {
        document.getElementById('dist-heads-settings').style.display = 'none';
    }
    
    document.getElementById('dist-heads-volume').value = settings.headsVolume || 100;
    document.getElementById('dist-heads-flow-rate').value = settings.headsFlowRate || 500;
}

// Обновление формы настроек насоса
function updatePumpSettingsForm(settings) {
    document.getElementById('pump-calibration-factor').value = settings.calibrationFactor || 1.0;
    
    // Параметры скорости отбора
    document.getElementById('pump-heads-flow-rate').value = settings.headsFlowRate || 300;
    document.getElementById('pump-body-flow-rate').value = settings.bodyFlowRate || 500;
    document.getElementById('pump-tails-flow-rate').value = settings.tailsFlowRate || 800;
    
    // Ограничения
    document.getElementById('pump-min-flow-rate').value = settings.minFlowRate || 50;
    document.getElementById('pump-max-flow-rate').value = settings.maxFlowRate || 2000;
    
    // Расширенные настройки
    document.getElementById('pump-period-ms').value = settings.pumpPeriodMs || 1000;
}

// Обновление информации о системе
function updateSystemInfo(info) {
    document.getElementById('system-version').textContent = info.version || '1.0.0';
    document.getElementById('build-date').textContent = info.buildDate || '01.01.2023';
    document.getElementById('device-model').textContent = info.deviceModel || 'ESP32 Controller';
    document.getElementById('device-mac').textContent = info.macAddress || 'XX:XX:XX:XX:XX:XX';
    document.getElementById('device-ip').textContent = info.ipAddress || '192.168.1.100';
    document.getElementById('free-memory').textContent = (info.freeMemory || 0) + ' байт';
    document.getElementById('device-uptime').textContent = formatUptime(info.uptime || 0);
}

// Обновление информации о датчиках
function updateSensorInfo(sensors) {
    sensors.forEach((sensor, index) => {
        if (index < 3) { // Обрабатываем только первые 3 датчика
            document.getElementById(`sensor-${index}-address`).textContent = 
                sensor.address || 'Не подключен';
            
            if (sensor.name) {
                document.getElementById(`sensor-${index}-name`).value = sensor.name;
            }
            
            if (sensor.calibration !== undefined) {
                document.getElementById(`sensor-${index}-calibration`).value = sensor.calibration;
            }
        }
    });
}

// Сохранение системных настроек
function saveSystemSettings(event) {
    event.preventDefault();
    
    const form = event.target;
    const formData = new FormData(form);
    
    // Собираем данные из формы
    const settings = {
        maxHeaterPower: parseInt(formData.get('maxHeaterPower')),
        powerControlMode: parseInt(formData.get('powerControlMode')),
        pzemEnabled: formData.get('pzemEnabled') === 'on',
        soundEnabled: formData.get('soundEnabled') === 'on',
        soundVolume: parseInt(formData.get('soundVolume')),
        displayEnabled: formData.get('displayEnabled') === 'on',
        displayBrightness: parseInt(formData.get('displayBrightness')),
        wifiSSID: formData.get('wifiSSID'),
        wifiPassword: formData.get('wifiPassword')
    };
    
    // Отправляем запрос на сохранение
    fetch('/api/settings/system', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(settings),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification('Системные настройки успешно сохранены', 'success');
            systemSettings = settings;
        } else {
            showNotification('Ошибка при сохранении системных настроек: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при отправке системных настроек:', error);
        showNotification('Ошибка при сохранении: ' + error.message, 'error');
    });
}

// Сохранение настроек ректификации
function saveRectificationSettings(event) {
    event.preventDefault();
    
    const form = event.target;
    const formData = new FormData(form);
    
    // Собираем данные из формы
    const settings = {
        model: parseInt(formData.get('model')),
        maxCubeTemp: parseFloat(formData.get('maxCubeTemp')),
        headsTemp: parseFloat(formData.get('headsTemp')),
        bodyTemp: parseFloat(formData.get('bodyTemp')),
        tailsTemp: parseFloat(formData.get('tailsTemp')),
        endTemp: parseFloat(formData.get('endTemp')),
        heatingPowerWatts: parseInt(formData.get('heatingPowerWatts')),
        stabilizationPowerWatts: parseInt(formData.get('stabilizationPowerWatts')),
        bodyPowerWatts: parseInt(formData.get('bodyPowerWatts')),
        tailsPowerWatts: parseInt(formData.get('tailsPowerWatts')),
        stabilizationTime: parseInt(formData.get('stabilizationTime')),
        headsVolume: parseInt(formData.get('headsVolume')),
        bodyVolume: parseInt(formData.get('bodyVolume')),
        refluxRatio: parseFloat(formData.get('refluxRatio')),
        refluxPeriod: parseInt(formData.get('refluxPeriod'))
    };
    
    // Добавляем параметры альтернативной модели
    if (settings.model === 1) {
        settings.headsTargetTime = parseInt(formData.get('headsTargetTime'));
        settings.postHeadsStabilizationTime = parseInt(formData.get('postHeadsStabilizationTime'));
        settings.bodyFlowRate = parseInt(formData.get('bodyFlowRate'));
        settings.tempDeltaEndBody = parseFloat(formData.get('tempDeltaEndBody'));
        settings.tailsCubeTemp = parseFloat(formData.get('tailsCubeTemp'));
        settings.tailsFlowRate = parseInt(formData.get('tailsFlowRate'));
        settings.useSameFlowForTails = formData.get('useSameFlowForTails') === 'on';
    }
    
    // Отправляем запрос на сохранение
    fetch('/api/settings/rectification', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(settings),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification('Настройки ректификации успешно сохранены', 'success');
            rectificationSettings = settings;
        } else {
            showNotification('Ошибка при сохранении настроек ректификации: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при отправке настроек ректификации:', error);
        showNotification('Ошибка при сохранении: ' + error.message, 'error');
    });
}

// Сохранение настроек дистилляции
function saveDistillationSettings(event) {
    event.preventDefault();
    
    const form = event.target;
    const formData = new FormData(form);
    
    // Собираем данные из формы
    const settings = {
        maxCubeTemp: parseFloat(formData.get('maxCubeTemp')),
        startCollectingTemp: parseFloat(formData.get('startCollectingTemp')),
        endTemp: parseFloat(formData.get('endTemp')),
        heatingPowerWatts: parseInt(formData.get('heatingPowerWatts')),
        distillationPowerWatts: parseInt(formData.get('distillationPowerWatts')),
        flowRate: parseInt(formData.get('flowRate')),
        separateHeads: formData.get('separateHeads') === 'on'
    };
    
    // Добавляем параметры отбора голов, если они активны
    if (settings.separateHeads) {
        settings.headsVolume = parseInt(formData.get('headsVolume'));
        settings.headsFlowRate = parseInt(formData.get('headsFlowRate'));
    }
    
    // Отправляем запрос на сохранение
    fetch('/api/settings/distillation', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(settings),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification('Настройки дистилляции успешно сохранены', 'success');
            distillationSettings = settings;
        } else {
            showNotification('Ошибка при сохранении настроек дистилляции: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при отправке настроек дистилляции:', error);
        showNotification('Ошибка при сохранении: ' + error.message, 'error');
    });
}

// Сохранение настроек насоса
function savePumpSettings(event) {
    event.preventDefault();
    
    const form = event.target;
    const formData = new FormData(form);
    
    // Собираем данные из формы
    const settings = {
        calibrationFactor: parseFloat(formData.get('calibrationFactor')),
        headsFlowRate: parseInt(formData.get('headsFlowRate')),
        bodyFlowRate: parseInt(formData.get('bodyFlowRate')),
        tailsFlowRate: parseInt(formData.get('tailsFlowRate')),
        minFlowRate: parseInt(formData.get('minFlowRate')),
        maxFlowRate: parseInt(formData.get('maxFlowRate')),
        pumpPeriodMs: parseInt(formData.get('pumpPeriodMs'))
    };
    
    // Отправляем запрос на сохранение
    fetch('/api/settings/pump', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(settings),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification('Настройки насоса успешно сохранены', 'success');
            pumpSettings = settings;
        } else {
            showNotification('Ошибка при сохранении настроек насоса: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при отправке настроек насоса:', error);
        showNotification('Ошибка при сохранении: ' + error.message, 'error');
    });
}

// Сохранение настроек датчиков
function saveSensorSettings() {
    const sensors = [];
    
    // Собираем данные о датчиках
    for (let i = 0; i < 3; i++) {
        sensors.push({
            id: i,
            name: document.getElementById(`sensor-${i}-name`).value,
            calibration: parseFloat(document.getElementById(`sensor-${i}-calibration`).value)
        });
    }
    
    // Отправляем запрос на сохранение
    fetch('/api/sensors/settings', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({sensors}),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification('Настройки датчиков успешно сохранены', 'success');
        } else {
            showNotification('Ошибка при сохранении настроек датчиков: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при отправке настроек датчиков:', error);
        showNotification('Ошибка при сохранении: ' + error.message, 'error');
    });
}

// Сканирование датчиков температуры
function scanTemperatureSensors() {
    fetch('/api/sensors/scan', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification('Сканирование датчиков температуры выполнено успешно', 'success');
            
            // Обновляем информацию о датчиках
            if (data.sensors) {
                updateSensorInfo(data.sensors);
            }
        } else {
            showNotification('Ошибка при сканировании датчиков: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при сканировании датчиков:', error);
        showNotification('Ошибка при сканировании: ' + error.message, 'error');
    });
}

// Калибровка датчика температуры
function calibrateTemperatureSensor(sensorId, calibrationValue) {
    fetch('/api/sensors/calibrate', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            sensorId: parseInt(sensorId),
            calibration: parseFloat(calibrationValue)
        }),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification(`Датчик ${sensorId} успешно откалиброван`, 'success');
        } else {
            showNotification('Ошибка при калибровке датчика: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при калибровке датчика:', error);
        showNotification('Ошибка при калибровке: ' + error.message, 'error');
    });
}

// Отображение модального окна для запуска ректификации
function showStartRectModal() {
    const modal = document.getElementById('start-rect-modal');
    
    // Обновляем информацию в модальном окне
    document.getElementById('rect-start-model').textContent = 
        rectificationSettings.model === 0 ? 'Классическая' : 'Альтернативная';
    
    document.getElementById('rect-start-heating-power').textContent = 
        rectificationSettings.heatingPowerWatts + ' Вт';
    
    document.getElementById('rect-start-heads-volume').textContent = 
        rectificationSettings.headsVolume + ' мл';
    
    document.getElementById('rect-start-body-volume').textContent = 
        rectificationSettings.bodyVolume + ' мл';
    
    // Показываем модальное окно
    modal.style.display = 'block';
}

// Отображение модального окна для запуска дистилляции
function showStartDistModal() {
    const modal = document.getElementById('start-dist-modal');
    
    // Обновляем информацию в модальном окне
    document.getElementById('dist-start-heating-power').textContent = 
        distillationSettings.heatingPowerWatts + ' Вт';
    
    document.getElementById('dist-start-dist-power').textContent = 
        distillationSettings.distillationPowerWatts + ' Вт';
    
    document.getElementById('dist-start-flow-rate').textContent = 
        distillationSettings.flowRate + ' мл/час';
    
    document.getElementById('dist-start-separate-heads').textContent = 
        distillationSettings.separateHeads ? 'Да' : 'Нет';
    
    // Показываем модальное окно
    modal.style.display = 'block';
}

// Запуск процесса ректификации
function startRectification() {
    fetch('/api/process/start', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            mode: 'rectification'
        }),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Закрываем модальное окно
            document.getElementById('start-rect-modal').style.display = 'none';
            
            // Обновляем интерфейс
            currentMode = 'rectification';
            isSystemRunning = true;
            isSystemPaused = false;
            
            // Обновляем кнопки
            document.getElementById('start-rect-btn').disabled = true;
            document.getElementById('start-dist-btn').disabled = true;
            document.getElementById('pause-btn').disabled = false;
            document.getElementById('stop-btn').disabled = false;
            
            showNotification('Процесс ректификации успешно запущен', 'success');
        } else {
            showNotification('Ошибка при запуске ректификации: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при запуске ректификации:', error);
        showNotification('Ошибка при запуске: ' + error.message, 'error');
    });
}

// Запуск процесса дистилляции
function startDistillation() {
    fetch('/api/process/start', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            mode: 'distillation'
        }),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Закрываем модальное окно
            document.getElementById('start-dist-modal').style.display = 'none';
            
            // Обновляем интерфейс
            currentMode = 'distillation';
            isSystemRunning = true;
            isSystemPaused = false;
            
            // Обновляем кнопки
            document.getElementById('start-rect-btn').disabled = true;
            document.getElementById('start-dist-btn').disabled = true;
            document.getElementById('pause-btn').disabled = false;
            document.getElementById('stop-btn').disabled = false;
            
            showNotification('Процесс дистилляции успешно запущен', 'success');
        } else {
            showNotification('Ошибка при запуске дистилляции: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при запуске дистилляции:', error);
        showNotification('Ошибка при запуске: ' + error.message, 'error');
    });
}

// Остановка процесса
function stopProcess() {
    fetch('/api/process/stop', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Обновляем интерфейс
            isSystemRunning = false;
            isSystemPaused = false;
            
            // Обновляем кнопки
            document.getElementById('start-rect-btn').disabled = false;
            document.getElementById('start-dist-btn').disabled = false;
            document.getElementById('pause-btn').disabled = true;
            document.getElementById('stop-btn').disabled = true;
            
            showNotification('Процесс успешно остановлен', 'success');
        } else {
            showNotification('Ошибка при остановке процесса: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при остановке процесса:', error);
        showNotification('Ошибка при остановке: ' + error.message, 'error');
    });
}

// Пауза/возобновление процесса
function togglePause() {
    const action = isSystemPaused ? 'resume' : 'pause';
    
    fetch(`/api/process/${action}`, {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Обновляем интерфейс
            isSystemPaused = !isSystemPaused;
            
            // Обновляем кнопку
            const pauseBtn = document.getElementById('pause-btn');
            
            if (isSystemPaused) {
                pauseBtn.textContent = 'Продолжить';
                pauseBtn.classList.remove('btn-warning');
                pauseBtn.classList.add('btn-success');
                showNotification('Процесс приостановлен', 'info');
            } else {
                pauseBtn.textContent = 'Пауза';
                pauseBtn.classList.remove('btn-success');
                pauseBtn.classList.add('btn-warning');
                showNotification('Процесс возобновлен', 'success');
            }
        } else {
            showNotification(`Ошибка при ${isSystemPaused ? 'возобновлении' : 'приостановке'} процесса: ` + data.message, 'error');
        }
    })
    .catch(error => {
        console.error(`Ошибка при ${isSystemPaused ? 'возобновлении' : 'приостановке'} процесса:`, error);
        showNotification(`Ошибка при ${isSystemPaused ? 'возобновлении' : 'приостановке'}: ` + error.message, 'error');
    });
}

// Установка мощности
function setPower(powerPercent) {
    fetch('/api/heater/power', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            power: parseInt(powerPercent)
        }),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Обновляем интерфейс
            document.getElementById('power-value').textContent = powerPercent + '%';
            
            // Рассчитываем мощность в ваттах
            if (systemSettings.maxHeaterPower) {
                const watts = Math.round(systemSettings.maxHeaterPower * powerPercent / 100);
                document.getElementById('power-watts').textContent = `(${watts} Вт)`;
            }
        } else {
            showNotification('Ошибка при установке мощности: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при установке мощности:', error);
        showNotification('Ошибка при установке мощности: ' + error.message, 'error');
    });
}

// Запрос статуса системы
function requestSystemStatus() {
    fetch('/api/status')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                updateSystemStatus(data);
            } else {
                console.error('Ошибка при получении статуса:', data.message);
            }
        })
        .catch(error => {
            console.error('Ошибка при получении статуса системы:', error);
        });
}

// Отображение модального окна для калибровки насоса
function showPumpCalibrationModal() {
    // Сбрасываем состояние модального окна
    document.getElementById('calibration-step-1').style.display = 'block';
    document.getElementById('calibration-step-2').style.display = 'none';
    document.getElementById('calibration-step-3').style.display = 'none';
    
    document.getElementById('calibration-timer').textContent = 'Готов к запуску';
    document.getElementById('calibration-volume').value = '';
    
    // Показываем модальное окно
    document.getElementById('pump-calibration-modal').style.display = 'block';
}

// Запуск калибровки насоса
function startPumpCalibration() {
    const startBtn = document.getElementById('calibration-start');
    const timer = document.getElementById('calibration-timer');
    
    // Отключаем кнопку на время калибровки
    startBtn.disabled = true;
    
    // Отправляем запрос на запуск калибровки
    fetch('/api/pump/calibrate/start', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Запускаем таймер обратного отсчета
            let remainingTime = 30; // 30 секунд
            
            function updateTimer() {
                timer.textContent = `Калибровка в процессе: ${remainingTime} с`;
                
                if (remainingTime <= 0) {
                    clearInterval(timerInterval);
                    timer.textContent = 'Калибровка завершена!';
                    
                    // Показываем следующий шаг
                    document.getElementById('calibration-step-2').style.display = 'none';
                    document.getElementById('calibration-step-3').style.display = 'block';
                    
                    // Отправляем запрос на остановку насоса
                    fetch('/api/pump/calibrate/stop', { method: 'POST' });
                } else {
                    remainingTime--;
                }
            }
            
            // Обновляем таймер каждую секунду
            updateTimer();
            const timerInterval = setInterval(updateTimer, 1000);
        } else {
            startBtn.disabled = false;
            showNotification('Ошибка при запуске калибровки: ' + data.message, 'error');
        }
    })
    .catch(error => {
        startBtn.disabled = false;
        console.error('Ошибка при запуске калибровки насоса:', error);
        showNotification('Ошибка при запуске калибровки: ' + error.message, 'error');
    });
}

// Завершение калибровки насоса
function finishPumpCalibration() {
    const volume = parseFloat(document.getElementById('calibration-volume').value);
    
    if (isNaN(volume) || volume <= 0) {
        showNotification('Пожалуйста, введите корректное значение объема', 'warning');
        return;
    }
    
    // Отправляем запрос на завершение калибровки
    fetch('/api/pump/calibrate/finish', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({
            volume: volume
        }),
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            // Закрываем модальное окно
            document.getElementById('pump-calibration-modal').style.display = 'none';
            
            // Обновляем калибровочный коэффициент в форме
            if (data.calibrationFactor) {
                document.getElementById('pump-calibration-factor').value = data.calibrationFactor.toFixed(3);
            }
            
            showNotification('Калибровка насоса успешно завершена', 'success');
        } else {
            showNotification('Ошибка при завершении калибровки: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при завершении калибровки насоса:', error);
        showNotification('Ошибка при завершении калибровки: ' + error.message, 'error');
    });
}

// Сброс всех настроек
function resetAllSettings() {
    fetch('/api/settings/reset', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            showNotification('Все настройки успешно сброшены к значениям по умолчанию', 'success');
            
            // Перезагружаем все настройки
            loadSettings();
        } else {
            showNotification('Ошибка при сбросе настроек: ' + data.message, 'error');
        }
    })
    .catch(error => {
        console.error('Ошибка при сбросе настроек:', error);
        showNotification('Ошибка при сбросе настроек: ' + error.message, 'error');
    });
}

// Перезагрузка устройства
function rebootDevice() {
    if (confirm('Вы уверены, что хотите перезагрузить устройство?')) {
        fetch('/api/system/reboot', {
            method: 'POST'
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Устройство перезагружается...', 'info');
                
                // Ждем 5 секунд перед попыткой переподключения
                setTimeout(() => {
                    showNotification('Пытаемся переподключиться к устройству...', 'info');
                    
                    // Пытаемся переподключиться через 15 секунд
                    setTimeout(() => {
                        window.location.reload();
                    }, 15000);
                }, 5000);
            } else {
                showNotification('Ошибка при перезагрузке устройства: ' + data.message, 'error');
            }
        })
        .catch(error => {
            console.error('Ошибка при перезагрузке устройства:', error);
            showNotification('Ошибка при перезагрузке устройства: ' + error.message, 'error');
        });
    }
}

// Обновление настроек
function updateSettings(settings) {
    if (settings.system) {
        systemSettings = settings.system;
        updateSystemSettingsForm(settings.system);
    }
    
    if (settings.rectification) {
        rectificationSettings = settings.rectification;
        updateRectificationSettingsForm(settings.rectification);
    }
    
    if (settings.distillation) {
        distillationSettings = settings.distillation;
        updateDistillationSettingsForm(settings.distillation);
    }
    
    if (settings.pump) {
        pumpSettings = settings.pump;
        updatePumpSettingsForm(settings.pump);
    }
}

// Форматирование времени (секунды -> ЧЧ:ММ:СС)
function formatTime(seconds) {
    const hrs = Math.floor(seconds / 3600);
    const mins = Math.floor((seconds % 3600) / 60);
    const secs = seconds % 60;
    
    return [hrs, mins, secs]
        .map(v => v < 10 ? '0' + v : v)
        .join(':');
}

// Форматирование времени работы (секунды -> дни, часы, минуты)
function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    
    let result = '';
    
    if (days > 0) {
        result += days + (days === 1 ? ' день, ' : ' дней, ');
    }
    
    if (hours > 0 || days > 0) {
        result += hours + (hours === 1 ? ' час, ' : ' часов, ');
    }
    
    result += minutes + (minutes === 1 ? ' минута' : ' минут');
    
    return result;
}