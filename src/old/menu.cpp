#include "menu.h"
#include "display.h"
#include "utils.h"

// Структура для хранения состояния меню
struct MenuState {
    int selectedItem;        // Индекс выбранного пункта
    int scrollPosition;      // Позиция прокрутки (для длинных меню)
    int itemsCount;          // Количество пунктов в меню
};

// Состояния всех меню
static MenuState menuStates[20];

// История навигации по меню
static MenuScreen menuHistory[MAX_MENU_HISTORY];
static int menuHistoryIndex = 0;

// Переменные для экрана подтверждения
static char confirmMessage[64];
static void (*confirmAction)() = NULL;
static MenuScreen returnScreen = MENU_MAIN;

// Меню и их элементы
static const char* menuMainItems[] = {
    "Запустить процесс",
    "Настройки",
    "Информация",
    "Калибровка"
};

static const char* menuProcessItems[] = {
    "Ректификация",
    "Дистилляция"
};

static const char* menuSettingsItems[] = {
    "Настройки ректификации",
    "Настройки дистилляции",
    "Настройки мощности",
    "Настройки датчиков",
    "Системные настройки"
};

static const char* menuRectSettingsItems[] = {
    "Модель ректификации",
    "Температуры",
    "Мощность",
    "Объемы",
    "Орошение",
    "Время стабилизации",
    "Параметры альт. модели"
};

static const char* menuDistSettingsItems[] = {
    "Температуры",
    "Мощность",
    "Отбор голов",
    "Скорость отбора"
};

static const char* menuPowerSettingsItems[] = {
    "Режим управления",
    "Макс. мощность",
    "PI параметры",
    "Настройки PZEM"
};

static const char* menuSystemSettingsItems[] = {
    "Звук",
    "Дисплей",
    "Сеть",
    "Сброс настроек"
};

static const char* menuTempSensorsItems[] = {
    "Датчик куба",
    "Датчик колонны",
    "Датчик продукта",
    "Поиск датчиков",
    "Калибровка датчиков"
};

static const char* menuCalibrationItems[] = {
    "Калибровка насоса",
    "Калибровка датчиков температуры"
};

// Инициализация системы меню
void initMenu() {
    // Инициализируем состояния всех меню
    for (int i = 0; i < 20; i++) {
        menuStates[i] = {0, 0, 0};
    }
    
    // Устанавливаем количество элементов в каждом меню
    menuStates[MENU_MAIN].itemsCount = sizeof(menuMainItems) / sizeof(menuMainItems[0]);
    menuStates[MENU_PROCESS].itemsCount = sizeof(menuProcessItems) / sizeof(menuProcessItems[0]);
    menuStates[MENU_RECT_SETTINGS].itemsCount = sizeof(menuRectSettingsItems) / sizeof(menuRectSettingsItems[0]);
    menuStates[MENU_DIST_SETTINGS].itemsCount = sizeof(menuDistSettingsItems) / sizeof(menuDistSettingsItems[0]);
    menuStates[MENU_POWER_SETTINGS].itemsCount = sizeof(menuPowerSettingsItems) / sizeof(menuPowerSettingsItems[0]);
    menuStates[MENU_SYSTEM_SETTINGS].itemsCount = sizeof(menuSystemSettingsItems) / sizeof(menuSystemSettingsItems[0]);
    menuStates[MENU_TEMP_SENSORS].itemsCount = sizeof(menuTempSensorsItems) / sizeof(menuTempSensorsItems[0]);
    menuStates[MENU_CALIBRATION].itemsCount = sizeof(menuCalibrationItems) / sizeof(menuCalibrationItems[0]);
    
    // Сбрасываем историю
    menuHistoryIndex = 0;
    menuHistory[menuHistoryIndex] = MENU_MAIN;
    
    Serial.println("Система меню инициализирована");
}

// Получить заголовок для текущего экрана меню
const char* getMenuTitle(MenuScreen screen) {
    switch (screen) {
        case MENU_MAIN:
            return "ГЛАВНОЕ МЕНЮ";
        case MENU_PROCESS:
            return "ВЫБОР ПРОЦЕССА";
        case MENU_RECT_SETTINGS:
            return "НАСТРОЙКИ РЕКТИФИКАЦИИ";
        case MENU_DIST_SETTINGS:
            return "НАСТРОЙКИ ДИСТИЛЛЯЦИИ";
        case MENU_POWER_SETTINGS:
            return "НАСТРОЙКИ МОЩНОСТИ";
        case MENU_SYSTEM_SETTINGS:
            return "СИСТЕМНЫЕ НАСТРОЙКИ";
        case MENU_TEMP_SENSORS:
            return "ДАТЧИКИ ТЕМПЕРАТУРЫ";
        case MENU_CALIBRATION:
            return "КАЛИБРОВКА";
        case MENU_INFO:
            return "ИНФОРМАЦИЯ О СИСТЕМЕ";
        case MENU_CONFIRM:
            return "ПОДТВЕРЖДЕНИЕ";
        case SCREEN_TEMPERATURES:
            return "ТЕМПЕРАТУРЫ";
        case SCREEN_POWER:
            return "МОЩНОСТЬ";
        default:
            return "МЕНЮ";
    }
}

// Получить количество элементов в меню
int getMenuItemsCount(MenuScreen screen) {
    return menuStates[screen].itemsCount;
}

// Получить текст элемента меню
const char* getMenuItemText(MenuScreen screen, int index) {
    // Проверка границ
    if (index < 0 || index >= menuStates[screen].itemsCount) {
        return "ОШИБКА";
    }
    
    switch (screen) {
        case MENU_MAIN:
            return menuMainItems[index];
        case MENU_PROCESS:
            return menuProcessItems[index];
        case MENU_RECT_SETTINGS:
            return menuRectSettingsItems[index];
        case MENU_DIST_SETTINGS:
            return menuDistSettingsItems[index];
        case MENU_POWER_SETTINGS:
            return menuPowerSettingsItems[index];
        case MENU_SYSTEM_SETTINGS:
            return menuSystemSettingsItems[index];
        case MENU_TEMP_SENSORS:
            return menuTempSensorsItems[index];
        case MENU_CALIBRATION:
            return menuCalibrationItems[index];
        default:
            return "ПУНКТ МЕНЮ";
    }
}

// Получить текущий выбранный элемент меню
int getSelectedMenuItem(MenuScreen screen) {
    return menuStates[screen].selectedItem;
}

// Получить позицию прокрутки меню
int getMenuScrollPosition(MenuScreen screen) {
    return menuStates[screen].scrollPosition;
}

// Перемещение вверх по меню
void menuNavigateUp(MenuScreen screen) {
    if (menuStates[screen].selectedItem > 0) {
        menuStates[screen].selectedItem--;
        
        // Обновляем позицию прокрутки
        if (menuStates[screen].selectedItem < menuStates[screen].scrollPosition) {
            menuStates[screen].scrollPosition = menuStates[screen].selectedItem;
        }
        
        updateDisplay();
    }
}

// Перемещение вниз по меню
void menuNavigateDown(MenuScreen screen) {
    if (menuStates[screen].selectedItem < menuStates[screen].itemsCount - 1) {
        menuStates[screen].selectedItem++;
        
        // Обновляем позицию прокрутки
        int visibleItems = 5; // Количество видимых элементов в меню
        if (menuStates[screen].selectedItem >= menuStates[screen].scrollPosition + visibleItems) {
            menuStates[screen].scrollPosition = menuStates[screen].selectedItem - visibleItems + 1;
        }
        
        updateDisplay();
    }
}

// Выбор текущего элемента меню
void menuSelectItem(MenuScreen screen) {
    int selectedItem = menuStates[screen].selectedItem;
    MenuScreen nextScreen = MENU_MAIN;
    
    // Обработка в зависимости от текущего экрана меню
    switch (screen) {
        case MENU_MAIN:
            if (selectedItem == 0) {
                // Запустить процесс
                nextScreen = MENU_PROCESS;
            } else if (selectedItem == 1) {
                // Настройки
                nextScreen = MENU_SETTINGS;
            } else if (selectedItem == 2) {
                // Информация
                nextScreen = MENU_INFO;
            } else if (selectedItem == 3) {
                // Калибровка
                nextScreen = MENU_CALIBRATION;
            }
            break;
            
        case MENU_PROCESS:
            if (selectedItem == 0) {
                // Ректификация
                nextScreen = SCREEN_START_RECT;
            } else if (selectedItem == 1) {
                // Дистилляция
                nextScreen = SCREEN_START_DIST;
            }
            break;
            
        case MENU_SETTINGS:
            if (selectedItem == 0) {
                // Настройки ректификации
                nextScreen = MENU_RECT_SETTINGS;
            } else if (selectedItem == 1) {
                // Настройки дистилляции
                nextScreen = MENU_DIST_SETTINGS;
            } else if (selectedItem == 2) {
                // Настройки мощности
                nextScreen = MENU_POWER_SETTINGS;
            } else if (selectedItem == 3) {
                // Настройки датчиков
                nextScreen = MENU_TEMP_SENSORS;
            } else if (selectedItem == 4) {
                // Системные настройки
                nextScreen = MENU_SYSTEM_SETTINGS;
            }
            break;
            
        // Обработка других меню...
        
        default:
            nextScreen = MENU_MAIN;
            break;
    }
    
    // Сохраняем текущий экран в историю и переходим к следующему
    if (menuHistoryIndex < MAX_MENU_HISTORY - 1) {
        menuHistoryIndex++;
        menuHistory[menuHistoryIndex] = screen;
    }
    
    goToScreen(nextScreen);
}

// Возврат в родительское меню
void menuGoBack(MenuScreen screen) {
    MenuScreen parentScreen = getParentScreen(screen);
    
    // Если есть история, возвращаемся на предыдущий экран
    if (menuHistoryIndex > 0) {
        menuHistoryIndex--;
        parentScreen = menuHistory[menuHistoryIndex];
    }
    
    goToScreen(parentScreen);
}

// Получить родительский экран для текущего экрана
MenuScreen getParentScreen(MenuScreen screen) {
    switch (screen) {
        case MENU_PROCESS:
            return MENU_MAIN;
        case MENU_RECT_SETTINGS:
        case MENU_DIST_SETTINGS:
        case MENU_POWER_SETTINGS:
        case MENU_TEMP_SENSORS:
        case MENU_SYSTEM_SETTINGS:
            return MENU_SETTINGS;
        case SCREEN_START_RECT:
        case SCREEN_START_DIST:
            return MENU_PROCESS;
        default:
            return MENU_MAIN;
    }
}

// Установить действие подтверждения
void setConfirmAction(const char* message, void (*action)()) {
    strncpy(confirmMessage, message, sizeof(confirmMessage) - 1);
    confirmMessage[sizeof(confirmMessage) - 1] = '\0';
    confirmAction = action;
    returnScreen = getCurrentScreen();
}

// Выполнить действие подтверждения
void executeConfirmAction(bool confirmed) {
    if (confirmed && confirmAction != NULL) {
        confirmAction();
    }
    
    // Возвращаемся на предыдущий экран
    goToScreen(returnScreen);
    
    // Сбрасываем
    confirmAction = NULL;
}