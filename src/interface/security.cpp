/**
 * Smart-Column S3 - Security
 *
 * Basic Auth, Rate Limiting, Security Headers
 */

#include "security.h"
#include <mbedtls/base64.h>
#include "config.h"

// Настройки
static String authUsername = "admin";
static String authPassword = "";
static bool authEnabled = false;

// Rate Limiting (простая имплементация)
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

    if (username) {
        authUsername = String(username);
    }

    if (password && strlen(password) > 0) {
        authPassword = String(password);
        authEnabled = true;
        LOG_I("Security: Authentication enabled (user: %s)", authUsername.c_str());
    } else {
        authEnabled = false;
        LOG_I("Security: Authentication disabled");
    }

    // Инициализация rate limit таблицы
    memset(rateLimitTable, 0, sizeof(rateLimitTable));
}

bool checkAuth(AsyncWebServerRequest *request) {
    if (!authEnabled) {
        return true;  // Аутентификация отключена
    }

    if (!request->hasHeader("Authorization")) {
        return false;
    }

    String authHeader = request->header("Authorization");

    if (!authHeader.startsWith("Basic ")) {
        return false;
    }

    // Извлечь base64 часть
    String auth = authHeader.substring(6);
    auth.trim();

    // Декодировать base64
    size_t outputLen;
    unsigned char decoded[128];

    int ret = mbedtls_base64_decode(decoded, sizeof(decoded), &outputLen,
                                     (const unsigned char*)auth.c_str(), auth.length());

    if (ret != 0) {
        return false;
    }

    // Преобразовать в строку
    String credentials = String((char*)decoded);

    // Разделить на username:password
    int colonIndex = credentials.indexOf(':');
    if (colonIndex == -1) {
        return false;
    }

    String user = credentials.substring(0, colonIndex);
    String pass = credentials.substring(colonIndex + 1);

    // Проверка credentials
    return (user == authUsername && pass == authPassword);
}

void requestAuth(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(401, "text/plain", "Unauthorized");
    response->addHeader("WWW-Authenticate", "Basic realm=\"Smart-Column S3\"");
    addSecurityHeaders(response);
    request->send(response);
}

bool checkRateLimit(IPAddress ip) {
    uint32_t now = millis();
    const uint32_t RATE_LIMIT_WINDOW = 60000;  // 1 минута
    const uint16_t MAX_REQUESTS = 60;           // 60 запросов в минуту

    // Найти запись для этого IP
    int8_t entryIndex = -1;
    int8_t oldestIndex = 0;
    uint32_t oldestTime = now;

    for (uint8_t i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (rateLimitTable[i].ip == ip) {
            entryIndex = i;
            break;
        }

        // Поиск самой старой записи для возможной замены
        if (rateLimitTable[i].lastRequest < oldestTime) {
            oldestTime = rateLimitTable[i].lastRequest;
            oldestIndex = i;
        }
    }

    // Если IP не найден, создать новую запись
    if (entryIndex == -1) {
        entryIndex = oldestIndex;
        rateLimitTable[entryIndex].ip = ip;
        rateLimitTable[entryIndex].lastRequest = now;
        rateLimitTable[entryIndex].requestCount = 1;
        return true;
    }

    RateLimitEntry& entry = rateLimitTable[entryIndex];

    // Проверка окна времени
    if (now - entry.lastRequest > RATE_LIMIT_WINDOW) {
        // Окно истекло, сбросить счётчик
        entry.lastRequest = now;
        entry.requestCount = 1;
        return true;
    }

    // Увеличить счётчик
    entry.requestCount++;

    // Проверка лимита
    if (entry.requestCount > MAX_REQUESTS) {
        LOG_W("Security: Rate limit exceeded for IP %s", ip.toString().c_str());
        return false;
    }

    return true;
}

void addSecurityHeaders(AsyncWebServerResponse *response) {
    // Content Security Policy
    response->addHeader("Content-Security-Policy",
        "default-src 'self'; "
        "script-src 'self' 'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data:;");

    // X-Frame-Options (защита от clickjacking)
    response->addHeader("X-Frame-Options", "DENY");

    // X-Content-Type-Options
    response->addHeader("X-Content-Type-Options", "nosniff");

    // X-XSS-Protection
    response->addHeader("X-XSS-Protection", "1; mode=block");

    // Referrer-Policy
    response->addHeader("Referrer-Policy", "strict-origin-when-cross-origin");

    // Permissions-Policy
    response->addHeader("Permissions-Policy",
        "geolocation=(), "
        "microphone=(), "
        "camera=()");
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

} // namespace Security
