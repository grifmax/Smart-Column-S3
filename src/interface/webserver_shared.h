#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../control/safety.h"
#include "../control/v2/status_adapter.h"
#include "../types.h"

extern SystemState g_state;
extern Settings g_settings;
extern EnergyHistory g_energyHistory;

bool hasConfiguredWiFi();

const char *getModeString(Mode mode);
const char *getPhaseString(RectPhase phase);
const char *getMashPhaseString(MashPhase phase);
const char *getNbkPhaseString(NbkPhase phase);
const char *getFermPhaseString(FermentationPhase phase);
const char *getFractionToken(Fraction fraction);
const char *getFractionLabel(Fraction fraction);

const char *getTempSensorLabel(uint8_t index);
const char *getTempSensorRoleKey(uint8_t index);
void formatTempAddress(const uint8_t address[8], char *buffer,
                       size_t bufferSize);
bool parseTempAddressString(const char *value, uint8_t address[8]);
void appendTempSensorMeta(JsonObject obj, uint8_t index);

void syncStirrerState();
void fillStirrerJson(JsonObject stirrer, const SystemState &state);
void sendStirrerStateResponse(AsyncWebServerRequest *request, int statusCode,
                              bool success, const char *message);
bool ensureStirrerReady(AsyncWebServerRequest *request);
bool handleSecurityGate(AsyncWebServerRequest *request);
void applySecuritySettings();
bool parseRequestedMode(const char *modeStr, Mode &mode);
void applyBoosterStartOverride(JsonObject params, Settings &settings);
bool buildProcessPreflight(JsonDocument &doc, Mode mode, const char *modeStr,
                           JsonObject params);
void fillAlarmJson(JsonObject alarm, const SystemState &state,
                   const Settings &settings);
void fillV2StatusJson(JsonObject v2, const ControlV2::ModeStatusV2 &status,
                      const ControlV2::MetricsSnapshotV2 &metrics);
void fillSafetyActionV2Json(JsonObject v2,
                            const ControlV2::ModeStatusV2 &status,
                            const ControlV2::MetricsSnapshotV2 &metrics);
String buildBlockingRequiredSensorsList(
    const Safety::RequiredSensorsMask &required, const SystemState &state,
    bool includePressure);
String buildMissingRequiredSensorsList(Mode mode, const Settings &settings,
                                       const SystemState &state);
String buildStartupMissingSensorsList(
    const Safety::RequiredSensorsMask &required, const SystemState &state,
    bool includePressure);
void fillTemperatureTopologyJson(JsonObject topology,
                                 const EquipmentSettings &equipment);
void fillTemperatureModeSupportJson(JsonObject modes,
                                    const Settings &settings);
void fillEquipmentModulesJson(JsonObject modules);
void fillSafetyChannelsJson(JsonObject channels);

uint8_t clampU8Range(uint32_t value, uint8_t minValue, uint8_t maxValue);
uint16_t clampU16Range(uint32_t value, uint16_t minValue, uint16_t maxValue);
float clampFloatRange(float value, float minValue, float maxValue);
bool collectRequestBody(AsyncWebServerRequest *request, const uint8_t *data,
                        size_t len, size_t index, size_t total, String &body);
