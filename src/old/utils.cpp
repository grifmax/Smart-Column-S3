/**
 * @file utils.cpp
 * @brief Реализация вспомогательных функций
 */

#include "utils.h"
#include <Arduino.h>

/**
 * @brief Карта символов для преобразования в hex
 */
static const char hexMap[] = "0123456789ABCDEF";

/**
 * @brief Преобразует байт в шестнадцатеричную строку
 * 
 * @param val Значение для преобразования
 * @param buffer Буфер для записи результата (минимум 3 символа)
 */
void byteToHexStr(byte val, char* buffer) {
    buffer[0] = hexMap[(val & 0xF0) >> 4];
    buffer[1] = hexMap[val & 0x0F];
    buffer[2] = '\0';
}

/**
 * @brief Преобразует массив байтов в шестнадцатеричную строку
 * 
 * @param data Массив байтов
 * @param length Длина массива
 * @param buffer Буфер для записи результата (минимум 2*length + 1 символов)
 */
void bytesToHexStr(const byte* data, int length, char* buffer) {
    for (int i = 0; i < length; i++) {
        buffer[i*2] = hexMap[(data[i] & 0xF0) >> 4];
        buffer[i*2+1] = hexMap[data[i] & 0x0F];
    }
    buffer[length * 2] = '\0';
}

/**
 * @brief Преобразует строку времени в секунды
 * 
 * @param timeStr Строка в формате "ЧЧ:ММ:СС"
 * @return Количество секунд
 */
unsigned long timeStrToSeconds(const String& timeStr) {
    int hours = 0, minutes = 0, seconds = 0;
    
    int firstColon = timeStr.indexOf(':');
    int lastColon = timeStr.lastIndexOf(':');
    
    if (firstColon > 0) {
        hours = timeStr.substring(0, firstColon).toInt();
        
        if (lastColon > firstColon) {
            minutes = timeStr.substring(firstColon + 1, lastColon).toInt();
            seconds = timeStr.substring(lastColon + 1).toInt();
        } else {
            minutes = timeStr.substring(firstColon + 1).toInt();
        }
    }
    
    return hours * 3600 + minutes * 60 + seconds;
}

/**
 * @brief Форматирует секунды в строку времени
 * 
 * @param seconds Количество секунд
 * @return Строка в формате "ЧЧ:ММ:СС"
 */
String secondsToTimeStr(unsigned long seconds) {
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;
    
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, secs);
    
    return String(timeStr);
}

/**
 * @brief Экспоненциальный фильтр для сглаживания значений
 * 
 * @param prevValue Предыдущее сглаженное значение
 * @param newValue Новое значение для сглаживания
 * @param alpha Коэффициент фильтрации (0.0 - 1.0)
 * @return Новое сглаженное значение
 */
float exponentialFilter(float prevValue, float newValue, float alpha) {
    return alpha * newValue + (1.0 - alpha) * prevValue;
}

/**
 * @brief Проверяет, находится ли число в заданном диапазоне
 * 
 * @param value Проверяемое значение
 * @param min Минимальное значение диапазона
 * @param max Максимальное значение диапазона
 * @return true если значение в диапазоне [min, max]
 */
bool isInRange(float value, float min, float max) {
    return value >= min && value <= max;
}

/**
 * @brief Ограничивает значение в заданном диапазоне
 * 
 * @param value Исходное значение
 * @param min Минимальное значение диапазона
 * @param max Максимальное значение диапазона
 * @return Ограниченное значение в диапазоне [min, max]
 */
float constrainValue(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief Вычисляет процентное соотношение
 * 
 * @param value Текущее значение
 * @param min Минимальное значение (0%)
 * @param max Максимальное значение (100%)
 * @return Процентное соотношение (0.0 - 100.0)
 */
float calculatePercent(float value, float min, float max) {
    if (max == min) return 0.0;
    float result = ((value - min) / (max - min)) * 100.0;
    return constrainValue(result, 0.0, 100.0);
}

/**
 * @brief Вычисляет скользящее среднее
 * 
 * @param buffer Буфер значений
 * @param size Размер буфера
 * @return Среднее значение
 */
float calculateAverage(float* buffer, int size) {
    float sum = 0.0;
    int count = 0;
    
    for (int i = 0; i < size; i++) {
        if (!isnan(buffer[i])) {
            sum += buffer[i];
            count++;
        }
    }
    
    if (count == 0) return 0.0;
    return sum / count;
}

/**
 * @brief Вычисляет время, прошедшее с заданного момента
 * 
 * @param startTime Начальное время в миллисекундах
 * @return Прошедшее время в секундах
 */
unsigned long calculateElapsedTime(unsigned long startTime) {
    if (startTime == 0) return 0;
    return (millis() - startTime) / 1000;
}

/**
 * @brief Преобразует MAC-адрес в строку
 * 
 * @param mac MAC-адрес (6 байт)
 * @return Строковое представление MAC-адреса
 */
String macToString(const uint8_t* mac) {
    char buf[18];
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

/**
 * @brief Вычисляет контрольную сумму CRC8 для массива данных
 * 
 * @param data Массив данных
 * @param len Длина массива
 * @return Контрольная сумма CRC8
 */
uint8_t calculateCRC8(const uint8_t *data, uint16_t len) {
    uint8_t crc = 0x00;
    
    while (len--) {
        uint8_t extract = *data++;
        for (uint8_t i = 8; i; i--) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) {
                crc ^= 0x8C;
            }
            extract >>= 1;
        }
    }
    
    return crc;
}

/**
 * @brief Вычисляет скорость изменения значения
 * 
 * @param oldValue Предыдущее значение
 * @param newValue Новое значение
 * @param deltaTimeMs Интервал времени между измерениями (мс)
 * @return Скорость изменения (единиц в секунду)
 */
float calculateRateOfChange(float oldValue, float newValue, unsigned long deltaTimeMs) {
    if (deltaTimeMs == 0) return 0.0;
    return ((newValue - oldValue) * 1000.0) / deltaTimeMs;
}

/**
 * @brief Преобразует строку в булево значение
 * 
 * @param str Строка ("true", "on", "1" -> true)
 * @return Булево значение
 */
bool stringToBool(const String& str) {
    String lowStr = str;
    lowStr.toLowerCase();
    return (lowStr == "true" || lowStr == "on" || lowStr == "1" || lowStr == "yes");
}

/**
 * @brief Преобразует булево значение в строку
 * 
 * @param value Булево значение
 * @return "true" или "false"
 */
String boolToString(bool value) {
    return value ? "true" : "false";
}
