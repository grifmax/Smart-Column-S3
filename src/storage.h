#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include "config.h"

// Инициализация системы хранения
void initStorage();

// Сохранение системных настроек
bool saveSystemSettings();

// Загрузка системных настроек
bool loadSystemSettings();

// Сохранение параметров ректификации
bool saveRectificationParams();

// Загрузка параметров ректификации
bool loadRectificationParams();

// Сохранение параметров дистилляции
bool saveDistillationParams();

// Загрузка параметров дистилляции
bool loadDistillationParams();

// Сохранение настроек насоса
bool savePumpSettings();

// Загрузка настроек насоса
bool loadPumpSettings();

// Сброс всех настроек к значениям по умолчанию
bool resetAllSettings();

// Проверка, инициализированы ли настройки
bool areSettingsInitialized();

// Установка значений по умолчанию для системных настроек
void setDefaultSystemSettings();

// Установка значений по умолчанию для параметров ректификации
void setDefaultRectificationParams();

// Установка значений по умолчанию для параметров дистилляции
void setDefaultDistillationParams();

// Установка значений по умолчанию для настроек насоса
void setDefaultPumpSettings();

#endif // STORAGE_H