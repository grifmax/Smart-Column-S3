#include "wifi_profiles.h"

#include <WiFi.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "storage/logger.h"

namespace {

bool isProfileUsable(const WiFiProfile& profile) {
  return profile.enabled && profile.ssid[0] != '\0';
}

const WiFiProfile* firstEnabledProfile(const WiFiSettings& wifi) {
  const uint8_t count = (wifi.profileCount > WIFI_MAX_PROFILES) ? WIFI_MAX_PROFILES : wifi.profileCount;
  for (uint8_t i = 0; i < count; ++i) {
    if (isProfileUsable(wifi.profiles[i])) {
      return &wifi.profiles[i];
    }
  }
  return nullptr;
}

void clearProfile(WiFiProfile& profile) {
  memset(&profile, 0, sizeof(profile));
  profile.enabled = true;
  strlcpy(profile.subnet, "255.255.255.0", sizeof(profile.subnet));
}

bool parseIpOrEmpty(const char* value, IPAddress& outIp) {
  if (!value || value[0] == '\0') {
    return false;
  }
  return outIp.fromString(value);
}

void ensureApStaMode() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  IPAddress apIp(192, 168, 4, 1);
  IPAddress apGw(192, 168, 4, 1);
  IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apGw, apMask);

  if (WiFi.softAPIP() != apIp) {
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  }
}

bool applyStationConfig(const WiFiProfile& profile) {
  if (!profile.useStaticIp) {
    return WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  if (!ip.fromString(profile.ip) || !gateway.fromString(profile.gateway) ||
      !subnet.fromString(profile.subnet)) {
    LOG_W("WiFi: invalid static IP settings for SSID %s, falling back to DHCP", profile.ssid);
    return WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  IPAddress dns1;
  IPAddress dns2;
  const bool hasDns1 = parseIpOrEmpty(profile.dns1, dns1);
  const bool hasDns2 = parseIpOrEmpty(profile.dns2, dns2);

  if (hasDns1 || hasDns2) {
    return WiFi.config(ip, gateway, subnet, hasDns1 ? dns1 : INADDR_NONE,
                       hasDns2 ? dns2 : INADDR_NONE);
  }

  return WiFi.config(ip, gateway, subnet);
}

bool waitForConnection(uint32_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    delay(100);
    esp_task_wdt_reset();
  }
  return WiFi.status() == WL_CONNECTED;
}

}  // namespace

namespace WiFiProfiles {

bool hasConfiguredProfiles(const WiFiSettings& wifi) {
  return firstEnabledProfile(wifi) != nullptr;
}

int findProfileIndex(const WiFiSettings& wifi, const char* ssid) {
  if (!ssid || ssid[0] == '\0') return -1;

  const uint8_t count = (wifi.profileCount > WIFI_MAX_PROFILES) ? WIFI_MAX_PROFILES : wifi.profileCount;
  for (uint8_t i = 0; i < count; ++i) {
    if (strcmp(wifi.profiles[i].ssid, ssid) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool getProfileBySsid(const WiFiSettings& wifi, const char* ssid, WiFiProfile& outProfile) {
  const int index = findProfileIndex(wifi, ssid);
  if (index < 0) return false;
  outProfile = wifi.profiles[index];
  return true;
}

void compactProfiles(WiFiSettings& wifi) {
  WiFiProfile compacted[WIFI_MAX_PROFILES];
  for (uint8_t i = 0; i < WIFI_MAX_PROFILES; ++i) {
    clearProfile(compacted[i]);
  }

  uint8_t next = 0;
  const uint8_t count = (wifi.profileCount > WIFI_MAX_PROFILES) ? WIFI_MAX_PROFILES : wifi.profileCount;
  for (uint8_t i = 0; i < count && next < WIFI_MAX_PROFILES; ++i) {
    const WiFiProfile& profile = wifi.profiles[i];
    if (!isProfileUsable(profile)) continue;

    bool duplicate = false;
    for (uint8_t j = 0; j < next; ++j) {
      if (strcmp(compacted[j].ssid, profile.ssid) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    compacted[next] = profile;
    ++next;
  }

  for (uint8_t i = 0; i < WIFI_MAX_PROFILES; ++i) {
    wifi.profiles[i] = compacted[i];
  }
  wifi.profileCount = next;
}

void syncLegacyFields(WiFiSettings& wifi, const WiFiProfile* preferred) {
  const WiFiProfile* selected = nullptr;

  if (preferred && isProfileUsable(*preferred)) {
    selected = preferred;
  } else {
    const int currentIndex = findProfileIndex(wifi, wifi.ssid);
    if (currentIndex >= 0) {
      selected = &wifi.profiles[currentIndex];
    } else {
      selected = firstEnabledProfile(wifi);
    }
  }

  if (!selected) {
    wifi.ssid[0] = '\0';
    wifi.password[0] = '\0';
    return;
  }

  strlcpy(wifi.ssid, selected->ssid, sizeof(wifi.ssid));
  strlcpy(wifi.password, selected->password, sizeof(wifi.password));
}

bool upsertProfile(WiFiSettings& wifi, const WiFiProfile& profile, bool makePreferred) {
  if (!profile.ssid[0]) return false;

  compactProfiles(wifi);

  int index = findProfileIndex(wifi, profile.ssid);
  if (index < 0) {
    if (wifi.profileCount >= WIFI_MAX_PROFILES) {
      return false;
    }
    index = wifi.profileCount++;
    clearProfile(wifi.profiles[index]);
    strlcpy(wifi.profiles[index].ssid, profile.ssid, sizeof(wifi.profiles[index].ssid));
  }

  WiFiProfile& target = wifi.profiles[index];
  target.enabled = profile.enabled;
  strlcpy(target.ssid, profile.ssid, sizeof(target.ssid));
  if (profile.password[0] != '\0' || target.password[0] == '\0') {
    strlcpy(target.password, profile.password, sizeof(target.password));
  }
  target.useStaticIp = profile.useStaticIp;
  strlcpy(target.ip, profile.ip, sizeof(target.ip));
  strlcpy(target.gateway, profile.gateway, sizeof(target.gateway));
  strlcpy(target.subnet, profile.subnet[0] ? profile.subnet : "255.255.255.0", sizeof(target.subnet));
  strlcpy(target.dns1, profile.dns1, sizeof(target.dns1));
  strlcpy(target.dns2, profile.dns2, sizeof(target.dns2));

  if (makePreferred && index > 0) {
    WiFiProfile promoted = target;
    for (int i = index; i > 0; --i) {
      wifi.profiles[i] = wifi.profiles[i - 1];
    }
    wifi.profiles[0] = promoted;
    index = 0;
  }

  syncLegacyFields(wifi, makePreferred ? &wifi.profiles[index] : nullptr);
  return true;
}

bool deleteProfile(WiFiSettings& wifi, const char* ssid) {
  compactProfiles(wifi);

  const int index = findProfileIndex(wifi, ssid);
  if (index < 0) return false;

  for (uint8_t i = index; i + 1 < wifi.profileCount; ++i) {
    wifi.profiles[i] = wifi.profiles[i + 1];
  }
  if (wifi.profileCount > 0) {
    --wifi.profileCount;
    clearProfile(wifi.profiles[wifi.profileCount]);
  }

  syncLegacyFields(wifi);
  return true;
}

bool moveProfile(WiFiSettings& wifi, const char* ssid, int direction) {
  compactProfiles(wifi);

  const int index = findProfileIndex(wifi, ssid);
  if (index < 0) return false;

  const int target = index + direction;
  if (target < 0 || target >= wifi.profileCount) return false;

  WiFiProfile tmp = wifi.profiles[index];
  wifi.profiles[index] = wifi.profiles[target];
  wifi.profiles[target] = tmp;
  syncLegacyFields(wifi);
  return true;
}

bool beginConnection(const WiFiProfile& profile) {
  if (!isProfileUsable(profile)) return false;

  ensureApStaMode();
  WiFi.disconnect();
  delay(100);

  if (!applyStationConfig(profile)) {
    LOG_W("WiFi: failed to apply STA config for %s", profile.ssid);
  }

  LOG_I("WiFi: connecting to profile %s", profile.ssid);
  WiFi.begin(profile.ssid, profile.password);
  return true;
}

bool connectProfileBlocking(WiFiSettings& wifi, const WiFiProfile& profile, uint32_t timeoutMs) {
  if (!beginConnection(profile)) return false;

  if (waitForConnection(timeoutMs)) {
    syncLegacyFields(wifi, &profile);
    LOG_I("WiFi: connected to %s, IP: %s", profile.ssid, WiFi.localIP().toString().c_str());
    return true;
  }

  LOG_W("WiFi: failed to connect to %s", profile.ssid);
  return false;
}

bool connectBestAvailable(WiFiSettings& wifi, uint32_t timeoutMs) {
  compactProfiles(wifi);
  if (!hasConfiguredProfiles(wifi)) {
    return false;
  }

  LOG_I("WiFi: scanning saved networks");
  const int networksFound = WiFi.scanNetworks();
  bool available[WIFI_MAX_PROFILES] = {false};

  if (networksFound > 0) {
    for (int i = 0; i < networksFound; ++i) {
      const String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      const int profileIndex = findProfileIndex(wifi, ssid.c_str());
      if (profileIndex >= 0) {
        available[profileIndex] = true;
      }
    }
  }
  WiFi.scanDelete();

  for (uint8_t i = 0; i < wifi.profileCount; ++i) {
    if (available[i] && connectProfileBlocking(wifi, wifi.profiles[i], timeoutMs)) {
      return true;
    }
  }

  WiFiProfile fallback;
  if (getProfileBySsid(wifi, wifi.ssid, fallback)) {
    return connectProfileBlocking(wifi, fallback, timeoutMs);
  }

  const WiFiProfile* firstProfile = firstEnabledProfile(wifi);
  if (firstProfile) {
    return connectProfileBlocking(wifi, *firstProfile, timeoutMs);
  }

  return false;
}

}  // namespace WiFiProfiles
