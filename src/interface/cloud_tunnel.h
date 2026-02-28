/**
 * Smart-Column S3 - Cloud Tunnel (IoT)
 *
 * Исходящий WSS-туннель в облако. Устройство не открывает порт в интернет.
 */
#ifndef CLOUD_TUNNEL_H
#define CLOUD_TUNNEL_H

#include <Arduino.h>

namespace CloudTunnel {
  void init();
  void loop();

  // Claim (PIN) — генерируется локально пользователем
  void generateClaim(uint32_t ttlSeconds = 600);
  bool hasActiveClaim();
  const char* getClaimCode(); // 6-8 digits
  uint32_t getClaimExpiresAt(); // unix seconds

  bool isConnected();
  bool isAuthenticated();
  const char* getDeviceId();
}

#endif

