/**
 * Smart-Column S3 - Security
 *
 * Basic Auth, Rate Limiting, Security Headers
 */

#include "security.h"

#include "config.h"
#include "storage/logger.h"

static String authUsername = "admin";
static String authPassword = "";
static bool authEnabled = false;
static bool rateLimitEnabled = true;
static bool securityHeadersInstalled = false;
static const char* kContentSecurityPolicy =
    "default-src 'self'; "
    "script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; "
    "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; "
    "font-src 'self' data: https://fonts.gstatic.com; "
    "img-src 'self' data: blob:; "
    "connect-src 'self' ws: wss:;";

struct RateLimitEntry {
    IPAddress ip;
    uint32_t lastRequest;
    uint16_t requestCount;
};

static const uint8_t MAX_RATE_LIMIT_ENTRIES = 20;
static RateLimitEntry rateLimitTable[MAX_RATE_LIMIT_ENTRIES];

namespace Security {

void init(const char* username, const char* password) {
    LOG_I("Security: Initializing...");

    if (username && username[0] != '\0') {
        authUsername = String(username);
    } else {
        authUsername = "admin";
    }

    if (password && strlen(password) > 0) {
        authPassword = String(password);
        authEnabled = true;
        LOG_I("Security: Authentication enabled (user: %s)", authUsername.c_str());
    } else {
        authPassword = "";
        authEnabled = false;
        LOG_I("Security: Authentication disabled");
    }

    memset(rateLimitTable, 0, sizeof(rateLimitTable));

    if (!securityHeadersInstalled) {
        DefaultHeaders::Instance().addHeader(
            "Content-Security-Policy",
            kContentSecurityPolicy
        );
        DefaultHeaders::Instance().addHeader("X-Frame-Options", "SAMEORIGIN");
        DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
        DefaultHeaders::Instance().addHeader("X-XSS-Protection", "1; mode=block");
        DefaultHeaders::Instance().addHeader("Referrer-Policy", "strict-origin-when-cross-origin");
        DefaultHeaders::Instance().addHeader(
            "Permissions-Policy",
            "geolocation=(), microphone=(), camera=()"
        );
        securityHeadersInstalled = true;
    }
}

bool checkAuth(AsyncWebServerRequest *request) {
    if (!authEnabled) {
        return true;
    }

    return request->authenticate(authUsername.c_str(), authPassword.c_str(),
                                 "Smart-Column S3");
}

void requestAuth(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response =
        request->beginResponse(401, "text/plain", "Unauthorized");
    response->addHeader("WWW-Authenticate", "Basic realm=\"Smart-Column S3\"");
    request->send(response);
}

bool checkRateLimit(IPAddress ip) {
    if (!rateLimitEnabled) {
        return true;
    }

    uint32_t now = millis();
    const uint32_t RATE_LIMIT_WINDOW = 60000;
    const uint16_t MAX_REQUESTS = 60;

    int8_t entryIndex = -1;
    int8_t oldestIndex = 0;
    uint32_t oldestTime = now;

    for (uint8_t i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (rateLimitTable[i].ip == ip) {
            entryIndex = i;
            break;
        }

        if (rateLimitTable[i].lastRequest < oldestTime) {
            oldestTime = rateLimitTable[i].lastRequest;
            oldestIndex = i;
        }
    }

    if (entryIndex == -1) {
        entryIndex = oldestIndex;
        rateLimitTable[entryIndex].ip = ip;
        rateLimitTable[entryIndex].lastRequest = now;
        rateLimitTable[entryIndex].requestCount = 1;
        return true;
    }

    RateLimitEntry& entry = rateLimitTable[entryIndex];
    if (now - entry.lastRequest > RATE_LIMIT_WINDOW) {
        entry.lastRequest = now;
        entry.requestCount = 1;
        return true;
    }

    entry.requestCount++;
    if (entry.requestCount > MAX_REQUESTS) {
        LOG_W("Security: Rate limit exceeded for IP %s", ip.toString().c_str());
        if (entry.requestCount == MAX_REQUESTS + 1) {
            Logger::logf(1, "Rate limit exceeded for IP %s",
                         ip.toString().c_str());
        }
        return false;
    }

    return true;
}

void addSecurityHeaders(AsyncWebServerResponse *response) {
    response->addHeader("Content-Security-Policy", kContentSecurityPolicy);
    response->addHeader("X-Frame-Options", "SAMEORIGIN");
    response->addHeader("X-Content-Type-Options", "nosniff");
    response->addHeader("X-XSS-Protection", "1; mode=block");
    response->addHeader("Referrer-Policy", "strict-origin-when-cross-origin");
    response->addHeader("Permissions-Policy",
        "geolocation=(), microphone=(), camera=()");
}

void setPassword(const char* password) {
    if (password && strlen(password) > 0) {
        authPassword = String(password);
        authEnabled = true;
        LOG_I("Security: Password updated");
    } else {
        authPassword = "";
        authEnabled = false;
        LOG_I("Security: Password cleared, authentication disabled");
    }
}

void setAuthEnabled(bool enabled) {
    if (enabled && authPassword.length() == 0) {
        LOG_W("Security: Cannot enable auth without password");
        return;
    }

    authEnabled = enabled;
    LOG_I("Security: Authentication %s", enabled ? "enabled" : "disabled");
}

bool isAuthEnabled() {
    return authEnabled;
}

void setRateLimitEnabled(bool enabled) {
    rateLimitEnabled = enabled;
    LOG_I("Security: Rate limit %s", enabled ? "enabled" : "disabled");
}

bool isRateLimitEnabled() {
    return rateLimitEnabled;
}

} // namespace Security
