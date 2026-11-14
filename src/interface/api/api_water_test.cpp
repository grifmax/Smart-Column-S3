#include "api_routes.h"

#include "control/control_command_queue.h"
#include "control/safety.h"
#include "control/fsm.h"
#include "control/water_test.h"
#include "control/water_test_logic.h"
#include "interface/webserver_shared.h"

#include <ArduinoJson.h>

namespace {

void sendJson(AsyncWebServerRequest *request, int status, JsonDocument &doc) {
  String response;
  serializeJson(doc, response);
  request->send(status, "application/json", response);
}

void appendMeasurements(JsonObject obj, const WaterTestState &wt) {
  obj["heaterPowerAtFullW"] = wt.heaterPowerAtFullW;
  obj["heaterPowerAtLowW"] = wt.heaterPowerAtLowW;
  obj["heatRateCPerMin"] = wt.heatRateCPerMin;
  obj["coolingFlowLpm"] = wt.coolingFlowLpm;
  obj["coolingDeltaTC"] = wt.coolingDeltaTC;
  obj["coolingCapacityW"] = wt.coolingCapacityW;
  obj["pressureAtLowW"] = wt.pressureAtLowW;
  obj["pressureAtMidW"] = wt.pressureAtMidW;
  obj["pressureAtFullW"] = wt.pressureAtFullW;
  obj["suggestedPackingCoeff"] = wt.suggestedPackingCoeff;
  obj["commandedVolumeMl"] = wt.commandedVolumeMl;
  obj["measuredVolumeMl"] = wt.measuredValue;
}

void appendChecks(JsonDocument &doc) {
  FSM::WaterTest::CheckRecord const *checks = nullptr;
  const uint8_t count = FSM::WaterTest::getChecks(&checks);
  JsonArray array = doc["checks"].to<JsonArray>();
  for (uint8_t i = 0; i < count && checks != nullptr; ++i) {
    JsonObject item = array.add<JsonObject>();
    item["step"] = WaterTestLogic::stepKey(
        static_cast<WaterTestStep>(checks[i].step));
    item["id"] = checks[i].id;
    item["verdict"] = WaterTestLogic::verdictToken(
        static_cast<WaterTestVerdict>(checks[i].verdict));
    item["expected"] = checks[i].expected;
    item["actual"] = checks[i].actual;
    item["note"] = checks[i].note;
  }
}

} // namespace

void registerWaterTestApiRoutes(AsyncWebServer &server) {
  // Текущее состояние прогона и накопленный отчёт.
  server.on("/api/water-test/status", HTTP_GET,
            [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    const WaterTestState &wt = g_state.waterTest;
    doc["success"] = true;
    doc["active"] = wt.active;
    doc["mode"] = FSM::getModeName(g_state.mode);
    doc["step"] =
        WaterTestLogic::stepKey(wt.step);
    doc["stepTitle"] =
        WaterTestLogic::stepTitle(wt.step);
    doc["stepElapsedSec"] =
        wt.stepStartedMs > 0 ? (millis() - wt.stepStartedMs) / 1000UL : 0UL;
    doc["stepMaxSec"] =
        WaterTestLogic::stepMaxMs(wt.step) / 1000UL;
    doc["waitingForOperator"] = wt.waitingForOperator;
    doc["injectionActive"] = wt.injectionActive;

    JsonObject counters = doc["counters"].to<JsonObject>();
    counters["pass"] = wt.checksPass;
    counters["warn"] = wt.checksWarn;
    counters["fail"] = wt.checksFail;
    counters["skip"] = wt.checksSkip;
    doc["overall"] = WaterTestLogic::verdictToken(
        WaterTestLogic::overallVerdict(wt.checksPass, wt.checksWarn,
                                            wt.checksFail));

    JsonObject measurements = doc["measurements"].to<JsonObject>();
    appendMeasurements(measurements, wt);
    doc["lastError"] = wt.lastError;
    appendChecks(doc);
    sendJson(request, 200, doc);
  });

  // Запуск. Идёт через очередь команд, чтобы сработали все штатные гейты:
  // защёлкнутая авария, занятость режима, приоритет владельца выходов.
  server.on("/api/water-test/start", HTTP_POST,
            [](AsyncWebServerRequest *request) {
    const long rawId = request->hasParam("commandId")
                           ? request->getParam("commandId")->value().toInt()
                           : 0;
    uint32_t commandId = rawId > 0 ? static_cast<uint32_t>(rawId) : 0;
    ControlCommandQueue::Status status = ControlCommandQueue::Status::Unknown;
    const auto result = ControlCommandQueue::enqueue(
        ControlCommandQueue::Type::StartMode,
        ControlCommandQueue::Source::Api, Mode::WATER_TEST, commandId,
        ControlCommandQueue::Reason::OperatorRequest, &commandId, &status);

    JsonDocument doc;
    const bool accepted =
        result == ControlCommandQueue::EnqueueResult::Accepted ||
        result == ControlCommandQueue::EnqueueResult::Duplicate;
    // Команда применяется в следующем проходе управляющего цикла, поэтому
    // Queued — это штатный успех, а не отказ.
    const bool queued = status == ControlCommandQueue::Status::Queued;
    const bool applied = status == ControlCommandQueue::Status::Applied;
    doc["success"] = accepted && (queued || applied);
    doc["accepted"] = accepted;
    doc["queued"] = queued;
    doc["status"] = ControlCommandQueue::statusToken(status);
    doc["commandId"] = commandId;
    doc["error"] = g_state.waterTest.lastError;
    sendJson(request, accepted ? (queued ? 202 : 200) : 409, doc);
  });

  server.on("/api/water-test/stop", HTTP_POST,
            [](AsyncWebServerRequest *request) {
    const long rawId = request->hasParam("commandId")
                           ? request->getParam("commandId")->value().toInt()
                           : 0;
    uint32_t commandId = rawId > 0 ? static_cast<uint32_t>(rawId) : 0;
    ControlCommandQueue::Status status = ControlCommandQueue::Status::Unknown;
    const auto result = ControlCommandQueue::enqueue(
        ControlCommandQueue::Type::Stop, ControlCommandQueue::Source::Api,
        g_state.mode, commandId, ControlCommandQueue::Reason::OperatorRequest,
        &commandId, &status);

    JsonDocument doc;
    doc["success"] = result == ControlCommandQueue::EnqueueResult::Accepted;
    doc["status"] = ControlCommandQueue::statusToken(status);
    doc["commandId"] = commandId;
    sendJson(request, 200, doc);
  });

  // Замер объёма для шага отбора: оператор взвешивает собранное и вводит мл.
  server.on("/api/water-test/measure", HTTP_POST,
            [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    const bool hasValue = request->hasParam("valueMl") ||
                          request->hasParam("valueMl", true);
    const float valueMl =
        hasValue ? (request->hasParam("valueMl")
                        ? request->getParam("valueMl")->value().toFloat()
                        : request->getParam("valueMl", true)->value().toFloat())
                 : 0.0f;
    const bool ok = FSM::WaterTest::setMeasuredVolume(g_state, valueMl);
    doc["success"] = ok;
    doc["valueMl"] = valueMl;
    if (!ok) {
      doc["error"] = "замер принимается только на шаге отбора";
    }
    sendJson(request, ok ? 200 : 409, doc);
  });

  server.on("/api/water-test/confirm", HTTP_POST,
            [](AsyncWebServerRequest *request) {
    FSM::WaterTest::confirm(g_state);
    JsonDocument doc;
    doc["success"] = true;
    doc["step"] = WaterTestLogic::stepKey(g_state.waterTest.step);
    sendJson(request, 200, doc);
  });

  // Отчёт отдельным запросом: удобно снимать после прогона.
  server.on("/api/water-test/report", HTTP_GET,
            [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    const WaterTestState &wt = g_state.waterTest;
    doc["success"] = true;
    doc["firmware"] = FW_VERSION;
    doc["startedAtMs"] = wt.startedMs;
    doc["active"] = wt.active;
    doc["overall"] = WaterTestLogic::verdictToken(
        WaterTestLogic::overallVerdict(wt.checksPass, wt.checksWarn,
                                            wt.checksFail));
    doc["interruptedAtStep"] =
        wt.active ? "" : WaterTestLogic::stepKey(wt.step);
    JsonObject measurements = doc["measurements"].to<JsonObject>();
    appendMeasurements(measurements, wt);
    appendChecks(doc);
    sendJson(request, 200, doc);
  });
}
