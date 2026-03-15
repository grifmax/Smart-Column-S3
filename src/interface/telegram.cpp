/**
 * Smart-Column S3 - Telegram bot
 *
 * FastBot2 backend for notifications and simple command handling.
 */

#include "telegram.h"

#include <FastBot2.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <ArduinoJson.h>
#include <cstdarg>
#include <cstdio>

#include "control/fsm.h"

static FastBot2* tgBot = nullptr;
static String tgChatId;
static bool tgEnabled = false;
static bool tgReady = false;
static bool tgOnline = false;
static bool tgPolling = false;
static TaskHandle_t tgTaskHandle = NULL;
static SemaphoreHandle_t tgMutex = NULL;
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
static String tgToken;
static int32_t tgNextUpdateId = 0;

static constexpr TickType_t kTelegramIdleDelayTicks = pdMS_TO_TICKS(500);
static constexpr TickType_t kTelegramOfflineDelayTicks = pdMS_TO_TICKS(2000);
static constexpr TickType_t kTelegramPollDelayTicks = pdMS_TO_TICKS(4000);
static constexpr TickType_t kTelegramLockTimeoutTicks = pdMS_TO_TICKS(5000);

// Параметры для отложенной инициализации
static bool tgNeedInit = false;
static String pendingToken;
static String pendingChatId;

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

static void ensureMutexReady() {
  if (tgMutex == NULL) {
    tgMutex = xSemaphoreCreateRecursiveMutex();
  }
}

static bool lockBot(TickType_t timeoutTicks = portMAX_DELAY) {
  ensureMutexReady();
  if (tgMutex == NULL) {
    setLastError("telegram mutex init failed");
    return false;
  }

  const bool locked = xSemaphoreTakeRecursive(tgMutex, timeoutTicks) == pdTRUE;
  if (!locked) {
    tgLockTimeoutCount++;
    setLastError("telegram mutex timeout");
  }
  return locked;
}

static void unlockBot() {
  if (tgMutex != NULL) {
    xSemaphoreGiveRecursive(tgMutex);
  }
}

static bool isAuthorizedChat(const fb::ID& id) {
  if (!tgChatId.length()) return false;
  String idStr;
  su::Text(id).toString(idStr);
  return idStr == tgChatId;
}

static bool sendMessageWithKeyb(const fb::ID& chatId, const char* message, fb::InlineMenu* menu = nullptr) {
  if (!tgBot || !message || !message[0]) return false;
  if (!lockBot(kTelegramLockTimeoutTicks)) return false;

  fb::Message out(message, chatId);
  if (menu) out.setInlineMenu(*menu);
  
  fb::Result res = tgBot->sendMessage(out, true);
  if (res.isError()) {
    tgSendErrorCount++;
    setLastError(res.getError().toString());
    LOG_W("Telegram: send failed (%s)", res.getError().toString().c_str());
    unlockBot();
    return false;
  }
  tgSendOkCount++;
  clearLastError();
  unlockBot();
  return true;
}

static bool sendMessageToChat(const fb::ID& chatId, const char* message) {
  return sendMessageWithKeyb(chatId, message, nullptr);
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

static void answerCallbackById(const String& callbackId) {
  if (!tgBot || !callbackId.length()) return;
  if (!lockBot(kTelegramLockTimeoutTicks)) return;

  fb::Result res = tgBot->answerCallbackQuery(callbackId, "", false, true);
  if (res.isError()) {
    tgSendErrorCount++;
    setLastError(res.getError().toString());
  } else {
    tgSendOkCount++;
    clearLastError();
  }
  unlockBot();
}

static void handleTextCommand(const fb::ID& chatId, String text);
static void handleCallbackCommand(const fb::ID& chatId, String data, const String& callbackId);
static void sendDetailedHealth(const fb::ID& chatId);

static void sendStatus(const fb::ID& chatId) {
    char status[256];
    const bool active = (g_state.mode != Mode::IDLE);
    snprintf(status, sizeof(status),
             "🤖 *Статус системы*\n"
             "Режим: %s\n"
             "Фаза: %s\n"
             "Состояние: %s\n"
             "Мощность: %.0f Вт\n"
             "Куб: %.1f °C\n"
             "Аптайм: %lu сек",
             FSM::getModeName(g_state.mode),
             FSM::getPhaseName(g_state.rectPhase),
             g_state.paused ? "ПАУЗА" : (active ? "РАБОТА" : "ОЖИДАНИЕ"),
             g_state.power.power,
             g_state.temps.cube,
             (unsigned long)g_state.uptime);

    fb::InlineMenu menu;
    if (active) {
        menu.addButton("▶️ ПУСК/⏸ ПАУЗА", "tg_pause");
        menu.addButton("🛑 СТОП", "tg_stop");
        menu.newRow();
    }
    menu.addButton("🩺 ЗДОРОВЬЕ", "tg_health");
    menu.addButton("❓ ПОМОЩЬ", "tg_help");
    
    sendMessageWithKeyb(chatId, status, &menu);
}

static void sendHealth(const fb::ID& chatId) {
    char msg[384];
    snprintf(msg, sizeof(msg),
             "🩺 *Здоровье системы*\n"
             "Общий статус: %u%%\n"
             "Причина ребута: %s\n"
             "Свободно Heap: %u КБ\n"
             "Темп. CPU: %.1f °C\n"
             "Сигнал WiFi: %d dBm\n"
             "PZEM: %s, ADS: %s\n"
             "Датчики: %u/%u OK",
             g_state.health.overallHealth,
             g_rebootTracker.lastReasonStr,
             g_state.health.freeHeap / 1024,
             g_state.health.cpuTemp,
             g_state.health.wifiRSSI,
             g_state.health.pzemOk ? "OK" : "FAIL",
             g_state.health.ads1115Ok ? "OK" : "FAIL",
             g_state.health.tempSensorsOk, g_state.health.tempSensorsTotal);

    fb::InlineMenu menu;
    menu.addButton("📊 ДЕТАЛИ", "tg_diag");
    menu.addButton("🔄 ОБНОВИТЬ", "tg_health");
    menu.addButton("⬅️ НАЗАД", "tg_status");
    
    sendMessageWithKeyb(chatId, msg, &menu);
}

static void handleTextCommand(const fb::ID& chatId, String text) {
  text.trim();
  text.toLowerCase();
  if (!text.length()) return;

  if (text == "/status" || text == "/start") {
    sendStatus(chatId);
  } else if (text == "/health") {
    sendHealth(chatId);
  } else if (text == "/diag") {
    sendDetailedHealth(chatId);
  } else if (text == "/stop") {
    FSM::stopMode(g_state);
    sendMessageToChat(chatId, "рџ›‘ РџСЂРѕС†РµСЃСЃ РѕСЃС‚Р°РЅРѕРІР»РµРЅ");
  } else {
    sendStatus(chatId);
  }
}

static void handleCallbackCommand(const fb::ID& chatId, String data, const String& callbackId) {
  if (data == "tg_status") {
    sendStatus(chatId);
  } else if (data == "tg_health") {
    sendHealth(chatId);
  } else if (data == "tg_diag") {
    sendDetailedHealth(chatId);
  } else if (data == "tg_pause") {
    if (g_state.paused) FSM::resume(g_state);
    else FSM::pause(g_state);
    sendStatus(chatId);
  } else if (data == "tg_stop") {
    fb::InlineMenu menu;
    menu.addButton("вњ… Р”Рђ, РћРЎРўРђРќРћР’РРўР¬", "tg_stop_confirm");
    menu.addButton("вќЊ РћРўРњР•РќРђ", "tg_status");
    sendMessageWithKeyb(chatId, "вљ пёЏ *Р’С‹ СѓРІРµСЂРµРЅС‹, С‡С‚Рѕ С…РѕС‚РёС‚Рµ РѕСЃС‚Р°РЅРѕРІРёС‚СЊ РїСЂРѕС†РµСЃСЃ?*", &menu);
  } else if (data == "tg_stop_confirm") {
    FSM::stopMode(g_state);
    sendMessageToChat(chatId, "рџ›‘ РџСЂРѕС†РµСЃСЃ РїРѕР»РЅРѕСЃС‚СЊСЋ РѕСЃС‚Р°РЅРѕРІР»РµРЅ");
  } else if (data == "tg_help") {
    sendMessageToChat(chatId, "Р”РѕСЃС‚СѓРїРЅС‹Рµ РєРѕРјР°РЅРґС‹: /status, /health, /stop");
  }

  answerCallbackById(callbackId);
}

static void sendDetailedHealth(const fb::ID& chatId) {
    char msg[512];
    int len = snprintf(msg, sizeof(msg), "📊 *Детальная диагностика*\n\n");
    
    const char* subs[] = {"Сенсоры", "WiFi", "Питание", "Память", "OTA", "Безопасность"};
    for (int i = 0; i < 6; i++) {
        const char* icon = (g_state.health.healthScores[i] > 90) ? "✅" : (g_state.health.healthScores[i] > 70 ? "⚠️" : "❌");
        len += snprintf(msg + len, sizeof(msg) - len, "%s %s: %.0f%%\n", icon, subs[i], g_state.health.healthScores[i]);
    }
    
    len += snprintf(msg + len, sizeof(msg) - len, "\n🛠 *Ошибки датчиков:*\n");
    for (int i = 0; i < 7; i++) {
        if (g_state.health.tempErrors[i] > 0) {
            len += snprintf(msg + len, sizeof(msg) - len, "- Датчик %d: %u\n", i, g_state.health.tempErrors[i]);
        }
    }
    
    len += snprintf(msg + len, sizeof(msg) - len, "\n📈 *Система:*\n- Uptime: %lu сек\n- Heap: %u B\n- WiFi RSSI: %d",
                    (unsigned long)g_state.uptime, g_state.health.freeHeap, g_state.health.wifiRSSI);

    fb::InlineMenu menu;
    menu.addButton("🔄 ОБНОВИТЬ", "tg_diag");
    menu.addButton("⬅️ НАЗАД", "tg_health");
    
    sendMessageWithKeyb(chatId, msg, &menu);
}

static void handleUpdate(fb::Update& u) {
  tgUpdateCount++;
  tgLastUpdateMs = millis();
  clearLastError();

  if (u.isMessage()) {
    tgMessageCount++;
    fb::MessageRead msg = u.message();
    if (!isAuthorizedChat(msg.chat().id())) return;

    String text;
    msg.text().toString(text);
    handleTextCommand(msg.chat().id(), text);
    return;
    text.trim();
    text.toLowerCase();
    if (!text.length()) return;

    fb::ID chatId = msg.chat().id();
    if (text == "/status" || text == "/start") {
      sendStatus(chatId);
    } else if (text == "/health") {
      sendHealth(chatId);
    } else if (text == "/diag") {
      sendDetailedHealth(chatId);
    } else if (text == "/stop") {
      FSM::stopMode(g_state);
      sendMessageToChat(chatId, "🛑 Процесс остановлен");
    } else {
      sendStatus(chatId);
    }
  } 
  else if (u.isQuery()) {
    tgQueryCount++;
    fb::QueryRead callback = u.query();
    if (!isAuthorizedChat(callback.message().chat().id())) return;

    String data;
    callback.data().toString(data);
    String callbackId;
    callback.id().toString(callbackId);
    handleCallbackCommand(callback.message().chat().id(), data, callbackId);
    return;
    fb::ID chatId = callback.message().chat().id();
    
    if (data == "tg_status") {
      sendStatus(chatId);
    } else if (data == "tg_health") {
      sendHealth(chatId);
    } else if (data == "tg_diag") {
      sendDetailedHealth(chatId);
    } else if (data == "tg_pause") {
      if (g_state.paused) FSM::resume(g_state);
      else FSM::pause(g_state);
      sendStatus(chatId);
    } else if (data == "tg_stop") {
      fb::InlineMenu menu;
      menu.addButton("✅ ДА, ОСТАНОВИТЬ", "tg_stop_confirm");
      menu.addButton("❌ ОТМЕНА", "tg_status");
      sendMessageWithKeyb(chatId, "⚠️ *Вы уверены, что хотите остановить процесс?*", &menu);
    } else if (data == "tg_stop_confirm") {
      FSM::stopMode(g_state);
      sendMessageToChat(chatId, "🛑 Процесс полностью остановлен");
    } else if (data == "tg_help") {
        sendMessageToChat(chatId, "Доступные команды: /status, /health, /stop");
    }

    tgBot->answerCallbackQuery(callback.id());
  }
}

static bool pollTelegramUpdates() {
  if (!tgToken.length()) {
    setLastError("telegram token missing");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = "https://api.telegram.org/bot" + tgToken + "/getUpdates?limit=5&timeout=1";
  if (tgNextUpdateId > 0) {
    url += "&offset=" + String(tgNextUpdateId);
  }

  tgPolling = true;
  https.useHTTP10(true);
  https.setReuse(false);
  if (!https.begin(client, url)) {
    tgPolling = false;
    setLastError("telegram getUpdates begin failed");
    return false;
  }

  https.setTimeout(5000);
  https.addHeader("Connection", "close");
  const int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    tgPolling = false;
    setLastError("telegram getUpdates http " + String(httpCode));
    https.end();
    return false;
  }

  const String responseBody = https.getString();
  https.end();
  tgPolling = false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, responseBody);

  if (error) {
    setLastError(String("telegram json error: ") + error.c_str());
    return false;
  }

  if (!(doc["ok"] | false)) {
    setLastError("telegram getUpdates api error");
    return false;
  }

  JsonArray updates = doc["result"].as<JsonArray>();
  if (updates.isNull()) {
    clearLastError();
    return true;
  }

  for (JsonObject update : updates) {
    const int32_t updateId = update["update_id"] | 0;
    if (updateId >= tgNextUpdateId) {
      tgNextUpdateId = updateId + 1;
    }

    JsonObject message = update["message"].as<JsonObject>();
    if (!message.isNull()) {
      fb::ID chatId = jsonIdToFbId(message["chat"]["id"]);
      if (!isAuthorizedChat(chatId)) continue;
      String text = message["text"] | "";
      tgUpdateCount++;
      tgMessageCount++;
      tgLastUpdateMs = millis();
      handleTextCommand(chatId, text);
      continue;
    }

    JsonObject callback = update["callback_query"].as<JsonObject>();
    if (!callback.isNull()) {
      fb::ID chatId = jsonIdToFbId(callback["message"]["chat"]["id"]);
      if (!isAuthorizedChat(chatId)) continue;
      String data = callback["data"] | "";
      String callbackId = callback["id"] | "";
      tgUpdateCount++;
      tgQueryCount++;
      tgLastUpdateMs = millis();
      handleCallbackCommand(chatId, data, callbackId);
    }
  }

  clearLastError();
  return true;
}

static void telegramPollTask(void* /*param*/) {
  for (;;) {
    if (!tgEnabled || !tgReady || tgBot == nullptr) {
      vTaskDelay(kTelegramIdleDelayTicks);
      continue;
    }

    tgOnline = (WiFi.status() == WL_CONNECTED);
    if (!tgOnline) {
      vTaskDelay(kTelegramOfflineDelayTicks);
      continue;
    }

    tgLastTickMs = millis();
    tgTickCount++;
    pollTelegramUpdates();

    vTaskDelay(kTelegramPollDelayTicks);
  }
}

static void ensureTaskStarted() {
  if (tgTaskHandle != NULL) {
    return;
  }

  const BaseType_t taskOk =
      xTaskCreatePinnedToCore(telegramPollTask, "telegram", 6144, NULL, 1, &tgTaskHandle, 0);
  if (taskOk != pdPASS) {
    tgTaskHandle = NULL;
    setLastError("telegram task create failed");
    LOG_E("Telegram: failed to create poll task");
  }
}

void performInit(const char* token, const char* chat) {
  ensureMutexReady();
  if (!lockBot(kTelegramLockTimeoutTicks)) {
    return;
  }

  if (tgBot) {
    tgBot->end();
    delete tgBot;
    tgBot = nullptr;
  }

  tgBot = new FastBot2();
  tgBot->setToken(token);
  tgBot->setPollMode(fb::Poll::Sync, 4000);
  tgBot->skipUpdates();
  tgBot->attachUpdate(handleUpdate);
  tgOnline = (WiFi.status() == WL_CONNECTED);
  tgBot->setOnline(tgOnline);
  tgBot->begin();

  tgChatId = chat;
  tgChatId.trim();
  tgToken = token;
  tgNextUpdateId = 0;
  tgReady = true;
  tgNeedInit = false;
  tgLastInitMs = millis();
  tgLastTickMs = 0;
  clearLastError();
  unlockBot();
  ensureTaskStarted();
  LOG_I("Telegram: Init complete");
}

}  // namespace

namespace TelegramBot {

void init(const char* token, const char* chat) {
  if (!token || !token[0] || !chat || !chat[0]) {
    tgEnabled = false;
    return;
  }
  pendingToken = token;
  pendingChatId = chat;
  tgNeedInit = true;
  tgEnabled = true;
}

void update() {
  if (!tgEnabled) return;

  if (tgNeedInit) {
    performInit(pendingToken.c_str(), pendingChatId.c_str());
    return;
  }

  if (!tgBot || !tgReady) return;
  tgOnline = (WiFi.status() == WL_CONNECTED);
  ensureTaskStarted();
}

bool sendMessage(const char* message) {
  if (!isConfigured() || !message) return false;
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
  const char* phases[] = {"Ожидание", "Нагрев", "Стабилизация", "Головы", "Продувка",
                          "Тело", "Хвосты", "Финиш", "Ошибка"};
  const uint8_t idx = static_cast<uint8_t>(phase);
  const char* phaseName = (idx < (sizeof(phases) / sizeof(phases[0]))) ? phases[idx] : "???";
  const float totalVolume = stats.headsVolume + stats.bodyVolume + stats.tailsVolume;
  sendFormatted("🚀 Смена фазы: %s\nСобрано: %.0f мл", phaseName, totalVolume);
}

void notifyAlarm(const Alarm& alarm) {
  sendFormatted("⚠️ ТРЕВОГА: %s", alarm.message);
}

void notifyFinish(const RunStats& stats) {
  const float totalVolume = stats.headsVolume + stats.bodyVolume + stats.tailsVolume;
  sendFormatted("✅ Процесс завершен\nГоловы: %.0f мл\nТело: %.0f мл\nХвосты: %.0f мл\nВсего: %.0f мл",
                stats.headsVolume, stats.bodyVolume, stats.tailsVolume, totalVolume);
}

void notifyHealthAlert(const SystemHealth& health) {
  char msg[512];
  int len = 0;
  len += snprintf(msg + len, sizeof(msg) - len, "🛑 Критическое состояние системы!\n\nЗдоровье: %u%%\n", health.overallHealth);
  if (!health.pzemOk) len += snprintf(msg + len, sizeof(msg) - len, "- Ошибка PZEM (питание)\n");
  if (!health.ads1115Ok) len += snprintf(msg + len, sizeof(msg) - len, "- Ошибка ADS1115 (давление)\n");
  if (health.tempSensorsOk < health.tempSensorsTotal) {
    len += snprintf(msg + len, sizeof(msg) - len, "- Отказ датчиков: %u/%u OK\n",
                    health.tempSensorsOk, health.tempSensorsTotal);
  }
  sendMessage(msg);
}

bool sendScreenshot() { return false; }

void setEnabled(bool enabled) {
  tgEnabled = enabled;
  if (!enabled && tgBot) {
    if (lockBot(kTelegramLockTimeoutTicks)) {
      tgBot->end();
      unlockBot();
    }
    tgReady = false;
    tgOnline = false;
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
  status.online = tgOnline;
  status.polling = tgPolling;
  status.needInit = tgNeedInit;
  status.taskRunning = tgTaskHandle != NULL;
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
