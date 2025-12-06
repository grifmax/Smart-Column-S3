/**
 * Smart-Column S3 - Telegram бот
 *
 * Уведомления и удалённое управление через Telegram
 */

#include "telegram.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// Telegram API Root Certificate
const char TELEGRAM_CERTIFICATE_ROOT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j
ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL
MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3
LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug
RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm
+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW
PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM
xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB
Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3
hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg
EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF
MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA
FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec
nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z
eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF
hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2
Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe
vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep
+OkuE6N36B9K
-----END CERTIFICATE-----
)EOF";

static WiFiClientSecure client;
static UniversalTelegramBot* bot = nullptr;
static String chatId;
static uint32_t lastCheck = 0;

namespace TelegramBot {

void init(const char* token, const char* chat) {
    LOG_I("Telegram: Initializing...");

    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    bot = new UniversalTelegramBot(token, client);

    chatId = String(chat);

    // Отправить приветствие
    sendMessage("Smart-Column S3 started!");

    LOG_I("Telegram: Ready");
}

void update() {
    if (!bot) return;

    uint32_t now = millis();
    if (now - lastCheck < 1000) return; // Проверяем раз в секунду
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

void sendMessage(const char* message) {
    if (!bot || chatId.isEmpty()) return;

    bot->sendMessage(chatId, message, "");
    LOG_D("Telegram: Sent message: %s", message);
}

void notifyPhaseChange(RectPhase phase, const RunStats& stats) {
    const char* phases[] = {
        "Idle", "Heating", "Stabilization", "Heads",
        "Purge", "Body", "Tails", "Finish", "Error"
    };

    char msg[128];
    snprintf(msg, sizeof(msg), "Phase changed: %s\nVolume: %.0f ml",
             phases[static_cast<int>(phase)], stats.totalVolume);
    sendMessage(msg);
}

void notifyAlarm(const Alarm& alarm) {
    char msg[128];
    snprintf(msg, sizeof(msg), "⚠️ ALARM: %s", alarm.message);
    sendMessage(msg);
}

void notifyFinish(const RunStats& stats) {
    char msg[256];
    snprintf(msg, sizeof(msg),
             "✅ Process finished!\nHeads: %.0f ml\nBody: %.0f ml\nTails: %.0f ml\nTotal: %.0f ml",
             stats.headsVolume, stats.bodyVolume, stats.tailsVolume, stats.totalVolume);
    sendMessage(msg);
}

} // namespace TelegramBot
