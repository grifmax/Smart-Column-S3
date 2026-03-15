/**
 * Smart-Column S3 - Telegram bot
 *
 * Minimal FastBot2-based notifications and command handling.
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
static bool tgPolling = false;
static bool tgNeedInit = false;
static String pendingToken;
static String pendingChatId;

static uint32_t tgLastInitMs = 0;
static uint32_t tgLastTickMs = 0;
static uint32_t tgLastUpdateMs = 0;
static uint32_t tgTickCount = 0;
static uint32_t tgUpdateCount = 0;
static uint32_t tgMessageCount = 0;
static uint32_t tgQueryCount = 0;
static uint32_t tgSendOkCount = 0;
static uint32_t tgSendErrorCount = 0;
static uint32_t tgLockTimeoutCount = 0;
static String tgLastError;

static uint32_t tgPollIntervalMs = 5000;
static constexpr uint32_t kTelegramMinPollMs = 5000;
static constexpr uint32_t kTelegramMaxPollMs = 60000;

namespace {

static bool isConfigured() {
  return tgEnabled && tgBot && tgReady && tgChatId.length();
}

static void setLastError(const String& error) {
  tgLastError = error;
  tgLastError.trim();
}

static void clearLastError() {
  tgLastError = "";
}

static bool isAuthorizedChat(const fb::ID& id) {
  if (!tgChatId.length()) return false;
  String idStr;
  su::Text(id).toString(idStr);
  return idStr == tgChatId;
}

static bool sendMessageWithKeyb(const fb::ID& chatId, const char* message,
                                fb::InlineMenu* menu = nullptr) {
  if (!tgBot || !message || !message[0]) return false;

  fb::Message out(message, chatId);
  if (menu) out.setInlineMenu(*menu);

  fb::Result res = tgBot->sendMessage(out, true);
  if (res.isError()) {
    tgSendErrorCount++;
    setLastError(res.getError().toString());
    LOG_W("Telegram: send failed (%s)", res.getError().toString().c_str());
    return false;
  }

  tgSendOkCount++;
  clearLastError();
  return true;
}

static bool sendMessageToChat(const fb::ID& chatId, const char* message) {
  return sendMessageWithKeyb(chatId, message, nullptr);
}

static bool sendMessageToConfiguredChat(const char* message) {
  if (!tgChatId.length()) return false;
  return sendMessageToChat(fb::ID(tgChatId), message);
}

static void sendStatus(const fb::ID& chatId) {
  char status[256];
  const bool active = (g_state.mode != Mode::IDLE);
  snprintf(status, sizeof(status),
           "Status\n"
           "Mode: %s\n"
           "Phase: %s\n"
           "State: %s\n"
           "Power: %.0f W\n"
           "Cube: %.1f C\n"
           "Uptime: %lu s",
           FSM::getModeName(g_state.mode), FSM::getPhaseName(g_state.rectPhase),
           g_state.paused ? "PAUSED" : (active ? "RUNNING" : "IDLE"), g_state.power.power,
           g_state.temps.cube, (unsigned long)g_state.uptime);

  fb::InlineMenu menu;
  if (active) {
    menu.addButton("PAUSE/RESUME", "tg_pause");
    menu.addButton("STOP", "tg_stop");
    menu.newRow();
  }
  menu.addButton("HEALTH", "tg_health");
  menu.addButton("HELP", "tg_help");
  sendMessageWithKeyb(chatId, status, &menu);
}

static void sendHealth(const fb::ID& chatId) {
  char msg[384];
  const char* resetReason = "Other";
  switch (g_state.health.lastRebootReason) {
    case 1: resetReason = "Power On"; break;
    case 3: resetReason = "SW Watchdog"; break;
    case 4: resetReason = "HW Watchdog"; break;
    case 5: resetReason = "Deep Sleep"; break;
    case 6: resetReason = "SW Reset"; break;
    case 7: resetReason = "Panic"; break;
  }

  snprintf(msg, sizeof(msg),
           "Health\n"
           "Overall: %u%%\n"
           "Reset: %s\n"
           "Heap: %u KB\n"
           "CPU: %.1f C\n"
           "WiFi: %d dBm\n"
           "PZEM: %s\n"
           "ADS: %s\n"
           "Temps: %u/%u OK",
           g_state.health.overallHealth, resetReason, g_state.health.freeHeap / 1024,
           g_state.health.cpuTemp, g_state.health.wifiRSSI,
           g_state.health.pzemOk ? "OK" : "FAIL",
           g_state.health.ads1115Ok ? "OK" : "FAIL", g_state.health.tempSensorsOk,
           g_state.health.tempSensorsTotal);

  fb::InlineMenu menu;
  menu.addButton("REFRESH", "tg_health");
  menu.addButton("BACK", "tg_status");
  sendMessageWithKeyb(chatId, msg, &menu);
}

static void sendHelp(const fb::ID& chatId) {
  sendMessageToChat(chatId, "Commands: /status, /health, /stop, /help");
}

static void handleUpdate(fb::Update& u) {
  tgUpdateCount++;
  tgLastUpdateMs = millis();
  clearLastError();

  if (u.isMessage()) {
    tgMessageCount++;
    fb::MessageRead msg = u.message();
    if (!isAuthorizedChat(msg.chat().id())) {
      return;
    }

    String text = msg.text().toString();
    text.trim();
    text.toLowerCase();
    if (!text.length()) {
      return;
    }

    fb::ID chatId(msg.chat().id());
    if (text == "/status" || text == "/start") {
      sendStatus(chatId);
    } else if (text == "/health") {
      sendHealth(chatId);
    } else if (text == "/stop") {
      FSM::stopMode(g_state);
      sendMessageToChat(chatId, "Process stopped");
    } else if (text == "/help") {
      sendHelp(chatId);
    } else {
      sendHelp(chatId);
    }
    return;
  }

  if (u.isQuery()) {
    tgQueryCount++;
    fb::QueryRead callback = u.query();
    if (!isAuthorizedChat(callback.message().chat().id())) {
      return;
    }

    String data;
    callback.data().toString(data);
    fb::ID chatId(callback.message().chat().id());

    if (data == "tg_status") {
      sendStatus(chatId);
    } else if (data == "tg_health") {
      sendHealth(chatId);
    } else if (data == "tg_pause") {
      if (g_state.paused) {
        FSM::resume(g_state);
      } else {
        FSM::pause(g_state);
      }
      sendStatus(chatId);
    } else if (data == "tg_stop") {
      FSM::stopMode(g_state);
      sendMessageToChat(chatId, "Process stopped");
    } else if (data == "tg_help") {
      sendHelp(chatId);
    }

    tgBot->answerCallbackQuery(callback.id());
  }
}

static void performInit(const char* token, const char* chat) {
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
  tgPollIntervalMs = kTelegramMinPollMs;
  tgLastInitMs = millis();
  tgLastTickMs = 0;
  clearLastError();
  LOG_I("Telegram: Init complete");
}

}  // namespace

namespace TelegramBot {

void init(const char* token, const char* chat) {
  if (!token || !token[0] || !chat || !chat[0]) {
    tgEnabled = false;
    tgReady = false;
    return;
  }

  pendingToken = token;
  pendingChatId = chat;
  tgNeedInit = true;
  tgEnabled = true;
}

void update() {
  if (!tgEnabled) return;

  const uint32_t now = millis();
  if (tgNeedInit) {
    performInit(pendingToken.c_str(), pendingChatId.c_str());
    tgLastTickMs = now;
    return;
  }

  if (!tgBot || !tgReady) return;

  const bool online = (WiFi.status() == WL_CONNECTED);
  tgBot->setOnline(online);
  if (!online) {
    tgPolling = false;
    if (tgPollIntervalMs < kTelegramMaxPollMs) {
      tgPollIntervalMs *= 2;
      if (tgPollIntervalMs > kTelegramMaxPollMs) {
        tgPollIntervalMs = kTelegramMaxPollMs;
      }
    }
    return;
  }

  if (now - tgLastTickMs < tgPollIntervalMs) {
    return;
  }

  tgLastTickMs = now;
  tgTickCount++;
  tgPolling = true;
  tgBot->tick();
  tgPolling = false;
  if (tgPollIntervalMs > kTelegramMinPollMs) {
    tgPollIntervalMs = kTelegramMinPollMs;
  }
}

bool sendMessage(const char* message) {
  if (!isConfigured() || !message) return false;
  if (WiFi.status() != WL_CONNECTED) {
    setLastError("telegram wifi offline");
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
  len += snprintf(msg + len, sizeof(msg) - len, "System health alert\n\nOverall: %u%%\n",
                  health.overallHealth);
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

DebugStatus getDebugStatus() {
  DebugStatus status;
  status.enabled = tgEnabled;
  status.ready = tgReady;
  status.configured = isConfigured();
  status.online = WiFi.status() == WL_CONNECTED;
  status.polling = tgPolling;
  status.needInit = tgNeedInit;
  status.taskRunning = false;
  status.lastInitMs = tgLastInitMs;
  status.lastTickMs = tgLastTickMs;
  status.lastUpdateMs = tgLastUpdateMs;
  status.tickCount = tgTickCount;
  status.updateCount = tgUpdateCount;
  status.messageCount = tgMessageCount;
  status.queryCount = tgQueryCount;
  status.sendOkCount = tgSendOkCount;
  status.sendErrorCount = tgSendErrorCount;
  status.lockTimeoutCount = tgLockTimeoutCount;
  status.lastError = tgLastError;
  return status;
}

}  // namespace TelegramBot
