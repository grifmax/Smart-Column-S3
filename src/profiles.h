#ifndef PROFILES_H
#define PROFILES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "fs_compat.h"
#include "types.h"
#include <vector>

struct ProcessHistory;

#define MAX_PROFILES 100
#define MAX_BUILTIN_PROFILES 10
#define PROFILES_DIR "/profiles"
#define MAX_PROFILE_NAME_LEN 50
#define MAX_PROFILE_DESC_LEN 200

struct ProfileMetadata {
    String name;
    String description;
    String category;
    std::vector<String> tags;
    uint32_t created;
    uint32_t updated;
    String author;
    bool isBuiltin;
};

struct HeaterParams {
    uint16_t maxPower;
    bool autoMode;
    float pidKp;
    float pidKi;
    float pidKd;
    bool boosterEnabled = false;
    float boosterStopCubeTempC = 78.0f;
};

struct RectificationParams {
    uint16_t stabilizationMin = 30;
    uint16_t headsVolume = 300;
    uint16_t bodyVolume = 3200;
    uint16_t tailsVolume = 300;
    uint16_t headsSpeed = 300;
    uint16_t bodySpeed = 600;
    uint16_t tailsSpeed = 360;
    uint16_t purgeMin = 5;
    bool baroCorrectionEnabled = true;
    bool pressureControlEnabled = false;
    uint8_t pressureMinPowerPercent = 30;
    RectTakeoffBackendType takeoffBackendType = RectTakeoffBackendType::PUMP;
    RectRefluxMode refluxMode = RectRefluxMode::ML_H;
    float srTarget = 0.0f;
    uint16_t autonomousCycleSec = 900;
    uint16_t autonomousPauseSec = 90;
    float chimAutoPercent = 0.0f;
    float chimTimePerH = 0.0f;
    float chimBegPercent = 0.0f;
    float chimMinPercent = 35.0f;
    uint8_t usePbMode = 0;
    uint32_t timpPbMs = 15000;
    uint16_t valvePulsePeriodMs = 1000;
    uint16_t valvePulseMinOpenMs = 80;
    uint16_t valvePulseMaxOpenMs = 900;
    uint16_t routingSettlingMs = 1500;
    uint16_t routingRetargetMinMs = 3000;
    uint8_t phasePowerPercent[RECT_POWER_COUNT] = {70, 60, 60, 50};
};

struct DistillationParams {
    uint16_t headsVolume;
    uint16_t targetVolume;
    uint16_t tailsVolume = 0;
    uint16_t speed;
    float endTemp;
    RectTakeoffBackendType takeoffBackendType = RectTakeoffBackendType::PUMP;
    bool valveSafeVentConfirmed = false;
    bool vaporTempControlEnabled = false;
    float vaporTempTargetC = 78.0f;
    uint8_t vaporTempMinPowerPercent = 30;
    uint8_t vaporTempMaxPowerPercent = 100;
    uint16_t vaporTempTimeoutMin = 0;
    FractionProgram fractionProgram;
};

struct MashingStepParams {
    MashStepType type = MashStepType::HEAT_AND_HOLD;
    float temperature = 0.0f;
    uint16_t duration = 0;
    String name;
};

struct MashingParams {
    std::vector<MashingStepParams> steps;
};

struct TemperatureParams {
    float maxCube;
    float maxColumn;
    float headsEnd;
    float bodyStart;
    float bodyEnd;
};

struct SafetyParams {
    uint16_t maxRuntime;
    float waterFlowMin;
    uint16_t pressureMax;
};

struct ProfileParameters {
    String mode;
    String model;
    HeaterParams heater;
    RectificationParams rectification;
    DistillationParams distillation;
    MashingParams mashing;
    TemperatureParams temperatures;
    SafetyParams safety;
};

struct ProfileStatistics {
    uint16_t useCount;
    uint32_t lastUsed;
    uint32_t avgDuration;
    uint16_t avgYield;
    float successRate;
};

struct ProfileLearningSnapshot {
    uint16_t successfulRuns = 0;
    uint16_t failedRuns = 0;
    float avgEnergyUsed = 0.0f;
    float avgEnergyPerLiter = 0.0f;
    float avgProcessHealth = 0.0f;
    float avgStabilityIndex = 0.0f;
    float typicalCubeFinalTemp = 0.0f;
    float typicalColumnTopFinalTemp = 0.0f;
    String lastProcessId;
    String lastSuccessfulProcessId;
};

struct ProfileValidationSnapshot {
    uint32_t validatedAt = 0;
    String sourceProcessId;
    float atmosphereHpa = 0.0f;
    float atmosphereMmHg = 0.0f;
    uint16_t columnHeightMm = 0;
    String packingType;
    float packingCoeff = 0.0f;
    uint16_t heaterPowerW = 0;
    uint16_t targetPowerW = 0;
    float feedVolumeL = 0.0f;
    float feedAbvPercent = 0.0f;
    float cubeChargePercent = 0.0f;
    uint16_t headsActualMl = 0;
    uint16_t bodyActualMl = 0;
    uint16_t tailsActualMl = 0;
    float headsCutColumnTopC = 0.0f;
    float bodyCutColumnTopC = 0.0f;
    float tailsCutColumnTopC = 0.0f;
    float cubeFinalC = 0.0f;
    float columnTopFinalC = 0.0f;
    float avgStabilityIndex = 0.0f;
    float avgProcessHealth = 0.0f;
    String equipmentSnapshotJson;
};

struct ProfileBaroCorrectionSummary {
    bool enabled = false;
    bool applicable = false;
    bool applied = false;
    float baselinePressureMmHg = 0.0f;
    float currentPressureMmHg = 0.0f;
    float pressureDeltaMmHg = 0.0f;
    float boilingShiftC = 0.0f;
    float appliedShiftC = 0.0f;
    float strength = 0.0f;
    float maxShiftC = 0.0f;
    String note;
};

struct Profile {
    String id;
    ProfileMetadata metadata;
    ProfileParameters parameters;
    ProfileStatistics statistics;
    ProfileLearningSnapshot learning;
    ProfileValidationSnapshot validation;
};

struct ProfileListItem {
    String id;
    String name;
    String description;
    String category;
    std::vector<String> tags;
    String author;
    uint16_t useCount;
    uint32_t lastUsed;
    uint32_t updated = 0;
    float successRate = 0.0f;
    uint16_t successfulRuns = 0;
    bool isBuiltin;
};

bool initProfiles();
bool saveProfile(const Profile& profile);
bool loadProfile(const String& id, Profile& profile);
std::vector<ProfileListItem> getProfileList();
bool deleteProfile(const String& id);
bool clearProfiles();
bool loadBuiltinProfiles();
void rotateProfiles();
uint16_t getProfileCount();
bool validateProfile(const Profile& profile);
bool applyProfile(const String& id);
void updateProfileStatistics(const String& id, bool success, uint32_t duration, uint16_t yield);
void updateProfileLearning(const ProcessHistory& history);
ProfileBaroCorrectionSummary evaluateProfileBaroCorrection(
    const Profile& profile,
    int enabledOverride = -1);
String buildValidationEquipmentSnapshotJson();
void appendProfileValidationJson(JsonObject validation, const Profile& profile);
TemperatureParams getEffectiveProfileTemperatures(
    const Profile& profile,
    ProfileBaroCorrectionSummary* summary = nullptr,
    int enabledOverride = -1);
void setActiveProfile(const String& id, const String& name);
void clearActiveProfile();
String getActiveProfileId();
String getActiveProfileName();
String createProfileFromSettings(const String& name, const String& description, const String& category);
String exportProfileToJSON(const String& id);
String exportAllProfilesToJSON(bool includeBuiltin = false);
String importProfileFromJSON(const String& jsonStr);
uint16_t importProfilesFromJSON(const String& jsonStr);

#endif // PROFILES_H
