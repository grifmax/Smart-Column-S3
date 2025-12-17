/**
 * Smart-Column S3 - Telegram бот
 *
 * Уведомления и удалённое управление через Telegram
 */

#include "telegram.h"
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>

// TelegramCertificate.h уже включён в UniversalTelegramBot.h

static WiFiClientSecure client;
static UniversalTelegramBot *bot = nullptr;
static String chatId;
static uint32_t lastCheck = 0;

namespace TelegramBot {

void init(const char *token, const char *chat) {
  LOG_I("Telegram: Initializing...");

  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  bot = new UniversalTelegramBot(token, client);

  chatId = String(chat);

  // Отправить приветствие
  sendMessage("Smart-Column S3 started!");

  LOG_I("Telegram: Ready");
}

void update() {
  if (!bot)
    return;

  uint32_t now = millis();
  if (now - lastCheck < 1000)
    return; // Проверяем раз в секунду
  lastCheck = now;

  int numNewMessages = bot->getUpdates(bot->last_message_received + 1);

  for (int i = 0; i < numNewMessages; i++) {
    String text = bot->messages[i].text;
    String from = bot->messages[i].chat_id;

    LOG_I("Telegram: Message from %s: %s", from.c_str(), text.c_str());

    // Обработка команд
    if (text == "/status") {
      sendMessage("System status: OK");
    } else if (text == "/start_rect") {
      sendMessage("Starting rectification...");
    } else if (text == "/stop") {
      sendMessage("Stopping process...");
    } else {
      sendMessage("Unknown command. Try /status");
    }
  }
}

bool sendMessage(const char *message) {
  if (!bot || chatId.isEmpty())
    return false;

  bool sent = bot->sendMessage(chatId, message, "");
  LOG_D("Telegram: Sent message: %s", message);
  return sent;
}

void notifyPhaseChange(RectPhase phase, const RunStats &stats) {
  const char *phases[] = {"Idle", "Heating", "Stabilization", "Heads", "Purge",
                          "Body", "Tails",   "Finish",        "Error"};

  float totalVolume = stats.headsVolume + stats.bodyVolume + stats.tailsVolume;
  char msg[128];
  snprintf(msg, sizeof(msg), "Phase changed: %s\nVolume: %.0f ml",
           phases[static_cast<int>(phase)], totalVolume);
  sendMessage(msg);
}

void notifyAlarm(const Alarm &alarm) {
  char msg[128];
  snprintf(msg, sizeof(msg), "⚠️ ALARM: %s", alarm.message);
  sendMessage(msg);
}

void notifyFinish(const RunStats &stats) {
  float totalVolume = stats.headsVolume + stats.bodyVolume + stats.tailsVolume;
  char msg[256];
  snprintf(msg, sizeof(msg),
           "✅ Process finished!\nHeads: %.0f ml\nBody: %.0f ml\nTails: %.0f "
           "ml\nTotal: %.0f ml",
           stats.headsVolume, stats.bodyVolume, stats.tailsVolume, totalVolume);
  sendMessage(msg);
}

void notifyHealthAlert(const SystemHealth &health) {
  char msg[512];
  int len = 0;

  len += snprintf(msg + len, sizeof(msg) - len, "⚠️ System Health Alert!\n\n");
  len += snprintf(msg + len, sizeof(msg) - len, "Overall Health: %d%%\n\n",
                  health.overallHealth);

  // Проблемные датчики
  if (!health.pzemOk) {
    len += snprintf(msg + len, sizeof(msg) - len, "❌ PZEM power meter\n");
  }
  if (!health.ads1115Ok) {
    len += snprintf(msg + len, sizeof(msg) - len, "❌ ADS1115 ADC\n");
  }
  if (!health.bmp280Ok) {
    len += snprintf(msg + len, sizeof(msg) - len, "❌ BMP280 sensor\n");
  }
  if (health.tempSensorsOk < health.tempSensorsTotal) {
    len += snprintf(msg + len, sizeof(msg) - len,
                    "⚠️ Temperature sensors: %d/%d OK\n", health.tempSensorsOk,
                    health.tempSensorsTotal);
  }

  // Статистика ошибок
  if (health.pzemSpikeCount > 0) {
    len += snprintf(msg + len, sizeof(msg) - len, "\nPZEM spikes: %d\n",
                    health.pzemSpikeCount);
  }
  if (health.tempReadErrors > 0) {
    len += snprintf(msg + len, sizeof(msg) - len, "Temp errors: %d\n",
                    health.tempReadErrors);
  }

  // WiFi
  if (health.wifiConnected && health.wifiRSSI < -80) {
    len += snprintf(msg + len, sizeof(msg) - len,
                    "\n⚠️ Weak WiFi signal: %d dBm", health.wifiRSSI);
  }

  sendMessage(msg);
}

} // namespace TelegramBot
