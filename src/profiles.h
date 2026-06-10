#ifndef PROFILES_H
#define PROFILES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "fs_compat.h"
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
};

struct RectificationParams {
    uint16_t stabilizationMin;
    uint16_t headsVolume;
    uint16_t bodyVolume;
    uint16_t tailsVolume;
    uint16_t headsSpeed;
    uint16_t bodySpeed;
    uint16_t tailsSpeed;
    uint16_t purgeMin;
};

struct DistillationParams {
    uint16_t headsVolume;
    uint16_t targetVolume;
    uint16_t speed;
    float endTemp;
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

struct Profile {
    String id;
    ProfileMetadata metadata;
    ProfileParameters parameters;
    ProfileStatistics statistics;
    ProfileLearningSnapshot learning;
};

struct ProfileListItem {
    String id;
    String name;
    String category;
    uint16_t useCount;
    uint32_t lastUsed;
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
