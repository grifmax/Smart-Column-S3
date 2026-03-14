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

// Параметры для отложенной инициализации
static bool tgNeedInit = false;
static String pendingToken;
static String pendingChatId;

namespace {

static bool isConfigured() {
  return tgEnabled && tgBot && tgReady && tgChatId.length();
}

static bool isAuthorizedChat(const fb::ID& id) {
  if (!tgChatId.length()) return false;
  String idStr;
  su::Text(id).toString(idStr);
  return idStr == tgChatId;
}

static bool sendMessageWithKeyb(const fb::ID& chatId, const char* message, fb::InlineMenu* menu = nullptr) {
  if (!tgBot || !message || !message[0]) return false;

  fb::Message out(message, chatId);
  if (menu) out.setInlineMenu(*menu);
  
  fb::Result res = tgBot->sendMessage(out, true);
  if (res.isError()) {
    LOG_W("Telegram: send failed (%s)", res.getError().toString().c_str());
    return false;
  }
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
  if (u.isMessage()) {
    fb::MessageRead msg = u.message();
    if (!isAuthorizedChat(msg.chat().id())) return;

    String text;
    msg.text().toString(text);
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
    fb::QueryRead callback = u.query();
    if (!isAuthorizedChat(callback.message().chat().id())) return;

    String data;
    callback.data().toString(data);
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

void performInit(const char* token, const char* chat) {
  if (tgBot) {
    tgBot->end();
    delete tgBot;
    tgBot = nullptr;
  }

  tgBot = new FastBot2();
  tgBot->setToken(token);
  tgBot->setPollMode(fb::Poll::Long, 15000);
  tgBot->skipUpdates();
  tgBot->attachUpdate(handleUpdate);
  tgBot->setOnline(WiFi.status() == WL_CONNECTED);
  tgBot->begin();

  tgChatId = chat;
  tgChatId.trim();
  tgReady = true;
  tgNeedInit = false;
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

static uint32_t lastTickMs = 0;
static uint32_t retryIntervalMs = 5000;
const uint32_t MIN_RETRY_MS = 5000;
const uint32_t MAX_RETRY_MS = 60000;

void update() {
  if (!tgEnabled) return;
  uint32_t now = millis();

  if (tgNeedInit) {
    performInit(pendingToken.c_str(), pendingChatId.c_str());
    lastTickMs = now;
    return;
  }

  if (!tgBot || !tgReady) return;

  const bool online = (WiFi.status() == WL_CONNECTED);
  tgBot->setOnline(online);
  
  if (!online) {
    if (retryIntervalMs < MAX_RETRY_MS) {
        retryIntervalMs = min(retryIntervalMs * 2, MAX_RETRY_MS);
    }
    return;
  }

  if (now - lastTickMs >= retryIntervalMs) {
    lastTickMs = now;
    tgBot->tick();
    if (retryIntervalMs > MIN_RETRY_MS) retryIntervalMs = MIN_RETRY_MS;
  }
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
