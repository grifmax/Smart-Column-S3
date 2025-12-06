/**
 * @file ota_update.h
 * @brief OTA (Over-The-Air) обновления прошивки
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

void initOTA();
void handleOTA();
bool startOTAUpdate(const String& firmwareURL);
int getOTAProgress();
bool isOTAInProgress();
void setOTAPassword(const String& password);
void enableOTA(bool enabled);

#endif
