#include "web_live.h"

#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESP.h>

#include "../config.h"
#include "../live_chart_history.h"
#include "control/fsm.h"
#include "control/rect_takeoff.h"
#include "drivers/display.h"
#include "drivers/sensors.h"
#include "drivers/valves.h"
#include "webserver_shared.h"

namespace {

AsyncWebSocket *g_liveSocket = nullptr;

} // namespace

namespace WebServerLive {

void bindWebSocket(AsyncWebSocket *socket) { g_liveSocket = socket; }

void broadcastState(const SystemState &state) {
  if (!g_liveSocket) {
    return;
  }

  LiveChartHistory::recordState(state, millis());
  g_liveSocket->cleanupClients();
  if (g_liveSocket->count() == 0) {
    return;
  }

  syncStirrerState();

  const uint32_t now = millis();
  const auto displayStats = Display::getRuntimeStats();

  JsonDocument fastDoc;
  fastDoc["mode"] = static_cast<int>(state.mode);
  fastDoc["modeStr"] = getModeString(state.mode);
  fastDoc["phase"] = static_cast<int>(state.rectPhase);
  fastDoc["phaseStr"] = getPhaseString(state.rectPhase);
  fastDoc["paused"] = state.paused;
  fastDoc["safetyOk"] = state.safetyOk;
  fastDoc["uptime"] = state.uptime;
  JsonObject fastAlarm = fastDoc["alarm"].to<JsonObject>();
  fillAlarmJson(fastAlarm, state, g_settings);
  JsonObject fastV2 = fastDoc["v2"].to<JsonObject>();
  fillV2StatusJson(fastV2, ControlV2::getLatestModeStatus(),
                   ControlV2::getLatestMetricsSnapshot());

  fastDoc["t_cube"] = state.temps.cube;
  fastDoc["t_column_bottom"] = state.temps.columnBottom;
  fastDoc["t_column_top"] = state.temps.columnTop;
  fastDoc["t_reflux"] = state.temps.reflux;
  fastDoc["t_tsa"] = state.temps.tsa;
  fastDoc["t_water_in"] = state.temps.waterIn;
  fastDoc["t_water_out"] = state.temps.waterOut;
  JsonObject fastTempValid = fastDoc["tempValid"].to<JsonObject>();
  fastTempValid["cube"] = state.temps.valid[TEMP_CUBE];
  fastTempValid["columnBottom"] = state.temps.valid[TEMP_COLUMN_BOTTOM];
  fastTempValid["columnTop"] = state.temps.valid[TEMP_COLUMN_TOP];
  fastTempValid["reflux"] = state.temps.valid[TEMP_REFLUX];
  fastTempValid["tsa"] = state.temps.valid[TEMP_TSA];
  fastTempValid["waterIn"] = state.temps.valid[TEMP_WATER_IN];
  fastTempValid["waterOut"] = state.temps.valid[TEMP_WATER_OUT];

  fastDoc["p_cube"] = state.pressure.cube;
  fastDoc["p_atm"] = state.pressure.atmosphere;

  fastDoc["voltage"] = state.power.voltage;
  fastDoc["current"] = state.power.current;
  fastDoc["power"] = state.power.power;
  fastDoc["energy"] = state.power.energy;
  fastDoc["frequency"] = state.power.frequency;
  fastDoc["pf"] = state.power.powerFactor;
  fastDoc["pzem_ok"] = state.health.pzemOk;

  fastDoc["pump_speed"] = state.pump.speedMlPerHour;
  fastDoc["pump_volume"] = state.pump.totalVolumeMl;
  fastDoc["speed"] = state.pump.speedMlPerHour;
  fastDoc["volume"] = state.pump.totalVolumeMl;
  fastDoc["volume_heads"] = state.stats.headsVolume;
  fastDoc["volume_body"] = state.stats.bodyVolume;
  fastDoc["volume_tails"] = state.stats.tailsVolume;
  JsonObject fastStirrer = fastDoc["stirrer"].to<JsonObject>();
  fillStirrerJson(fastStirrer, g_state);
  JsonObject fastEquipment = fastDoc["equipment"].to<JsonObject>();
  fastEquipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
  fastEquipment["columnHeightMm"] = g_settings.equipment.columnHeightMm;
  fastEquipment["packingCoeff"] = g_settings.equipment.packingCoeff;
  fastEquipment["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
  fastEquipment["minHeaterSubmergeL"] =
      g_settings.equipment.minHeaterSubmergeL;
  fastEquipment["waterAutoStartCubeTempC"] =
      g_settings.equipment.waterAutoStartCubeTempC;
  fastEquipment["boosterHeaterEnabled"] =
      g_settings.equipment.boosterHeaterEnabled;
  fastEquipment["boosterHeaterPowerW"] =
      g_settings.equipment.boosterHeaterPowerW;
  fastEquipment["boosterHeaterStopCubeTempC"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  fastEquipment["coolingPwmEnabled"] =
      g_settings.equipment.coolingPwmEnabled;
  fastEquipment["coolingPwmMinDuty"] =
      g_settings.equipment.coolingPwmMinDuty;
  fastEquipment["coolingPwmMaxDuty"] =
      g_settings.equipment.coolingPwmMaxDuty;
  fastEquipment["coolingPwmStartupDuty"] =
      g_settings.equipment.coolingPwmStartupDuty;
  fastEquipment["coolingPwmCurrentDuty"] = Valves::getStartStop();
  fastEquipment["useDs2482ForTemps"] =
      g_settings.equipment.useDs2482ForTemps;
  fastEquipment["ds2482Address"] = g_settings.equipment.ds2482Address;
  fastEquipment["tempBusGpioPin"] = PIN_ONEWIRE;
  fastEquipment["temperatureBusSource"] =
      Sensors::getTemperatureBusSourceKey();
  fastEquipment["temperatureBusSourceLabel"] =
      Sensors::getTemperatureBusSourceLabel();
  JsonObject fastSafetySettings = fastDoc["safetySettings"].to<JsonObject>();
  fastSafetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
  fastSafetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
  fastSafetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
  fastSafetySettings["waterOutRiseRateCMin"] =
      g_settings.safety.waterOutRiseRateCMin;
  fastSafetySettings["pressureRiseRateMmHgMin"] =
      g_settings.safety.pressureRiseRateMmHgMin;
  fastDoc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
  fastDoc["water_auto_start_cube_temp_c"] =
      g_settings.equipment.waterAutoStartCubeTempC;
  fastDoc["booster_heater_enabled"] =
      g_settings.equipment.boosterHeaterEnabled;
  fastDoc["booster_heater_power_w"] =
      g_settings.equipment.boosterHeaterPowerW;
  fastDoc["booster_heater_stop_cube_temp_c"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  fastDoc["cooling_pwm_enabled"] = g_settings.equipment.coolingPwmEnabled;
  fastDoc["cooling_pwm_min_duty"] = g_settings.equipment.coolingPwmMinDuty;
  fastDoc["cooling_pwm_max_duty"] = g_settings.equipment.coolingPwmMaxDuty;
  fastDoc["cooling_pwm_startup_duty"] =
      g_settings.equipment.coolingPwmStartupDuty;
  fastDoc["cooling_pwm_current_duty"] = Valves::getStartStop();
  fastDoc["safety_pressure_max_mmhg"] = g_settings.safety.pressureMaxMmHg;
  fastDoc["safety_tsa_max_c"] = g_settings.safety.tsaMaxC;
  fastDoc["safety_water_out_max_c"] = g_settings.safety.waterOutMaxC;
  fastDoc["safety_water_out_rise_rate_c_min"] =
      g_settings.safety.waterOutRiseRateCMin;
  fastDoc["safety_pressure_rise_rate_mmhg_min"] =
      g_settings.safety.pressureRiseRateMmHgMin;
  JsonObject fastValves = fastDoc["valves"].to<JsonObject>();
  fastValves["water"] = Valves::getWater();
  fastValves["heads"] = Valves::getHeads();
  fastValves["body"] = Valves::getBody();
  fastValves["bodyAvailable"] = Valves::hasBodyValve();
  fastValves["tailsAvailable"] = Valves::hasTailsValve();
  fastValves["uno"] = Valves::getUno();
  fastValves["startStopDuty"] = Valves::getStartStop();
  fastValves["tails"] = Valves::getTails();

  fastDoc["abv"] = state.hydrometer.abv;
  fastDoc["abv_valid"] = state.hydrometer.valid;
  const uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
  const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(state, g_settings);
  fastDoc["phase_elapsed_sec"] = phaseElapsedSec;
  fastDoc["phase_target_sec"] = phaseTargetSec;
  fastDoc["phase_remaining_sec"] =
      (phaseTargetSec > phaseElapsedSec) ? (phaseTargetSec - phaseElapsedSec)
                                         : 0;
  fastDoc["phase_percent"] = FSM::getPhaseProgressPercent(state, g_settings);
  fastDoc["display_last_ms"] = displayStats.lastFrameMs;
  fastDoc["display_full"] = displayStats.fullRedraws;
  fastDoc["display_partial"] = displayStats.partialRedraws;
  fastDoc["display_last_reason"] = displayStats.lastRedrawReason;
  fastDoc["display_slow"] = displayStats.slowFrames;
  fastDoc["display_hard"] = displayStats.hardWatchdogRecoveries;
  fastDoc["display_gap_ms"] = displayStats.lastUpdateGapMs;
  JsonObject fastRect = fastDoc["rectification"].to<JsonObject>();
  const RectTakeoffFeedback fastTakeoffFeedback = RectTakeoff::getFeedback();
  fastRect["takeoffBackendType"] =
      static_cast<uint8_t>(g_settings.rectParams.takeoffBackendType);
  fastRect["takeoffBackendActive"] = fastTakeoffFeedback.backendActive;
  fastRect["takeoffRoutingReady"] = fastTakeoffFeedback.routingReady;
  fastRect["takeoffActualEquivalentRateMlH"] =
      fastTakeoffFeedback.actualEquivalentRateMlH;
  fastRect["takeoffRequestedEquivalentRateMlH"] =
      fastTakeoffFeedback.requestedEquivalentRateMlH;
  fastRect["takeoffRateLimited"] = fastTakeoffFeedback.rateLimited;
  fastRect["takeoffActualDuty"] = fastTakeoffFeedback.actualDuty;
  fastRect["takeoffSessionVolumeMl"] = fastTakeoffFeedback.sessionVolumeMl;
  fastRect["takeoffRequestedFraction"] =
      static_cast<uint8_t>(fastTakeoffFeedback.requestedFraction);
  fastRect["takeoffRoutedFraction"] =
      static_cast<uint8_t>(fastTakeoffFeedback.routedFraction);
  fastRect["takeoffActiveFraction"] =
      static_cast<uint8_t>(fastTakeoffFeedback.activeFraction);
  fastRect["takeoffActiveValve"] =
      static_cast<uint8_t>(fastTakeoffFeedback.activeValve);

  String fastJson;
  serializeJson(fastDoc, fastJson);
  g_liveSocket->textAll(fastJson);

  static uint32_t lastFullBroadcast = 0;
  if (now - lastFullBroadcast < INTERVAL_WEB_BROADCAST_FULL) {
    return;
  }
  lastFullBroadcast = now;

  JsonDocument doc;
  doc["mode"] = static_cast<int>(state.mode);
  doc["modeStr"] = getModeString(state.mode);
  doc["phase"] = static_cast<int>(state.rectPhase);
  doc["phaseStr"] = getPhaseString(state.rectPhase);
  doc["paused"] = state.paused;
  doc["safetyOk"] = state.safetyOk;
  doc["uptime"] = state.uptime;
  JsonObject alarm = doc["alarm"].to<JsonObject>();
  fillAlarmJson(alarm, state, g_settings);
  JsonObject v2 = doc["v2"].to<JsonObject>();
  fillV2StatusJson(v2, ControlV2::getLatestModeStatus(),
                   ControlV2::getLatestMetricsSnapshot());

  doc["t_cube"] = state.temps.cube;
  doc["t_column_bottom"] = state.temps.columnBottom;
  doc["t_column_top"] = state.temps.columnTop;
  doc["t_reflux"] = state.temps.reflux;
  doc["t_tsa"] = state.temps.tsa;
  doc["t_water_in"] = state.temps.waterIn;
  doc["t_water_out"] = state.temps.waterOut;
  JsonObject tempValid = doc["tempValid"].to<JsonObject>();
  tempValid["cube"] = state.temps.valid[TEMP_CUBE];
  tempValid["columnBottom"] = state.temps.valid[TEMP_COLUMN_BOTTOM];
  tempValid["columnTop"] = state.temps.valid[TEMP_COLUMN_TOP];
  tempValid["reflux"] = state.temps.valid[TEMP_REFLUX];
  tempValid["tsa"] = state.temps.valid[TEMP_TSA];
  tempValid["waterIn"] = state.temps.valid[TEMP_WATER_IN];
  tempValid["waterOut"] = state.temps.valid[TEMP_WATER_OUT];

  doc["p_cube"] = state.pressure.cube;
  doc["p_atm"] = state.pressure.atmosphere;

  doc["voltage"] = state.power.voltage;
  doc["current"] = state.power.current;
  doc["power"] = state.power.power;
  doc["energy"] = state.power.energy;
  doc["frequency"] = state.power.frequency;
  doc["pf"] = state.power.powerFactor;
  doc["pzem_ok"] = state.health.pzemOk;

  doc["pump_speed"] = state.pump.speedMlPerHour;
  doc["pump_volume"] = state.pump.totalVolumeMl;
  doc["speed"] = state.pump.speedMlPerHour;
  doc["volume"] = state.pump.totalVolumeMl;
  doc["volume_heads"] = state.stats.headsVolume;
  doc["volume_body"] = state.stats.bodyVolume;
  doc["volume_tails"] = state.stats.tailsVolume;
  JsonObject valves = doc["valves"].to<JsonObject>();
  valves["water"] = Valves::getWater();
  valves["heads"] = Valves::getHeads();
  valves["body"] = Valves::getBody();
  valves["bodyAvailable"] = Valves::hasBodyValve();
  valves["tailsAvailable"] = Valves::hasTailsValve();
  valves["uno"] = Valves::getUno();
  valves["startStopDuty"] = Valves::getStartStop();
  valves["tails"] = Valves::getTails();

  doc["abv"] = state.hydrometer.abv;
  doc["abv_valid"] = state.hydrometer.valid;

  JsonObject progress = doc["progress"].to<JsonObject>();
  progress["phaseElapsedSec"] = phaseElapsedSec;
  progress["phaseTargetSec"] = phaseTargetSec;
  progress["phaseRemainingSec"] =
      (phaseTargetSec > phaseElapsedSec) ? (phaseTargetSec - phaseElapsedSec)
                                         : 0;
  progress["phasePercent"] = FSM::getPhaseProgressPercent(state, g_settings);

  JsonObject equipment = doc["equipment"].to<JsonObject>();
  equipment["heaterPowerW"] = g_settings.equipment.heaterPowerW;
  equipment["columnHeightMm"] = g_settings.equipment.columnHeightMm;
  equipment["packingCoeff"] = g_settings.equipment.packingCoeff;
  equipment["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
  equipment["minHeaterSubmergeL"] =
      g_settings.equipment.minHeaterSubmergeL;
  equipment["waterAutoStartCubeTempC"] =
      g_settings.equipment.waterAutoStartCubeTempC;
  equipment["boosterHeaterEnabled"] =
      g_settings.equipment.boosterHeaterEnabled;
  equipment["boosterHeaterPowerW"] =
      g_settings.equipment.boosterHeaterPowerW;
  equipment["boosterHeaterStopCubeTempC"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  equipment["coolingPwmEnabled"] =
      g_settings.equipment.coolingPwmEnabled;
  equipment["coolingPwmMinDuty"] =
      g_settings.equipment.coolingPwmMinDuty;
  equipment["coolingPwmMaxDuty"] =
      g_settings.equipment.coolingPwmMaxDuty;
  equipment["coolingPwmStartupDuty"] =
      g_settings.equipment.coolingPwmStartupDuty;
  equipment["coolingPwmCurrentDuty"] = Valves::getStartStop();
  equipment["useDs2482ForTemps"] =
      g_settings.equipment.useDs2482ForTemps;
  equipment["ds2482Address"] = g_settings.equipment.ds2482Address;
  equipment["tempBusGpioPin"] = PIN_ONEWIRE;
  equipment["temperatureBusSource"] =
      Sensors::getTemperatureBusSourceKey();
  equipment["temperatureBusSourceLabel"] =
      Sensors::getTemperatureBusSourceLabel();
  JsonObject safetySettings = doc["safetySettings"].to<JsonObject>();
  safetySettings["pressureMaxMmHg"] = g_settings.safety.pressureMaxMmHg;
  safetySettings["tsaMaxC"] = g_settings.safety.tsaMaxC;
  safetySettings["waterOutMaxC"] = g_settings.safety.waterOutMaxC;
  safetySettings["waterOutRiseRateCMin"] =
      g_settings.safety.waterOutRiseRateCMin;
  safetySettings["pressureRiseRateMmHgMin"] =
      g_settings.safety.pressureRiseRateMmHgMin;
  doc["min_heater_submerge_l"] = g_settings.equipment.minHeaterSubmergeL;
  doc["water_auto_start_cube_temp_c"] =
      g_settings.equipment.waterAutoStartCubeTempC;
  doc["booster_heater_enabled"] =
      g_settings.equipment.boosterHeaterEnabled;
  doc["booster_heater_power_w"] =
      g_settings.equipment.boosterHeaterPowerW;
  doc["booster_heater_stop_cube_temp_c"] =
      g_settings.equipment.boosterHeaterStopCubeTempC;
  doc["cooling_pwm_enabled"] = g_settings.equipment.coolingPwmEnabled;
  doc["cooling_pwm_min_duty"] = g_settings.equipment.coolingPwmMinDuty;
  doc["cooling_pwm_max_duty"] = g_settings.equipment.coolingPwmMaxDuty;
  doc["cooling_pwm_startup_duty"] =
      g_settings.equipment.coolingPwmStartupDuty;
  doc["cooling_pwm_current_duty"] = Valves::getStartStop();
  doc["safety_pressure_max_mmhg"] = g_settings.safety.pressureMaxMmHg;
  doc["safety_tsa_max_c"] = g_settings.safety.tsaMaxC;
  doc["safety_water_out_max_c"] = g_settings.safety.waterOutMaxC;
  doc["safety_water_out_rise_rate_c_min"] =
      g_settings.safety.waterOutRiseRateCMin;
  doc["safety_pressure_rise_rate_mmhg_min"] =
      g_settings.safety.pressureRiseRateMmHgMin;

  JsonObject rect = doc["rectification"].to<JsonObject>();
  const RectTakeoffFeedback takeoffFeedback = RectTakeoff::getFeedback();
  rect["feedVolumeL"] = g_settings.rectParams.feedVolumeL;
  rect["feedAbvPercent"] = g_settings.rectParams.feedAbvPercent;
  rect["headsPercent"] = g_settings.rectParams.headsPercent;
  rect["bodyPercent"] = g_settings.rectParams.bodyPercent;
  rect["tailsPercent"] = g_settings.rectParams.tailsPercent;
  rect["headsSpeedMlHKw"] = g_settings.rectParams.headsSpeedMlHKw;
  rect["bodySpeedMlHKw"] = g_settings.rectParams.bodySpeedMlHKw;
  rect["takeoffBackendType"] =
      static_cast<uint8_t>(g_settings.rectParams.takeoffBackendType);
  rect["takeoffBackendActive"] = takeoffFeedback.backendActive;
  rect["takeoffRoutingReady"] = takeoffFeedback.routingReady;
  rect["takeoffActualEquivalentRateMlH"] =
      takeoffFeedback.actualEquivalentRateMlH;
  rect["takeoffRequestedEquivalentRateMlH"] =
      takeoffFeedback.requestedEquivalentRateMlH;
  rect["takeoffRateLimited"] = takeoffFeedback.rateLimited;
  rect["takeoffActualDuty"] = takeoffFeedback.actualDuty;
  rect["takeoffSessionVolumeMl"] = takeoffFeedback.sessionVolumeMl;
  rect["takeoffRequestedFraction"] =
      static_cast<uint8_t>(takeoffFeedback.requestedFraction);
  rect["takeoffRoutedFraction"] =
      static_cast<uint8_t>(takeoffFeedback.routedFraction);
  rect["takeoffActiveFraction"] =
      static_cast<uint8_t>(takeoffFeedback.activeFraction);
  rect["takeoffActiveValve"] =
      static_cast<uint8_t>(takeoffFeedback.activeValve);
  rect["refluxMode"] = static_cast<uint8_t>(g_settings.rectParams.refluxMode);
  rect["srRatio"] = g_settings.rectParams.srRatio;
  rect["autonomousCycleSec"] = g_settings.rectParams.autonomousCycleSec;
  rect["autonomousPauseSec"] = g_settings.rectParams.autonomousPauseSec;
  rect["chimAutoPercent"] = g_settings.rectParams.chimAutoPercent;
  rect["chimTimePerH"] = g_settings.rectParams.chimTimePerH;
  rect["chimBegPercent"] = g_settings.rectParams.chimBegPercent;
  rect["chimMinPercent"] = g_settings.rectParams.chimMinPercent;
  rect["phasePowerStabilization"] =
      g_settings.rectParams.phasePowerPercent[RECT_POWER_STABILIZATION];
  rect["phasePowerHeads"] =
      g_settings.rectParams.phasePowerPercent[RECT_POWER_HEADS];
  rect["phasePowerBody"] =
      g_settings.rectParams.phasePowerPercent[RECT_POWER_BODY];
  rect["phasePowerTails"] =
      g_settings.rectParams.phasePowerPercent[RECT_POWER_TAILS];
  rect["usePbMode"] = g_settings.rectParams.usePbMode;
  rect["timpPbMs"] = g_settings.rectParams.timpPbMs;
  rect["routingSettlingMs"] = g_settings.rectParams.routingSettlingMs;
  rect["routingRetargetMinMs"] =
      g_settings.rectParams.routingRetargetMinMs;

  float rectHeadsTargetMl = 0.0f;
  float rectBodyTargetMl = 0.0f;
  float rectTailsTargetMl = 0.0f;
  FSM::getRectTargetsMl(rectHeadsTargetMl, rectBodyTargetMl, rectTailsTargetMl);
  rect["headsTargetMl"] = rectHeadsTargetMl;
  rect["bodyTargetMl"] = rectBodyTargetMl;
  rect["tailsTargetMl"] = rectTailsTargetMl;

  JsonObject distillation = doc["distillation"].to<JsonObject>();
  float distSpeedMlH = 0.0f;
  float distHeadsVolumeMl = 0.0f;
  float distTargetVolumeMl = 0.0f;
  float distEndTempC = 0.0f;
  uint16_t distPowerWatts = 0;
  FSM::getDistillationParams(distSpeedMlH, distHeadsVolumeMl,
                             distTargetVolumeMl, distEndTempC,
                             distPowerWatts);
  distillation["speedMlH"] = distSpeedMlH;
  distillation["headsVolumeMl"] = distHeadsVolumeMl;
  distillation["targetVolumeMl"] = distTargetVolumeMl;
  distillation["endTempC"] = distEndTempC;
  distillation["powerW"] = distPowerWatts;
  distillation["takeoffBackendType"] =
      static_cast<uint8_t>(g_settings.distillationUi.takeoffBackendType);
  distillation["valveSafeVentConfirmed"] =
      g_settings.distillationUi.valveSafeVentConfirmed;
  distillation["tailsVolumeMl"] = g_settings.distillationUi.tailsVolumeMl;
  JsonObject distillationFractionProgram =
      distillation["fractionProgram"].to<JsonObject>();
  fillFractionProgramJson(distillationFractionProgram,
                          g_settings.fractionProgram);
  JsonObject fractionProgramRuntime = doc["fractionProgram"].to<JsonObject>();
  fillFractionProgramRuntimeJson(fractionProgramRuntime, state, g_settings,
                                 takeoffFeedback);
  if (g_settings.equipment.heaterPowerW > 0) {
    distillation["powerPercent"] =
        static_cast<uint8_t>((static_cast<uint32_t>(distPowerWatts) * 100U +
                              g_settings.equipment.heaterPowerW / 2U) /
                             g_settings.equipment.heaterPowerW);
  }

  JsonObject display = doc["display"].to<JsonObject>();
  display["frames"] = displayStats.framesRendered;
  display["fullRedraws"] = displayStats.fullRedraws;
  display["partialRedraws"] = displayStats.partialRedraws;
  display["slowFrames"] = displayStats.slowFrames;
  display["recoveries"] = displayStats.watchdogRecoveries;
  display["hardRecoveries"] = displayStats.hardWatchdogRecoveries;
  display["hardFailures"] = displayStats.hardWatchdogFailures;
  display["lastFrameMs"] = displayStats.lastFrameMs;
  display["maxFrameMs"] = displayStats.maxFrameMs;
  display["lastFrameAt"] = displayStats.lastFrameAtMs;
  display["lastGapMs"] = displayStats.lastUpdateGapMs;
  display["maxGapMs"] = displayStats.maxUpdateGapMs;
  display["gapOverruns"] = displayStats.updateGapOverruns;
  display["lastReason"] = displayStats.lastRedrawReason;
  JsonObject redraw = display["reasons"].to<JsonObject>();
  redraw["screenEnter"] = displayStats.redrawReasonScreenEnter;
  redraw["tapAction"] = displayStats.redrawReasonTapAction;
  redraw["liveDataChanged"] = displayStats.redrawReasonLiveDataChanged;
  redraw["timerKeepalive"] = displayStats.redrawReasonTimerKeepalive;
  redraw["sparklineRefresh"] = displayStats.redrawReasonSparklineRefresh;
  redraw["themeChanged"] = displayStats.redrawReasonThemeChanged;
  redraw["languageChanged"] = displayStats.redrawReasonLanguageChanged;
  redraw["layoutChanged"] = displayStats.redrawReasonLayoutChanged;
  redraw["recovery"] = displayStats.redrawReasonRecovery;

  JsonObject mashing = doc["mashing"].to<JsonObject>();
  mashing["active"] = state.mashing.active;
  mashing["phase"] = static_cast<int>(state.mashing.phase);
  mashing["phaseStr"] = getMashPhaseString(state.mashing.phase);
  mashing["stepCount"] = state.mashing.stepCount;
  mashing["currentStep"] = state.mashing.currentStep;
  mashing["targetTemp"] = state.mashing.targetTemp;
  mashing["stepDurationSec"] = state.mashing.stepDuration;
  mashing["tempInRange"] = state.mashing.tempInRange;
  mashing["stepName"] = state.mashing.stepName;

  uint32_t mashElapsedSec = 0;
  if (state.mashing.tempInRange && state.mashing.inRangeStartTime > 0 &&
      now >= state.mashing.inRangeStartTime) {
    mashElapsedSec = (now - state.mashing.inRangeStartTime) / 1000UL;
  }
  mashing["elapsedSec"] = mashElapsedSec;
  mashing["remainingSec"] =
      (state.mashing.stepDuration > mashElapsedSec)
          ? (state.mashing.stepDuration - mashElapsedSec)
          : 0;

  JsonObject hold = doc["hold"].to<JsonObject>();
  hold["active"] = state.hold.active;
  hold["stepCount"] = state.hold.stepCount;
  hold["currentStep"] = state.hold.currentStep;
  hold["targetTemp"] = state.hold.targetTemp;
  hold["tempInRange"] = state.hold.tempInRange;

  uint32_t holdStepDurationSec = 0;
  if (state.hold.stepCount > 0 &&
      state.hold.currentStep < state.hold.stepCount) {
    holdStepDurationSec =
        static_cast<uint32_t>(state.hold.steps[state.hold.currentStep].duration) *
        60UL;
  }
  hold["stepDurationSec"] = holdStepDurationSec;

  uint32_t holdElapsedSec = 0;
  if (state.hold.tempInRange && state.hold.inRangeStartTime > 0 &&
      now >= state.hold.inRangeStartTime) {
    holdElapsedSec = (now - state.hold.inRangeStartTime) / 1000UL;
  }
  hold["elapsedSec"] = holdElapsedSec;
  hold["remainingSec"] =
      (holdStepDurationSec > holdElapsedSec)
          ? (holdStepDurationSec - holdElapsedSec)
          : 0;

  JsonObject mem = doc["memory"].to<JsonObject>();
  mem["heap_free"] = ESP.getFreeHeap();
  mem["heap_total"] = ESP.getHeapSize();
  mem["heap_used_pct"] =
      (ESP.getHeapSize() - ESP.getFreeHeap()) * 100 / ESP.getHeapSize();
  mem["psram_free"] = ESP.getFreePsram();
  mem["psram_total"] = ESP.getPsramSize();
  mem["flash_used"] = ESP.getSketchSize();
  mem["flash_total"] = ESP.getFlashChipSize();
  mem["flash_used_pct"] =
      ESP.getSketchSize() * 100 / ESP.getFlashChipSize();

  JsonObject health = doc["health"].to<JsonObject>();
  health["overall"] = state.health.overallHealth;
  health["tempSensorsOk"] = state.health.tempSensorsOk;
  health["tempSensorsTotal"] = state.health.tempSensorsTotal;
  health["bmp280"] = state.health.bmp280Ok;
  health["ads1115"] = state.health.ads1115Ok;
  health["pzem"] = state.health.pzemOk;
  health["wifiRSSI"] = state.health.wifiRSSI;
  health["pzemSpikes"] = state.health.pzemSpikeCount;
  health["tempErrors"] = state.health.tempReadErrors;
  health["cpuTemp"] = state.health.cpuTemp;

  JsonObject stirrer = doc["stirrer"].to<JsonObject>();
  fillStirrerJson(stirrer, g_state);

  String json;
  serializeJson(doc, json);
  g_liveSocket->textAll(json);
}

void broadcastEvent(const char *event, const char *message) {
  if (!g_liveSocket) {
    return;
  }

  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = event;
  doc["message"] = message;

  String json;
  serializeJson(doc, json);
  g_liveSocket->textAll(json);
}

} // namespace WebServerLive
