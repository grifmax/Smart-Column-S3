/**
 * Smart-Column S3 - Telegram bot
 *
 * FastBot2 backend for notifications and simple command handling.
 */

#include "telegram.h"

#include <FastBot2.h>
#include <WiFi.h>

#include <cstdarg>
#include <cstdio>

#include "control/fsm.h"

static FastBot2* tgBot = nullptr;
static String tgChatId;
static bool tgEnabled = false;
static bool tgReady = false;

// Параметры для отложенной инициализации (чтобы избежать Race Condition с веб-сервером)
static bool tgNeedInit = false;
static String pendingToken;
static String pendingChatId;

namespace {

static bool isConfigured() {
  return tgEnabled && tgBot && tgReady && tgChatId.length();
}

static bool isAuthorizedMessage(fb::MessageRead msg) {
  if (!tgChatId.length()) return false;
  return msg.chat().id().toString() == tgChatId;
}

static bool sendMessageToChat(const fb::ID& chatId, const char* message) {
  if (!tgBot || !message || !message[0]) {
    return false;
  }

  fb::Message out(message, chatId);
  // Используем синхронную отправку (true), так как объект 'out' живет только в этой функции.
  // Это предотвращает краш из-за обращения к памяти после выхода из функции.
  fb::Result res = tgBot->sendMessage(out, true);
  if (res.isError()) {
    LOG_W("Telegram: send failed (%s)", res.getError().toString().c_str());
    return false;
  }
  return true;
}

static bool sendMessageToConfiguredChat(const char* message) {
  if (!tgChatId.length()) {
    return false;
  }
  return sendMessageToChat(fb::ID(tgChatId), message);
}

static uint32_t lastMessageMs = 0;
static uint8_t messageBurstCount = 0;

static void handleUpdate(fb::Update& u) {
  if (!u.isMessage()) {
    return;
  }

  // Basic rate limiting
  uint32_t now = millis();
  if (now - lastMessageMs < 500) {
    messageBurstCount++;
  } else {
    messageBurstCount = 0;
  }
  lastMessageMs = now;

  if (messageBurstCount > 5) {
    LOG_W("Telegram: rate limit exceeded");
    return;
  }

  fb::MessageRead msg = u.message();
  if (!isAuthorizedMessage(msg)) {
    LOG_W("Telegram: unauthorized chat %s", msg.chat().id().toString().c_str());
    return;
  }

  String text = msg.text().toString();
  text.trim();
  text.toLowerCase();
  if (!text.length()) {
    return;
  }

  LOG_I("Telegram: command from %s: %s", tgChatId.c_str(), text.c_str());

  fb::ID replyChat(msg.chat().id());
  if (text == "/status") {
    char status[256];
    snprintf(status, sizeof(status),
             "Mode: %s\nPhase: %s\nPaused: %s\nUptime: %lus\nPower: %.0fW\nTemp: %.1fC",
             FSM::getModeName(g_state.mode),
             FSM::getPhaseName(g_state.rectPhase),
             g_state.paused ? "yes" : "no",
             (unsigned long)g_state.uptime,
             g_state.power.power,
             g_state.temps.cube);
    sendMessageToChat(replyChat, status);
  } else if (text == "/health") {
    char msg[384];
    const char* resetReason = "Unknown";
    switch(g_state.health.lastRebootReason) {
        case 1: resetReason = "Power On"; break;
        case 3: resetReason = "Software Watchdog"; break;
        case 4: resetReason = "Hardware Watchdog"; break;
        case 5: resetReason = "Deep Sleep"; break;
        case 6: resetReason = "Software Reset"; break;
        case 7: resetReason = "Panic"; break;
        default: resetReason = "Other"; break;
    }
    
    snprintf(msg, sizeof(msg),
             "System Health: %u%%\n"
             "Reset Reason: %s\n"
             "Free Heap: %u KB\n"
             "CPU Temp: %.1f C\n"
             "WiFi RSSI: %d dBm\n"
             "PZEM: %s\n"
             "ADS1115: %s\n"
             "Temps: %u/%u OK",
             g_state.health.overallHealth,
             resetReason,
             g_state.health.freeHeap / 1024,
             g_state.health.cpuTemp,
             g_state.health.wifiRSSI,
             g_state.health.pzemOk ? "OK" : "FAIL",
             g_state.health.ads1115Ok ? "OK" : "FAIL",
             g_state.health.tempSensorsOk, g_state.health.tempSensorsTotal);
    sendMessageToChat(replyChat, msg);
  } else if (text == "/stop") {
    FSM::stopMode(g_state);
    sendMessageToChat(replyChat, "Process stopped");
  } else if (text == "/help" || text == "/start") {
    sendMessageToChat(replyChat, "Commands: /status, /health, /stop, /help");
  } else {
    sendMessageToChat(replyChat, "Unknown command. Use /help");
  }
}

// Реальная инициализация бота (вызывать ТОЛЬКО из основного цикла)
void performInit(const char* token, const char* chat) {
  LOG_I("Telegram(FastBot2): Real init starting...");
  
  if (tgBot) {
    tgBot->end();
    delete tgBot;
    tgBot = nullptr;
  }

  tgBot = new FastBot2();
  tgBot->setToken(token);
  tgBot->setTimeout(5000);
  tgBot->setPollMode(fb::Poll::Long, 15000);
  tgBot->skipUpdates();
  tgBot->attachUpdate(handleUpdate);
  tgBot->setOnline(WiFi.status() == WL_CONNECTED);
  tgBot->begin();

  tgChatId = chat;
  tgChatId.trim();
  tgReady = true;
  tgNeedInit = false;
  LOG_I("Telegram(FastBot2): Init complete");
}

}  // namespace

namespace TelegramBot {

void init(const char* token, const char* chat) {
  if (!token || !token[0] || !chat || !chat[0]) {
    tgEnabled = false;
    return;
  }

  // Вместо немедленной инициализации ставим флаг
  pendingToken = token;
  pendingChatId = chat;
  tgNeedInit = true;
  tgEnabled = true;
  LOG_I("Telegram: Scheduled for init...");
}

void update() {
  if (!tgEnabled) return;

  // Безопасная инициализация в основном цикле
  if (tgNeedInit) {
    performInit(pendingToken.c_str(), pendingChatId.c_str());
  }

  if (!tgBot || !tgReady) return;

  const bool online = (WiFi.status() == WL_CONNECTED);
  tgBot->setOnline(online);
  if (!online) return;

  tgBot->tick();
}

bool sendMessage(const char* message) {
  if (!isConfigured() || !message) return false;
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("Telegram: send skipped, WiFi STA not connected");
    return false;
  }
  return sendMessageToConfiguredChat(message);
}

bool sendFormatted(const char* format, ...) {
  if (!format) return false;
  char buffer[384];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return sendMessage(buffer);
}

void notifyPhaseChange(RectPhase phase, const RunStats& stats) {
  const char* phases[] = {"Idle", "Heating", "Stabilization", "Heads", "Purge",
                          "Body", "Tails", "Finish", "Error"};
  const uint8_t idx = static_cast<uint8_t>(phase);
  const char* phaseName = (idx < (sizeof(phases) / sizeof(phases[0]))) ? phases[idx] : "Unknown";
  const float totalVolume = stats.headsVolume + stats.bodyVolume + stats.tailsVolume;
  sendFormatted("Phase changed: %s\nVolume: %.0f ml", phaseName, totalVolume);
}

void notifyAlarm(const Alarm& alarm) {
  sendFormatted("ALARM: %s", alarm.message);
}

void notifyFinish(const RunStats& stats) {
  const float totalVolume = stats.headsVolume + stats.bodyVolume + stats.tailsVolume;
  sendFormatted("Process finished\nHeads: %.0f ml\nBody: %.0f ml\nTails: %.0f ml\nTotal: %.0f ml",
                stats.headsVolume, stats.bodyVolume, stats.tailsVolume, totalVolume);
}

void notifyHealthAlert(const SystemHealth& health) {
  char msg[512];
  int len = 0;
  len += snprintf(msg + len, sizeof(msg) - len, "System health alert\n\nOverall: %u%%\n", health.overallHealth);
  if (!health.pzemOk) len += snprintf(msg + len, sizeof(msg) - len, "- PZEM power meter\n");
  if (!health.ads1115Ok) len += snprintf(msg + len, sizeof(msg) - len, "- ADS1115 ADC\n");
  if (!health.bmp280Ok) len += snprintf(msg + len, sizeof(msg) - len, "- BMP280 sensor\n");
  if (health.tempSensorsOk < health.tempSensorsTotal) {
    len += snprintf(msg + len, sizeof(msg) - len, "- Temp sensors: %u/%u OK\n",
                    health.tempSensorsOk, health.tempSensorsTotal);
  }
  sendMessage(msg);
}

bool sendScreenshot() { return false; }

void setEnabled(bool enabled) {
  tgEnabled = enabled;
  if (!enabled && tgBot) {
    tgBot->end();
    tgReady = false;
  }
}

bool isEnabled() { return tgEnabled && tgBot != nullptr && tgReady; }

void setSettings(const TelegramSettings& settings) {
  if (settings.enabled) {
    init(settings.token, settings.chatId);
  } else {
    setEnabled(false);
  }
}

}  // namespace TelegramBot
