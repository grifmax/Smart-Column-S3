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
  fb::Result res = tgBot->sendMessage(out, true);
  if (res.type() != fb::Result::Type::OK || res.isError()) {
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

static void handleUpdate(fb::Update& u) {
  if (!u.isMessage()) {
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
    char status[128];
    snprintf(status, sizeof(status),
             "Mode: %s\nPaused: %s\nUptime: %lus",
             FSM::getModeName(g_state.mode),
             g_state.paused ? "yes" : "no",
             (unsigned long)g_state.uptime);
    sendMessageToChat(replyChat, status);
  } else if (text == "/stop") {
    FSM::stopMode(g_state);
    sendMessageToChat(replyChat, "Process stopped");
  } else if (text == "/help" || text == "/start") {
    sendMessageToChat(replyChat, "Commands: /status, /stop, /help");
  } else {
    sendMessageToChat(replyChat, "Unknown command. Use /help");
  }
}

}  // namespace

namespace TelegramBot {

void init(const char* token, const char* chat) {
  LOG_I("Telegram(FastBot2): Initializing...");

  tgEnabled = (token && token[0] && chat && chat[0]);
  tgReady = false;
  tgChatId = "";

  if (!tgEnabled) {
    LOG_W("Telegram(FastBot2): Disabled (empty token/chat)");
    return;
  }

  tgChatId = chat;
  tgChatId.trim();
  if (!tgChatId.length()) {
    LOG_E("Telegram(FastBot2): Invalid chat id");
    tgEnabled = false;
    return;
  }

  if (tgBot) {
    delete tgBot;
    tgBot = nullptr;
  }

  tgBot = new FastBot2();
  tgBot->setToken(token);
  tgBot->setTimeout(8000);
  tgBot->setPollMode(fb::Poll::Long, 60000);
  tgBot->skipUpdates();
  tgBot->attachUpdate(handleUpdate);
  tgBot->setOnline(WiFi.status() == WL_CONNECTED);
  tgBot->begin();

  tgReady = true;
  LOG_I("Telegram(FastBot2): Ready");
}

void update() {
  if (!tgEnabled || !tgBot || !tgReady) {
    return;
  }

  const bool online = (WiFi.status() == WL_CONNECTED);
  tgBot->setOnline(online);
  if (!online) {
    return;
  }

  tgBot->tick();
}

bool sendMessage(const char* message) {
  if (!isConfigured() || !message) {
    return false;
  }
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

  if (health.pzemSpikeCount > 0) {
    len += snprintf(msg + len, sizeof(msg) - len, "PZEM spikes: %u\n", health.pzemSpikeCount);
  }
  if (health.tempReadErrors > 0) {
    len += snprintf(msg + len, sizeof(msg) - len, "Temp errors: %u\n", health.tempReadErrors);
  }
  if (health.wifiConnected && health.wifiRSSI < -80) {
    len += snprintf(msg + len, sizeof(msg) - len, "Weak WiFi: %d dBm\n", health.wifiRSSI);
  }

  sendMessage(msg);
}

bool sendScreenshot() {
  return false;
}

void setEnabled(bool enabled) {
  tgEnabled = enabled;
  if (!tgBot) return;

  if (enabled) {
    tgBot->begin();
  } else {
    tgBot->end();
  }
}

bool isEnabled() {
  return tgEnabled && tgBot != nullptr && tgReady;
}

void setSettings(const TelegramSettings& settings) {
  tgEnabled = settings.enabled;
  if (!tgEnabled) {
    setEnabled(false);
    return;
  }
  init(settings.token, settings.chatId);
}

}  // namespace TelegramBot
