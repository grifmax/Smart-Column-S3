#ifndef WIFI_PROFILES_H
#define WIFI_PROFILES_H

#include <Arduino.h>
#include "types.h"

namespace WiFiProfiles {

bool hasConfiguredProfiles(const WiFiSettings& wifi);
int findProfileIndex(const WiFiSettings& wifi, const char* ssid);
bool getProfileBySsid(const WiFiSettings& wifi, const char* ssid, WiFiProfile& outProfile);
void compactProfiles(WiFiSettings& wifi);
void syncLegacyFields(WiFiSettings& wifi, const WiFiProfile* preferred = nullptr);

bool upsertProfile(WiFiSettings& wifi, const WiFiProfile& profile, bool makePreferred);
bool deleteProfile(WiFiSettings& wifi, const char* ssid);
bool moveProfile(WiFiSettings& wifi, const char* ssid, int direction);

bool beginConnection(const WiFiProfile& profile);
bool connectProfileBlocking(WiFiSettings& wifi, const WiFiProfile& profile, uint32_t timeoutMs);
bool connectBestAvailable(WiFiSettings& wifi, uint32_t timeoutMs);

}

#endif
