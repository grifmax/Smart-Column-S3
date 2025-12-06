#include "display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "utils.h"
#include "menu.h"

// Создаем экземпляр дисплея
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, DISPLAY_RESET_PIN);

// Глобальные переменные модуля
static MenuScreen currentScreen = MENU_MAIN;
static unsigned long lastScreenUpdateTime = 0;
static unsigned long lastUserInteractionTime = 0;
static bool displayPoweredOn = true;
static bool notificationActive = false;
static unsigned long notificationEndTime = 0;
static char notificationMessage[64] = "";
static NotificationType notificationType = NOTIFY_INFO;

// Определения констант и макросов
#define DISPLAY_UPDATE_INTERVAL 200 // мс
#define LOGO_DISPLAY_TIME 2000      // мс

// Инициализация дисплея
void initDisplay() {
    // Настройка I2C для дисплея
    Wire.begin(DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
    
    // Инициализация дисплея
    if (!display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS)) {
        Serial.println("Ошибка инициализации SSD1306 OLED дисплея!");
        return;
    }
    
    // Начальная настройка дисплея
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    // Применение настроек из системных параметров
    setDisplayBrightness(sysSettings.displaySettings.brightness);
    display.setRotation(sysSettings.displaySettings.rotation);
    
    if (sysSettings.displaySettings.invertColors) {
        display.invertDisplay(true);
    }
    
    // Показать логотип при запуске, если включено
    if (sysSettings.displaySettings.showLogo) {
        showLogo();
        delay(LOGO_DISPLAY_TIME);
    }
    
    // Начальный экран
    drawMainScreen();
    display.display();
    
    Serial.println("OLED дисплей инициализирован успешно");
}

// Очистка дисплея
void clearDisplay() {
    display.clearDisplay();
    display.display();
}

// Обновление дисплея
void updateDisplay() {
    unsigned long currentMillis = millis();
    
    // Проверяем таймаут отключения дисплея
    if (sysSettings.displaySettings.timeout > 0 && 
        currentMillis - lastUserInteractionTime > sysSettings.displaySettings.timeout) {
        if (displayPoweredOn) {
            display.ssd1306_command(SSD1306_DISPLAYOFF);
            displayPoweredOn = false;
        }
        return;
    } else if (!displayPoweredOn) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        displayPoweredOn = true;
    }
    
    // Обновляем дисплей с заданной частотой
    if (currentMillis - lastScreenUpdateTime < DISPLAY_UPDATE_INTERVAL) {
        return;
    }
    
    lastScreenUpdateTime = currentMillis;
    
        // Если активно уведомление и время еще не истекло
    if (notificationActive && currentMillis < notificationEndTime) {
        display.clearDisplay();
        
        // Рамка для уведомления
        display.drawRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, SSD1306_WHITE);
        
        // Иконка типа уведомления
        int iconX = 8;
        int iconY = 8;
        switch (notificationType) {
            case NOTIFY_INFO:
                // Рисуем "i" в круге
                display.drawCircle(iconX + 4, iconY + 4, 4, SSD1306_WHITE);
                display.drawPixel(iconX + 4, iconY + 2, SSD1306_WHITE);
                display.drawLine(iconX + 4, iconY + 4, iconX + 4, iconY + 6, SSD1306_WHITE);
                break;
            case NOTIFY_SUCCESS:
                // Рисуем галочку
                display.drawLine(iconX + 1, iconY + 4, iconX + 3, iconY + 7, SSD1306_WHITE);
                display.drawLine(iconX + 3, iconY + 7, iconX + 7, iconY + 1, SSD1306_WHITE);
                break;
            case NOTIFY_WARNING:
                // Рисуем восклицательный знак в треугольнике
                display.drawLine(iconX + 4, iconY, iconX, iconY + 8, SSD1306_WHITE);
                display.drawLine(iconX, iconY + 8, iconX + 8, iconY + 8, SSD1306_WHITE);
                display.drawLine(iconX + 8, iconY + 8, iconX + 4, iconY, SSD1306_WHITE);
                display.drawPixel(iconX + 4, iconY + 3, SSD1306_WHITE);
                display.drawLine(iconX + 4, iconY + 5, iconX + 4, iconY + 6, SSD1306_WHITE);
                break;
            case NOTIFY_ERROR:
                // Рисуем "X" в круге
                display.drawCircle(iconX + 4, iconY + 4, 4, SSD1306_WHITE);
                display.drawLine(iconX + 2, iconY + 2, iconX + 6, iconY + 6, SSD1306_WHITE);
                display.drawLine(iconX + 6, iconY + 2, iconX + 2, iconY + 6, SSD1306_WHITE);
                break;
        }
        
        // Выводим текст уведомления
        display.setTextSize(1);
        display.setCursor(20, DISPLAY_HEIGHT / 2 - 4);
        display.println(notificationMessage);
        
        display.display();
        return;
    } else if (notificationActive && currentMillis >= notificationEndTime) {
        notificationActive = false;
        // После окончания уведомления перерисовываем текущий экран
    }
    
    // Проверяем таймаут возврата к главному экрану
    handleMenuTimeout();
    
    // Отображаем текущий экран
    display.clearDisplay();
    
    switch (currentScreen) {
        case MENU_MAIN:
            drawMainScreen();
            break;
        case SCREEN_PROCESS:
            if (currentMode == MODE_RECTIFICATION) {
                drawRectificationScreen();
            } else if (currentMode == MODE_DISTILLATION) {
                drawDistillationScreen();
            } else {
                drawMainScreen();
            }
            break;
        case SCREEN_TEMPERATURES:
            drawTemperaturesScreen();
            break;
        case SCREEN_POWER:
            drawPowerScreen();
            break;
        case MENU_PROCESS:
        case MENU_RECT_SETTINGS:
        case MENU_DIST_SETTINGS:
        case MENU_POWER_SETTINGS:
        case MENU_SYSTEM_SETTINGS:
        case MENU_TEMP_SENSORS:
        case MENU_CALIBRATION:
        case MENU_INFO:
            drawMenuScreen(currentScreen);
            break;
        case MENU_CONFIRM:
            // Экран подтверждения должен получать свое сообщение из вызывающего кода
            // поэтому оставляем пустым
            break;
        case SCREEN_START_RECT:
            drawStartRectificationScreen();
            break;
        case SCREEN_START_DIST:
            drawStartDistillationScreen();
            break;
        default:
            drawMainScreen();
            break;
    }
    
    display.display();
}

// Установка яркости дисплея
void setDisplayBrightness(int brightness) {
    // SSD1306 не поддерживает изменение яркости, только контрастность
    // Для полноценного управления яркостью нужно использовать ШИМ для питания дисплея
    // Здесь устанавливаем контрастность
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(constrain(brightness, 0, 255));
}

// Включение/выключение дисплея
void setDisplayEnabled(bool enabled) {
    if (enabled) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        displayPoweredOn = true;
    } else {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        displayPoweredOn = false;
    }
}

// Вывод логотипа при запуске
void showLogo() {
    display.clearDisplay();
    
    // Здесь можно нарисовать логотип или использовать растровое изображение
    // Пример простого логотипа
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 5);
    display.println("DISTILLER");
    
    display.setTextSize(1);
    display.setCursor(25, 30);
    display.println("Ректификация");
    display.setCursor(25, 40);
    display.println("& Дистилляция");
    
    display.setCursor(25, 55);
    display.print("v");
    display.println(FIRMWARE_VERSION);
    
    display.display();
}

// Отрисовка главного экрана с температурами
void drawMainScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("DISTILLER");
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Отображение температур
    display.setTextSize(1);
    
    // Температура в кубе
    display.setCursor(0, 15);
    display.print("Куб: ");
    if (tempSensors.isConnected(TEMP_CUBE)) {
        display.print(temperatures[TEMP_CUBE], 1);
        display.print(" C");
    } else {
        display.print("N/A");
    }
    
    // Температура в колонне
    display.setCursor(0, 25);
    display.print("Колонна: ");
    if (tempSensors.isConnected(TEMP_REFLUX)) {
        display.print(temperatures[TEMP_REFLUX], 1);
        display.print(" C");
    } else {
        display.print("N/A");
    }
    
    // Температура продукта
    display.setCursor(0, 35);
    display.print("Продукт: ");
    if (tempSensors.isConnected(TEMP_PRODUCT)) {
        display.print(temperatures[TEMP_PRODUCT], 1);
        display.print(" C");
    } else {
        display.print("N/A");
    }
    
    // Текущая мощность
    display.setCursor(0, 45);
    display.print("Мощность: ");
    display.print(getCurrentPowerPercent());
    display.print("%");
    
    // Статус системы
    display.setCursor(0, 55);
    if (systemRunning) {
        if (systemPaused) {
            display.print("Пауза - ");
        } else {
            display.print("Работа - ");
        }
        
        switch (currentMode) {
            case MODE_RECTIFICATION:
                display.print("Ректификация");
                break;
            case MODE_DISTILLATION:
                display.print("Дистилляция");
                break;
            default:
                display.print("Режим: ");
                display.print(currentMode);
                break;
        }
    } else {
        display.print("Система в режиме ожидания");
    }
}

// Отрисовка экрана процесса ректификации
void drawRectificationScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("РЕКТИФИКАЦИЯ - ");
    
    // Отображение текущей фазы
    switch (rectPhase) {
        case PHASE_HEATING:
            display.println("НАГРЕВ");
            break;
        case PHASE_STABILIZATION:
            display.println("СТАБИЛИЗ.");
            break;
        case PHASE_HEADS:
            display.println("ГОЛОВЫ");
            break;
        case PHASE_POST_HEADS_STABILIZATION:
            display.println("СТАБ.ПОСЛЕ ГОЛОВ");
            break;
        case PHASE_BODY:
            display.println("ТЕЛО");
            break;
        case PHASE_TAILS:
            display.println("ХВОСТЫ");
            break;
        case PHASE_COMPLETED:
            display.println("ЗАВЕРШЕНО");
            break;
        default:
            display.println("---");
            break;
    }
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Температуры
    display.setCursor(0, 15);
    display.print("Куб:");
    display.setCursor(40, 15);
    if (tempSensors.isConnected(TEMP_CUBE)) {
        display.print(temperatures[TEMP_CUBE], 1);
        display.print("C");
    } else {
        display.print("N/A");
    }
    
    display.setCursor(0, 25);
    display.print("Колонна:");
    display.setCursor(60, 25);
    if (tempSensors.isConnected(TEMP_REFLUX)) {
        display.print(temperatures[TEMP_REFLUX], 1);
        display.print("C");
    } else {
        display.print("N/A");
    }
    
    // Информация о фазе
    display.setCursor(0, 35);
    switch (rectPhase) {
        case PHASE_HEATING:
            display.print("До: ");
            display.print(rectParams.headsTemp, 1);
            display.print("C");
            break;
        case PHASE_STABILIZATION:
            {
                unsigned long stabilizationElapsedMinutes = (millis() - stabilizationStartTime) / 60000;
                unsigned long remainingMinutes = 0;
                if (stabilizationElapsedMinutes < rectParams.stabilizationTime) {
                    remainingMinutes = rectParams.stabilizationTime - stabilizationElapsedMinutes;
                }
                display.print("Осталось: ");
                display.print(remainingMinutes);
                display.print("мин");
            }
            break;
        case PHASE_HEADS:
            display.print("Отобрано: ");
            display.print(headsCollected, 1);
            display.print("мл из ");
            display.print(rectParams.headsVolume, 0);
            display.print("мл");
            break;
        case PHASE_POST_HEADS_STABILIZATION:
            {
                unsigned long postHeadsElapsedMinutes = (millis() - postHeadsStabStartTime) / 60000;
                unsigned long remainingMinutes = 0;
                if (postHeadsElapsedMinutes < rectParams.postHeadsStabilizationTime) {
                    remainingMinutes = rectParams.postHeadsStabilizationTime - postHeadsElapsedMinutes;
                }
                display.print("Осталось: ");
                display.print(remainingMinutes);
                display.print("мин");
            }
            break;
        case PHASE_BODY:
            display.print("Отобрано: ");
            display.print(bodyCollected, 1);
            if (rectParams.model == MODEL_CLASSIC) {
                display.print("мл из ");
                display.print(rectParams.bodyVolume, 0);
                display.print("мл");
            } else {
                display.print("мл");
                display.setCursor(0, 45);
                display.print("Тем.дельта: ");
                display.print(temperatures[TEMP_REFLUX] - stabilizedRefluxTemp, 2);
                display.print("C");
            }
            break;
        case PHASE_TAILS:
            display.print("Отобрано: ");
            display.print(tailsCollected, 1);
            display.print("мл");
            break;
        case PHASE_COMPLETED:
            display.print("Всего отобрано: ");
            display.print(headsCollected + bodyCollected + tailsCollected, 0);
            display.print("мл");
            break;
        default:
            break;
    }
    
    // Мощность и время работы
    display.setCursor(0, 55);
    display.print("Мощ:");
    display.print(getCurrentPowerPercent());
    display.print("% Раб:");
    
    unsigned long processTimeMinutes = (millis() - processStartTime) / 60000;
    unsigned long processTimeHours = processTimeMinutes / 60;
    processTimeMinutes %= 60;
    
    if (processTimeHours > 0) {
        display.print(processTimeHours);
        display.print("ч ");
    }
    display.print(processTimeMinutes);
    display.print("м");
}

// Отрисовка экрана процесса дистилляции
void drawDistillationScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("ДИСТИЛЛЯЦИЯ - ");
    
    // Отображение текущей фазы
    switch (distPhase) {
        case DIST_PHASE_HEATING:
            display.println("НАГРЕВ");
            break;
        case DIST_PHASE_DISTILLATION:
            display.println("ОТБОР");
            break;
        case DIST_PHASE_COMPLETED:
            display.println("ЗАВЕРШЕНО");
            break;
        default:
            display.println("---");
            break;
    }
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Температуры
    display.setCursor(0, 15);
    display.print("Куб:");
    display.setCursor(40, 15);
    if (tempSensors.isConnected(TEMP_CUBE)) {
        display.print(temperatures[TEMP_CUBE], 1);
        display.print("C");
    } else {
        display.print("N/A");
    }
    
    display.setCursor(0, 25);
    display.print("Продукт:");
    display.setCursor(60, 25);
    if (tempSensors.isConnected(TEMP_PRODUCT)) {
        display.print(temperatures[TEMP_PRODUCT], 1);
        display.print("C");
    } else {
        display.print("N/A");
    }
    
    // Информация о фазе
    display.setCursor(0, 35);
    switch (distPhase) {
        case DIST_PHASE_HEATING:
            display.print("До: ");
            display.print(distParams.startCollectingTemp, 1);
            display.print("C");
            break;
        case DIST_PHASE_DISTILLATION:
            display.print("Отобрано: ");
            display.print(distillationCollected, 1);
            display.print("мл");
            break;
        case DIST_PHASE_COMPLETED:
            display.print("Всего отобрано: ");
            display.print(distillationCollected, 1);
            display.print("мл");
            break;
        default:
            break;
    }
    
    // Мощность и время работы
    display.setCursor(0, 55);
    display.print("Мощ:");
    display.print(getCurrentPowerPercent());
    display.print("% Раб:");
    
    unsigned long processTimeMinutes = (millis() - processStartTime) / 60000;
    unsigned long processTimeHours = processTimeMinutes / 60;
    processTimeMinutes %= 60;
    
    if (processTimeHours > 0) {
        display.print(processTimeHours);
        display.print("ч ");
    }
    display.print(processTimeMinutes);
    display.print("м");
}

// Отрисовка текущего экрана меню
void drawMenuScreen(MenuScreen screen) {
    // Заголовок меню
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(getMenuTitle(screen));
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Отрисовка элементов меню
    int menuItemsCount = getMenuItemsCount(screen);
    int startItem = getMenuScrollPosition(screen);
    int selectedItem = getSelectedMenuItem(screen);
    int displayableItems = 5; // Количество отображаемых элементов меню на экране
    
    for (int i = 0; i < displayableItems && (i + startItem) < menuItemsCount; i++) {
        int itemIndex = i + startItem;
        const char* itemText = getMenuItemText(screen, itemIndex);
        
        // Позиция элемента меню
        int y = 15 + i * 10;
        
        // Отображаем индикатор выбранного элемента
        if (itemIndex == selectedItem) {
            display.fillRect(0, y-1, DISPLAY_WIDTH, 10, SSD1306_INVERSE);
        }
        
        display.setCursor(5, y);
        display.print(itemText);
    }
    
    // Индикаторы прокрутки
    if (startItem > 0) {
        // Индикатор прокрутки вверх
        display.fillTriangle(DISPLAY_WIDTH - 10, 15, DISPLAY_WIDTH - 5, 15, DISPLAY_WIDTH - 7, 12, SSD1306_WHITE);
    }
    
    if (startItem + displayableItems < menuItemsCount) {
        // Индикатор прокрутки вниз
        display.fillTriangle(DISPLAY_WIDTH - 10, 55, DISPLAY_WIDTH - 5, 55, DISPLAY_WIDTH - 7, 58, SSD1306_WHITE);
    }
}

// Отрисовка экрана с информацией о системе
void drawInfoScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ИНФОРМАЦИЯ О СИСТЕМЕ");
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Вывод информации
    display.setCursor(0, 15);
    display.print("Версия ПО: ");
    display.println(FIRMWARE_VERSION);
    
    display.setCursor(0, 25);
    display.print("IP: ");
    display.println(WiFi.localIP().toString());
    
    display.setCursor(0, 35);
    display.print("Время работы: ");
    
    unsigned long uptime = millis() / 1000; // в секундах
    unsigned long uptimeHours = uptime / 3600;
    unsigned long uptimeMinutes = (uptime % 3600) / 60;
    
    display.print(uptimeHours);
    display.print("ч ");
    display.print(uptimeMinutes);
    display.println("м");
    
    display.setCursor(0, 45);
    display.print("Своб. память: ");
    display.print(ESP.getFreeHeap() / 1024);
    display.println("кБ");
}

// Отрисовка экрана температур
void drawTemperaturesScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ТЕМПЕРАТУРЫ");
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Температуры и графики
    for (int i = 0; i < MAX_TEMP_SENSORS; i++) {
        int y = 15 + i * 16;
        
        // Название датчика
        display.setCursor(0, y);
        switch (i) {
            case TEMP_CUBE:
                display.print("Куб:");
                break;
            case TEMP_REFLUX:
                display.print("Колонна:");
                break;
            case TEMP_PRODUCT:
                display.print("Продукт:");
                break;
        }
        
        // Значение температуры
        display.setCursor(60, y);
        if (tempSensors.isConnected(i)) {
            display.print(temperatures[i], 1);
            display.print("C");
        } else {
            display.print("N/A");
        }
        
        // Мини-график температуры (последние 30 минут)
        // Это требует хранения истории температур, которое нужно добавить в temp_sensors.cpp
        // Мини-график температуры (последние 30 минут)
        // В упрощенном варианте просто рисуем прямоугольник, высота которого пропорциональна температуре
        int barWidth = 30;
        int barMaxHeight = 10;
        int barHeight = 0;
        
        if (tempSensors.isConnected(i)) {
            float maxTemp = 100.0f; // Максимальная отображаемая температура для графика
            barHeight = (int)((temperatures[i] / maxTemp) * barMaxHeight);
            barHeight = constrain(barHeight, 1, barMaxHeight);
        }
        
        display.fillRect(90, y + barMaxHeight - barHeight, barWidth, barHeight, SSD1306_WHITE);
        display.drawRect(90, y, barWidth, barMaxHeight, SSD1306_WHITE);
    }
}

// Отрисовка экрана мощности
void drawPowerScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("МОЩНОСТЬ НАГРЕВА");
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Текущая мощность
    display.setCursor(0, 15);
    display.print("Текущая: ");
    display.print(getCurrentPowerPercent());
    display.print("% (");
    display.print(getCurrentPowerWatts());
    display.print("Вт)");
    
    // Максимальная мощность
    display.setCursor(0, 25);
    display.print("Максимальная: ");
    display.print(sysSettings.maxHeaterPowerWatts);
    display.print("Вт");
    
    // Режим управления
    display.setCursor(0, 35);
    display.print("Режим: ");
    switch (sysSettings.powerControlMode) {
        case POWER_CONTROL_MANUAL:
            display.print("Ручной");
            break;
        case POWER_CONTROL_PI:
            display.print("PI-регулятор");
            break;
        case POWER_CONTROL_PZEM:
            display.print("По PZEM");
            break;
        default:
            display.print("Неизвестный");
            break;
    }
    
    // Если подключен PZEM, показываем его показания
    if (sysSettings.pzemEnabled) {
        display.setCursor(0, 45);
        display.print("PZEM: ");
        display.print(getPzemPowerWatts(), 0);
        display.print("Вт, ");
        display.print(getPzemVoltage(), 0);
        display.print("В, ");
        display.print(getPzemCurrent(), 2);
        display.print("А");
    }
    
    // Визуализация мощности
    int powerBarWidth = 100;
    int powerBarHeight = 10;
    int fillWidth = (powerBarWidth * getCurrentPowerPercent()) / 100;
    
    display.setCursor(0, 55);
    display.print("0%");
    display.setCursor(powerBarWidth + 10, 55);
    display.print("100%");
    
    // Рамка прогресс-бара
    display.drawRect(10, 55, powerBarWidth, powerBarHeight, SSD1306_WHITE);
    // Заполнение прогресс-бара
    display.fillRect(10, 55, fillWidth, powerBarHeight, SSD1306_WHITE);
}

// Отрисовка экрана запуска ректификации
void drawStartRectificationScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ЗАПУСК РЕКТИФИКАЦИИ");
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Информация о выбранной модели
    display.setCursor(0, 15);
    display.print("Модель: ");
    
    switch (rectParams.model) {
        case MODEL_CLASSIC:
            display.println("Классическая");
            break;
        case MODEL_ALTERNATIVE:
            display.println("Альтернативная");
            break;
        default:
            display.println("Неизвестная");
            break;
    }
    
    // Температуры
    display.setCursor(0, 25);
    display.print("Тем.голов: ");
    display.print(rectParams.headsTemp, 1);
    display.print("C");
    
    display.setCursor(0, 35);
    if (rectParams.model == MODEL_CLASSIC) {
        display.print("Тем.тела: ");
        display.print(rectParams.bodyTemp, 1);
        display.print("C");
        
        display.setCursor(0, 45);
        display.print("Тем.хвостов: ");
        display.print(rectParams.tailsTemp, 1);
        display.print("C");
    } else {
        display.print("Стаб.после голов: ");
        display.print(rectParams.postHeadsStabilizationTime);
        display.print("мин");
        
        display.setCursor(0, 45);
        display.print("Дельта окон.тела: ");
        display.print(rectParams.tempDeltaEndBody, 1);
        display.print("C");
    }
    
    // Инструкция
    display.setCursor(0, 55);
    display.print("OK-пуск BACK-назад");
}

// Отрисовка экрана запуска дистилляции
void drawStartDistillationScreen() {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ЗАПУСК ДИСТИЛЛЯЦИИ");
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Параметры
    display.setCursor(0, 15);
    display.print("Макс.тем.куба: ");
    display.print(distParams.maxCubeTemp, 1);
    display.print("C");
    
    display.setCursor(0, 25);
    display.print("Тем.начала отбора: ");
    display.print(distParams.startCollectingTemp, 1);
    display.print("C");
    
    display.setCursor(0, 35);
    display.print("Тем.окончания: ");
    display.print(distParams.endTemp, 1);
    display.print("C");
    
    // Скорость отбора и мощность
    display.setCursor(0, 45);
    display.print("Скорость: ");
    display.print(distParams.flowRate, 1);
    display.print("мл/ч");
    
    // Инструкция
    display.setCursor(0, 55);
    display.print("OK-пуск BACK-назад");
}

// Отрисовка экрана подтверждения
void drawConfirmScreen(const char* message) {
    // Заголовок
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ПОДТВЕРЖДЕНИЕ");
    
    // Разделительная линия
    display.drawLine(0, 10, DISPLAY_WIDTH, 10, SSD1306_WHITE);
    
    // Сообщение
    display.setCursor(0, 20);
    display.println(message);
    
    // Кнопки
    display.fillRect(0, 45, DISPLAY_WIDTH/2 - 5, 19, SSD1306_INVERSE);
    display.setCursor(10, 50);
    display.print("ОК");
    
    display.fillRect(DISPLAY_WIDTH/2 + 5, 45, DISPLAY_WIDTH/2 - 5, 19, SSD1306_INVERSE);
    display.setCursor(DISPLAY_WIDTH/2 + 15, 50);
    display.print("ОТМЕНА");
}

// Обновление показаний температур на дисплее
void updateDisplayTemperatures() {
    if (currentScreen == MENU_MAIN || currentScreen == SCREEN_TEMPERATURES) {
        updateDisplay();
    }
}

// Обновление информации о процессе на дисплее
void updateDisplayProcess() {
    if (currentScreen == SCREEN_PROCESS) {
        updateDisplay();
    }
}

// Переход к определенному экрану меню
void goToScreen(MenuScreen screen) {
    currentScreen = screen;
    lastUserInteractionTime = millis();
    updateDisplay();
}

// Получение текущего экрана меню
MenuScreen getCurrentScreen() {
    return currentScreen;
}

// Обработка таймаута возврата к главному экрану
void handleMenuTimeout() {
    // Если таймаут активен и прошло достаточно времени бездействия
    if (MENU_TIMEOUT_MS > 0 && 
        (millis() - lastUserInteractionTime > MENU_TIMEOUT_MS)) {
        
        // Не переключаем, если мы уже на главном экране или на экране процесса
        if (currentScreen != MENU_MAIN && currentScreen != SCREEN_PROCESS && 
            currentScreen != SCREEN_TEMPERATURES) {
            
            // Если процесс запущен, переходим на экран процесса, иначе на главный
            if (systemRunning) {
                goToScreen(SCREEN_PROCESS);
            } else {
                goToScreen(MENU_MAIN);
            }
        }
    }
}

// Показать всплывающее уведомление
void showNotification(const char* message, NotificationType type, int durationMs) {
    // Копируем сообщение в буфер
    strncpy(notificationMessage, message, sizeof(notificationMessage) - 1);
    notificationMessage[sizeof(notificationMessage) - 1] = '\0';
    
    notificationType = type;
    notificationActive = true;
    notificationEndTime = millis() + durationMs;
    
    // Обновляем дисплей немедленно
    updateDisplay();
}
/**
 * @brief Показать заставку при запуске
 */
void showSplashScreen() {
    #ifdef DISPLAY_ENABLED
    // Очищаем дисплей
    display.clearDisplay();
    
    // Отображаем логотип
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.println("DISTILLER");
    
    display.setTextSize(1);
    display.setCursor(20, 35);
    display.print("v");
    display.println(FIRMWARE_VERSION);
    
    display.setCursor(15, 50);
    display.println("Loading...");
    
    display.display();
    delay(2000);
    #endif
}
