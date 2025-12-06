/**
 * @file notification.h
 * @brief Система уведомлений (звук/вибрация/LED)
 */

#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <Arduino.h>

enum NotificationType {
    NOTIFY_INFO,
    NOTIFY_WARNING,
    NOTIFY_ERROR,
    NOTIFY_PHASE_CHANGE,
    NOTIFY_PROCESS_COMPLETE
};

void initNotifications();
void playNotification(NotificationType type);
void playMelody(const int* melody, int length);
void beep(int frequency, int duration);
void setNotificationsEnabled(bool enabled);

#endif
