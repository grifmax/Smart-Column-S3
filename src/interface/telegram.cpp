/**
 * Smart-Column S3 - Telegram bot
 *
 * Minimal FastBot2-based notifications and command handling.
 */

#include "telegram.h"

#include <FastBot2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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
static int32_t tgNextUpdateId = 0;

static constexpr uint8_t kTelegramActionQueueSize = 8;
static constexpr uint8_t kTelegramMessageQueueSize = 8;
static constexpr uint32_t kTelegramTaskDelayMs = 50;
static constexpr uint32_t kTelegramTaskIdleDelayMs = 250;

enum class PendingTelegramAction : uint8_t {
  Status,
  Health,
  TogglePause,
  Stop,
  Help,
};

struct TelegramActionItem {
  PendingTelegramAction action = PendingTelegramAction::Help;
  String chatId;
};

static TelegramActionItem tgActionQueue[kTelegramActionQueueSize];
static uint8_t tgActionQueueHead = 0;
static uint8_t tgActionQueueTail = 0;
static uint8_t tgActionQueueCount = 0;

static String tgMessageQueue[kTelegramMessageQueueSize];
static uint8_t tgMessageQueueHead = 0;
static uint8_t tgMessageQueueTail = 0;
static uint8_t tgMessageQueueCount = 0;
static SemaphoreHandle_t tgMessageQueueMutex = nullptr;

static TaskHandle_t tgTaskHandle = nullptr;
static bool tgTaskRunning = false;

namespace {

static void performInit(const char* token, const char* chat);
static void telegramTask(void* param);
static void ensureTaskStarted();
static bool sendRawMessage(const fb::ID& chatId, const char* message, const char* menuKind = nullptr);

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

static bool enqueueAction(PendingTelegramAction action, const fb::ID& chatId) {
  if (tgActionQueueCount >= kTelegramActionQueueSize) {
    tgLockTimeoutCount++;
    setLastError("telegram action queue full");
    LOG_W("Telegram: action queue overflow");
    return false;
  }

  TelegramActionItem& item = tgActionQueue[tgActionQueueTail];
  item.action = action;
  su::Text(chatId).toString(item.chatId);
  tgActionQueueTail = (tgActionQueueTail + 1) % kTelegramActionQueueSize;
  tgActionQueueCount++;
  return true;
}

static bool dequeueAction(TelegramActionItem& item) {
  if (!tgActionQueueCount) return false;

  item = tgActionQueue[tgActionQueueHead];
  tgActionQueueHead = (tgActionQueueHead + 1) % kTelegramActionQueueSize;
  tgActionQueueCount--;
  return true;
}

static void clearActionQueue() {
  tgActionQueueHead = 0;
  tgActionQueueTail = 0;
  tgActionQueueCount = 0;
}

static bool enqueueOutboundMessage(const char* message) {
  if (!message || !message[0]) return false;
  if (!tgMessageQueueMutex) {
    setLastError("telegram queue mutex missing");
    return false;
  }

  bool ok = false;
  if (xSemaphoreTake(tgMessageQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (tgMessageQueueCount < kTelegramMessageQueueSize) {
      tgMessageQueue[tgMessageQueueTail] = message;
      tgMessageQueueTail = (tgMessageQueueTail + 1) % kTelegramMessageQueueSize;
      tgMessageQueueCount++;
      ok = true;
    }
    xSemaphoreGive(tgMessageQueueMutex);
  }

  if (!ok) {
    tgLockTimeoutCount++;
    setLastError("telegram message queue full");
    LOG_W("Telegram: message queue overflow");
  }
  return ok;
}

static bool dequeueOutboundMessage(String& message) {
  if (!tgMessageQueueMutex) return false;

  bool ok = false;
  if (xSemaphoreTake(tgMessageQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (tgMessageQueueCount) {
      message = tgMessageQueue[tgMessageQueueHead];
      tgMessageQueue[tgMessageQueueHead] = "";
      tgMessageQueueHead = (tgMessageQueueHead + 1) % kTelegramMessageQueueSize;
      tgMessageQueueCount--;
      ok = true;
    }
    xSemaphoreGive(tgMessageQueueMutex);
  }
  return ok;
}

static void clearMessageQueue() {
  if (!tgMessageQueueMutex) return;

  if (xSemaphoreTake(tgMessageQueueMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    for (uint8_t i = 0; i < kTelegramMessageQueueSize; ++i) {
      tgMessageQueue[i] = "";
    }
    tgMessageQueueHead = 0;
    tgMessageQueueTail = 0;
    tgMessageQueueCount = 0;
    xSemaphoreGive(tgMessageQueueMutex);
  }
}

static void resetBotInstance() {
  if (tgBot) {
    tgBot->end();
    delete tgBot;
    tgBot = nullptr;
  }
  tgReady = false;
  tgPolling = false;
}

static bool sendMessageWithKeyb(const fb::ID& chatId, const char* message,
                                fb::InlineMenu* menu = nullptr) {
  const char* menuKind = menu ? "status_idle" : nullptr;
  return sendRawMessage(chatId, message, menuKind);
}

static bool sendMessageToChat(const fb::ID& chatId, const char* message) {
  return sendRawMessage(chatId, message);
}

static bool sendMessageToConfiguredChat(const char* message) {
  if (!tgChatId.length()) return false;
  return sendMessageToChat(fb::ID(tgChatId), message);
}

static fb::ID jsonIdToFbId(JsonVariantConst value) {
  if (value.is<const char*>()) {
    return fb::ID(String(value.as<const char*>()));
  }

  char buffer[24];
  const long long numericId = value | 0LL;
  snprintf(buffer, sizeof(buffer), "%lld", numericId);
  return fb::ID(String(buffer));
}

static bool readTelegramHttpBody(WiFiClientSecure& client, String& body) {
  String response;
  response.reserve(8192);

  uint32_t lastDataMs = millis();
  while (millis() - lastDataMs < 5000) {
    while (client.available()) {
      response += static_cast<char>(client.read());
      lastDataMs = millis();
    }

    if (!client.connected() && !client.available()) {
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  const int headerEnd = response.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    setLastError("telegram http header parse failed");
    return false;
  }

  body = response.substring(headerEnd + 4);
  return true;
}

static bool telegramApiPostJson(const char* method, const String& payload, String* responseBody = nullptr) {
  if (!method || !method[0] || !pendingToken.length()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  if (!client.connect("api.telegram.org", 443)) {
    tgSendErrorCount++;
    setLastError("telegram post connect failed");
    return false;
  }

  String path = "/bot";
  path += pendingToken;
  path += "/";
  path += method;

  client.print(String("POST ") + path + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "User-Agent: SmartColumn/2.0.57\r\n" +
               "Connection: close\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " + String(payload.length()) + "\r\n\r\n" +
               payload);

  String body;
  const bool bodyOk = readTelegramHttpBody(client, body);
  client.stop();
  if (!bodyOk) {
    tgSendErrorCount++;
    return false;
  }

  if (responseBody) {
    *responseBody = body;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    tgSendErrorCount++;
    setLastError(String("telegram post json: ") + error.c_str());
    return false;
  }

  if (!(doc["ok"] | false)) {
    tgSendErrorCount++;
    const char* description = doc["description"] | "telegram api error";
    setLastError(description);
    return false;
  }

  tgSendOkCount++;
  clearLastError();
  return true;
}

static void addInlineButtonRow(JsonArray row, const char* text, const char* callbackData) {
  JsonObject button = row.add<JsonObject>();
  button["text"] = text;
  button["callback_data"] = callbackData;
}

static bool sendRawMessage(const fb::ID& chatId, const char* message, const char* menuKind) {
  if (!message || !message[0]) return false;

  String chatIdStr;
  su::Text(chatId).toString(chatIdStr);
  if (!chatIdStr.length()) return false;

  JsonDocument doc;
  doc["chat_id"] = chatIdStr;
  doc["text"] = message;

  if (menuKind) {
    JsonArray keyboard = doc["reply_markup"]["inline_keyboard"].to<JsonArray>();
    if (strcmp(menuKind, "status_active") == 0) {
      JsonArray row1 = keyboard.add<JsonArray>();
      addInlineButtonRow(row1, "PAUSE/RESUME", "tg_pause");
      addInlineButtonRow(row1, "STOP", "tg_stop");
      JsonArray row2 = keyboard.add<JsonArray>();
      addInlineButtonRow(row2, "HEALTH", "tg_health");
      addInlineButtonRow(row2, "HELP", "tg_help");
    } else if (strcmp(menuKind, "status_idle") == 0) {
      JsonArray row = keyboard.add<JsonArray>();
      addInlineButtonRow(row, "HEALTH", "tg_health");
      addInlineButtonRow(row, "HELP", "tg_help");
    } else if (strcmp(menuKind, "health") == 0) {
      JsonArray row = keyboard.add<JsonArray>();
      addInlineButtonRow(row, "REFRESH", "tg_health");
      addInlineButtonRow(row, "BACK", "tg_status");
    }
  }

  String payload;
  serializeJson(doc, payload);
  return telegramApiPostJson("sendMessage", payload);
}

static void enqueueTextCommand(const fb::ID& chatId, String text) {
  text.trim();
  text.toLowerCase();
  if (!text.length()) return;

  if (text == "/status" || text == "/start") {
    enqueueAction(PendingTelegramAction::Status, chatId);
  } else if (text == "/health") {
    enqueueAction(PendingTelegramAction::Health, chatId);
  } else if (text == "/stop") {
    enqueueAction(PendingTelegramAction::Stop, chatId);
  } else if (text == "/help") {
    enqueueAction(PendingTelegramAction::Help, chatId);
  } else {
    enqueueAction(PendingTelegramAction::Help, chatId);
  }
}

static void enqueueCallbackCommand(const fb::ID& chatId, String data) {
  if (data == "tg_status") {
    enqueueAction(PendingTelegramAction::Status, chatId);
  } else if (data == "tg_health") {
    enqueueAction(PendingTelegramAction::Health, chatId);
  } else if (data == "tg_pause") {
    enqueueAction(PendingTelegramAction::TogglePause, chatId);
  } else if (data == "tg_stop") {
    enqueueAction(PendingTelegramAction::Stop, chatId);
  } else if (data == "tg_help") {
    enqueueAction(PendingTelegramAction::Help, chatId);
  }
}

static void answerCallbackById(const String& callbackId) {
  if (!callbackId.length()) return;

  JsonDocument doc;
  doc["callback_query_id"] = callbackId;
  String payload;
  serializeJson(doc, payload);
  telegramApiPostJson("answerCallbackQuery", payload);
}

static bool pollTelegramUpdates() {
  if (!tgReady || !pendingToken.length()) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  if (!client.connect("api.telegram.org", 443)) {
    setLastError("telegram connect failed");
    return false;
  }

  String path = "/bot";
  path += pendingToken;
  path += "/getUpdates?limit=8&timeout=1";
  if (tgNextUpdateId > 0) {
    path += "&offset=" + String(tgNextUpdateId);
  }

  tgPolling = true;
  client.print(String("GET ") + path + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "User-Agent: SmartColumn/2.0.57\r\n" +
               "Connection: close\r\n\r\n");

  String body;
  const bool bodyOk = readTelegramHttpBody(client, body);
  client.stop();
  tgPolling = false;
  if (!bodyOk || !body.length()) {
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    setLastError(String("telegram json: ") + error.c_str());
    return false;
  }

  JsonArray results = doc["result"].as<JsonArray>();
  if (results.isNull()) {
    setLastError("telegram result parse failed");
    return false;
  }

  for (JsonObject update : results) {
    const int32_t updateId = update["update_id"] | 0;
    if (updateId >= tgNextUpdateId) {
      tgNextUpdateId = updateId + 1;
    }

    JsonObject message = update["message"].as<JsonObject>();
    if (!message.isNull()) {
      fb::ID chatId = jsonIdToFbId(message["chat"]["id"]);
      if (isAuthorizedChat(chatId)) {
        tgUpdateCount++;
        tgMessageCount++;
        tgLastUpdateMs = millis();
        enqueueTextCommand(chatId, String(message["text"] | ""));
      }
      continue;
    }

    JsonObject callback = update["callback_query"].as<JsonObject>();
    if (!callback.isNull()) {
      fb::ID chatId = jsonIdToFbId(callback["message"]["chat"]["id"]);
      if (isAuthorizedChat(chatId)) {
        tgUpdateCount++;
        tgQueryCount++;
        tgLastUpdateMs = millis();
        enqueueCallbackCommand(chatId, String(callback["data"] | ""));
        answerCallbackById(String(callback["id"] | ""));
      }
    }
  }

  clearLastError();
  return true;
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

  sendRawMessage(chatId, status, active ? "status_active" : "status_idle");
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

  sendRawMessage(chatId, msg, "health");
}

static void sendHelp(const fb::ID& chatId) {
  sendMessageToChat(chatId, "Commands: /status, /health, /stop, /help");
}

static void processPendingActions() {
  TelegramActionItem item;
  while (dequeueAction(item)) {
    fb::ID chatId(item.chatId);
    switch (item.action) {
      case PendingTelegramAction::Status:
        sendStatus(chatId);
        break;

      case PendingTelegramAction::Health:
        sendHealth(chatId);
        break;

      case PendingTelegramAction::TogglePause:
        if (g_state.paused) {
          FSM::resume(g_state);
        } else {
          FSM::pause(g_state);
        }
        sendStatus(chatId);
        break;

      case PendingTelegramAction::Stop:
        FSM::stopMode(g_state);
        sendMessageToChat(chatId, "Process stopped");
        break;

      case PendingTelegramAction::Help:
        sendHelp(chatId);
        break;
    }
  }
}

static void processOutboundMessages() {
  if (!tgChatId.length()) return;

  String message;
  while (dequeueOutboundMessage(message)) {
    sendMessageToConfiguredChat(message.c_str());
  }
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
      enqueueAction(PendingTelegramAction::Status, chatId);
    } else if (text == "/health") {
      enqueueAction(PendingTelegramAction::Health, chatId);
    } else if (text == "/stop") {
      enqueueAction(PendingTelegramAction::Stop, chatId);
    } else if (text == "/help") {
      enqueueAction(PendingTelegramAction::Help, chatId);
    } else {
      enqueueAction(PendingTelegramAction::Help, chatId);
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
      enqueueAction(PendingTelegramAction::Status, chatId);
    } else if (data == "tg_health") {
      enqueueAction(PendingTelegramAction::Health, chatId);
    } else if (data == "tg_pause") {
      enqueueAction(PendingTelegramAction::TogglePause, chatId);
    } else if (data == "tg_stop") {
      enqueueAction(PendingTelegramAction::Stop, chatId);
    } else if (data == "tg_help") {
      enqueueAction(PendingTelegramAction::Help, chatId);
    }
  }
}

static void telegramTask(void* /*param*/) {
  tgTaskRunning = true;

  for (;;) {
    if (!tgEnabled) {
      resetBotInstance();
      clearActionQueue();
      clearMessageQueue();
      vTaskDelay(pdMS_TO_TICKS(kTelegramTaskIdleDelayMs));
      continue;
    }

    if (tgNeedInit) {
      performInit(pendingToken.c_str(), pendingChatId.c_str());
      vTaskDelay(pdMS_TO_TICKS(kTelegramTaskDelayMs));
      continue;
    }

    if (!tgBot || !tgReady) {
      vTaskDelay(pdMS_TO_TICKS(kTelegramTaskIdleDelayMs));
      continue;
    }

    const bool online = (WiFi.status() == WL_CONNECTED);
    tgBot->setOnline(online);
    if (!online) {
      tgPolling = false;
      vTaskDelay(pdMS_TO_TICKS(kTelegramTaskIdleDelayMs));
      continue;
    }

    tgLastTickMs = millis();
    tgTickCount++;
    pollTelegramUpdates();
    processPendingActions();
    processOutboundMessages();
    vTaskDelay(pdMS_TO_TICKS(kTelegramTaskDelayMs));
  }
}

static void ensureTaskStarted() {
  if (tgTaskHandle) return;
  if (!tgMessageQueueMutex) {
    tgMessageQueueMutex = xSemaphoreCreateMutex();
    if (!tgMessageQueueMutex) {
      setLastError("telegram queue mutex create failed");
      LOG_E("Telegram: failed to create queue mutex");
      return;
    }
  }
  BaseType_t res = xTaskCreatePinnedToCore(telegramTask, "telegram_bot", 8192, nullptr, 1,
                                           &tgTaskHandle, 0);
  if (res != pdPASS) {
    tgTaskHandle = nullptr;
    tgTaskRunning = false;
    setLastError("telegram task create failed");
    LOG_E("Telegram: failed to create task");
  }
}

static void performInit(const char* token, const char* chat) {
  resetBotInstance();

  tgBot = new FastBot2();
  tgBot->setToken(token);
  tgBot->setTimeout(5000);
  tgBot->setPollMode(fb::Poll::Async, 1000);
  tgBot->skipUpdates();
  tgBot->attachUpdate(handleUpdate);
  tgBot->setOnline(WiFi.status() == WL_CONNECTED);
  tgBot->begin();

  tgChatId = chat;
  tgChatId.trim();
  tgReady = true;
  tgNeedInit = false;
  tgNextUpdateId = 0;
  clearActionQueue();
  clearMessageQueue();
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
  ensureTaskStarted();
}

void update() {
  if (!tgEnabled) return;
  ensureTaskStarted();
}

bool sendMessage(const char* message) {
  if (!tgEnabled || !message || !message[0]) return false;
  if (WiFi.status() != WL_CONNECTED) {
    setLastError("telegram wifi offline");
    return false;
  }
  if (!tgChatId.length() && !pendingChatId.length()) {
    setLastError("telegram chat not configured");
    return false;
  }
  return enqueueOutboundMessage(message);
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
  if (!enabled) {
    tgNeedInit = false;
    tgReady = false;
    tgPolling = false;
  } else {
    ensureTaskStarted();
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
  status.taskRunning = tgTaskRunning;
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
