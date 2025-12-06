/**
 * @file backup.h
 * @brief Система резервного копирования настроек
 */

#ifndef BACKUP_H
#define BACKUP_H

#include <Arduino.h>

void initBackup();
bool createBackup(const String& backupName = "");
bool restoreBackup(const String& backupName);
bool listBackups();
bool deleteBackup(const String& backupName);
bool autoBackup(); // Автоматическое резервное копирование
int getBackupCount();
String getLatestBackupName();

#endif
