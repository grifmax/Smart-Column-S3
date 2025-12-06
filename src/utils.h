#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include "config.h"

// Воспроизведение звукового сигнала
void playSound(SoundType type);

// Логирование события
void logEvent(const String& message);

// Отправка уведомления в веб-интерфейс
void sendWebNotification(NotificationType type, const String& message);

// Отправка данных о температуре клиентам
void sendTemperaturesToClients();

// Отправка данных о статусе процесса клиентам
void sendStatusToClients();

// Запуск процесса
void startProcess();

// Остановка процесса
void stopProcess();

// Пауза процесса
void pauseProcess();

// Возобновление процесса
void resumeProcess();

// Преобразование процентов мощности в ватты
int percentToWatts(int percent);

// Преобразование ватт в проценты мощности
int wattsToPercent(int watts);

// Получение имени фазы ректификации на русском
String getPhaseNameRussian(RectificationPhase phase);

// Получение имени фазы дистилляции на русском
String getDistPhaseNameRussian(DistillationPhase phase);

// Получение строки с форматированным временем (ч:м:с)
String getFormattedTime(unsigned long timeInMs);

// Получение строки с форматированным временем работы
String getFormattedUptime();

#endif // UTILS_H