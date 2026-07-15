#include "api_routes.h"

#include "config.h"
#include "control/fsm.h"
#include "control/rect_takeoff.h"
#include "drivers/display.h"
#include "drivers/heater.h"
#include "drivers/sensors.h"
#include "drivers/valves.h"
#include "interface/cloud_tunnel.h"
#include "interface/webserver_shared.h"
#include "profiles.h"

String buildBlockingRequiredSensorsList(
    const Safety::RequiredSensorsMask &required, const SystemState &state,
    bool includePressure) {
  String missing;

  auto appendMissing = [&](bool condition, const char *label) {
    if (!condition) {
      return;
    }
    if (!missing.isEmpty()) {
      missing += ", ";
    }
    missing += label;
  };

  appendMissing(required.cubeTemp && !state.temps.valid[TEMP_CUBE], "куб");
  appendMissing(required.columnBottomTemp &&
                    !state.temps.valid[TEMP_COLUMN_BOTTOM],
                "низ колонны");
  appendMissing(required.tsaTemp && !state.temps.valid[TEMP_TSA], "TSA");
  appendMissing(required.waterOutTemp && !state.temps.valid[TEMP_WATER_OUT],
                "выход воды");
  appendMissing(includePressure && required.pressure && !state.pressure.ok,
                "давление куба");

  return missing;
}

String buildMissingRequiredSensorsList(Mode mode, const Settings &settings,
                                       const SystemState &state) {
  const Safety::RequiredSensorsMask required =
      Safety::getRequiredSensorsForMode(mode, settings);
  String missing;

  auto appendMissing = [&](bool condition, const char *label) {
    if (!condition) {
      return;
    }
    if (!missing.isEmpty()) {
      missing += ", ";
    }
    missing += label;
  };

  appendMissing(required.cubeTemp && !state.temps.valid[TEMP_CUBE], "куб");
  appendMissing(required.columnBottomTemp &&
                    !state.temps.valid[TEMP_COLUMN_BOTTOM],
                "низ колонны");
  appendMissing(required.tsaTemp && !state.temps.valid[TEMP_TSA], "TSA");
  appendMissing(required.waterOutTemp && !state.temps.valid[TEMP_WATER_OUT],
                "выход воды");
  appendMissing(required.pressure && !state.pressure.ok, "давление куба");

  return missing;
}

String buildStartupMissingSensorsList(
    const Safety::RequiredSensorsMask &required, const SystemState &state,
    bool includePressure) {
  String missing;

  auto appendMissing = [&](bool condition, const char *label) {
    if (!condition) {
      return;
    }
    if (!missing.isEmpty()) {
      missing += ", ";
    }
    missing += label;
  };

  appendMissing(required.cubeTemp && !state.temps.valid[TEMP_CUBE], "куб");
  appendMissing(required.columnBottomTemp &&
                    !state.temps.valid[TEMP_COLUMN_BOTTOM],
                "низ колонны");
  appendMissing(required.tsaTemp && !state.temps.valid[TEMP_TSA], "TSA");
  appendMissing(required.waterOutTemp && !state.temps.valid[TEMP_WATER_OUT],
                "выход воды");
  appendMissing(includePressure && required.pressure && !state.pressure.ok,
                "давление куба");

  return missing;
}

void fillTemperatureTopologyJson(JsonObject topology,
                                 const EquipmentSettings &equipment) {
  topology["cube"] = Safety::isTempSensorInstalled(equipment, TEMP_CUBE);
  topology["columnBottom"] =
      Safety::isTempSensorInstalled(equipment, TEMP_COLUMN_BOTTOM);
  topology["columnTop"] =
      Safety::isTempSensorInstalled(equipment, TEMP_COLUMN_TOP);
  topology["reflux"] = Safety::isTempSensorInstalled(equipment, TEMP_REFLUX);
  topology["tsa"] = Safety::isTempSensorInstalled(equipment, TEMP_TSA);
  topology["waterIn"] = Safety::isTempSensorInstalled(equipment, TEMP_WATER_IN);
  topology["waterOut"] =
      Safety::isTempSensorInstalled(equipment, TEMP_WATER_OUT);
  topology["installedCount"] = Safety::getInstalledTempSensorCount(equipment);
}

void fillTemperatureModeSupportJson(JsonObject modes,
                                    const Settings &settings) {
  struct ModeItem {
    const char *key;
    Mode mode;
  };
  static const ModeItem kModes[] = {
      {"rectification", Mode::RECTIFICATION},
      {"manualRect", Mode::MANUAL_RECT},
      {"distillation", Mode::DISTILLATION},
      {"nbk", Mode::NBK},
      {"mashing", Mode::MASHING},
      {"hold", Mode::HOLD},
      {"fermentation", Mode::FERMENTATION},
  };

  for (const ModeItem &item : kModes) {
    char reason[160] = "";
    JsonObject modeJson = modes[item.key].to<JsonObject>();
    const bool supported = Safety::isModeTemperatureTopologySupported(
        item.mode, settings.equipment, reason, sizeof(reason));
    modeJson["supported"] = supported;
    modeJson["reason"] = supported ? "" : reason;
  }
}

void registerStatusApiRoutes(AsyncWebServer &server) {
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    ControlV2::updateRuntime(g_state, g_settings);
    syncStirrerState();

    JsonDocument doc;
    const String activeProfileId = getActiveProfileId();
    Profile activeProfile;
    const bool activeProfileLoaded =
        !activeProfileId.isEmpty() && loadProfile(activeProfileId, activeProfile);

    doc["mode"] = static_cast<int>(g_state.mode);
    doc["modeStr"] = getModeString(g_state.mode);
    int activePhase = static_cast<int>(g_state.rectPhase);
    const char *activePhaseStr = getPhaseString(g_state.rectPhase);
    if (g_state.mode == Mode::NBK) {
      activePhase = static_cast<int>(g_state.nbkPhase);
      activePhaseStr = getNbkPhaseString(g_state.nbkPhase);
    } else if (g_state.mode == Mode::FERMENTATION) {
      activePhase = static_cast<int>(g_state.fermPhase);
      activePhaseStr = getFermPhaseString(g_state.fermPhase);
    }
    doc["phase"] = activePhase;
    doc["phaseStr"] = activePhaseStr;
    doc["nbkPhase"] = static_cast<int>(g_state.nbkPhase);
    doc["nbkPhaseStr"] = getNbkPhaseString(g_state.nbkPhase);
    doc["fermPhase"] = static_cast<int>(g_state.fermPhase);
    doc["fermPhaseStr"] = getFermPhaseString(g_state.fermPhase);
    doc["paused"] = g_state.paused;
    doc["safetyOk"] = g_state.safetyOk;
    doc["uptime"] = g_state.uptime;
    doc["deviceId"] = CloudTunnel::getDeviceId();
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, g_state, g_settings);
    JsonObject activeProfileJson = doc["activeProfile"].to<JsonObject>();
    activeProfileJson["id"] =
        activeProfileLoaded ? activeProfile.id : activeProfileId;
    activeProfileJson["loaded"] = activeProfileLoaded;
    activeProfileJson["name"] =
        activeProfileLoaded ? activeProfile.metadata.name : "";
    activeProfileJson["category"] =
        activeProfileLoaded ? activeProfile.metadata.category : "";

    if (activeProfileLoaded) {
      JsonObject validation = activeProfileJson["validation"].to<JsonObject>();
      validation["validatedAt"] = activeProfile.validation.validatedAt;
      validation["sourceProcessId"] = activeProfile.validation.sourceProcessId;
      validation["atmosphereMmHg"] = activeProfile.validation.atmosphereMmHg;
      validation["columnHeightMm"] = activeProfile.validation.columnHeightMm;
      validation["packingType"] = activeProfile.validation.packingType;
      validation["packingCoeff"] = activeProfile.validation.packingCoeff;
      validation["heaterPowerW"] = activeProfile.validation.heaterPowerW;
      validation["targetPowerW"] = activeProfile.validation.targetPowerW;
      validation["feedVolumeL"] = activeProfile.validation.feedVolumeL;
      validation["feedAbvPercent"] = activeProfile.validation.feedAbvPercent;

      JsonObject baseTemperatures =
          activeProfileJson["baseTemperatures"].to<JsonObject>();
      baseTemperatures["maxCube"] = activeProfile.parameters.temperatures.maxCube;
      baseTemperatures["maxColumn"] =
          activeProfile.parameters.temperatures.maxColumn;
      baseTemperatures["headsEnd"] =
          activeProfile.parameters.temperatures.headsEnd;
      baseTemperatures["bodyStart"] =
          activeProfile.parameters.temperatures.bodyStart;
      baseTemperatures["bodyEnd"] =
          activeProfile.parameters.temperatures.bodyEnd;

      ProfileBaroCorrectionSummary baroPreview =
          evaluateProfileBaroCorrection(activeProfile, 1);
      TemperatureParams previewTemps =
          getEffectiveProfileTemperatures(activeProfile, nullptr, 1);

      JsonObject baroPreviewJson =
          activeProfileJson["baroPreview"].to<JsonObject>();
      baroPreviewJson["enabled"] = true;
      baroPreviewJson["applicable"] = baroPreview.applicable;
      baroPreviewJson["applied"] = baroPreview.applied;
      baroPreviewJson["baselinePressureMmHg"] =
          baroPreview.baselinePressureMmHg;
      baroPreviewJson["currentPressureMmHg"] =
          baroPreview.currentPressureMmHg;
      baroPreviewJson["pressureDeltaMmHg"] = baroPreview.pressureDeltaMmHg;
      baroPreviewJson["boilingShiftC"] = baroPreview.boilingShiftC;
      baroPreviewJson["appliedShiftC"] = baroPreview.appliedShiftC;
      baroPreviewJson["strength"] = baroPreview.strength;
      baroPreviewJson["maxShiftC"] = baroPreview.maxShiftC;
      baroPreviewJson["note"] = baroPreview.note;

      JsonObject previewEffectiveTemps =
          activeProfileJson["effectiveTemperaturesPreview"].to<JsonObject>();
      previewEffectiveTemps["maxCube"] = previewTemps.maxCube;
      previewEffectiveTemps["maxColumn"] = previewTemps.maxColumn;
      previewEffectiveTemps["headsEnd"] = previewTemps.headsEnd;
      previewEffectiveTemps["bodyStart"] = previewTemps.bodyStart;
      previewEffectiveTemps["bodyEnd"] = previewTemps.bodyEnd;

      if (activeProfile.metadata.category == "mashing" ||
          activeProfile.parameters.mode == "mashing") {
        JsonObject activeMashing = activeProfileJson["mashing"].to<JsonObject>();
        JsonArray activeMashingSteps = activeMashing["steps"].to<JsonArray>();
        for (const auto &stepData : activeProfile.parameters.mashing.steps) {
          JsonObject step = activeMashingSteps.add<JsonObject>();
          step["temperature"] = stepData.temperature;
          step["duration"] = stepData.duration;
          step["name"] = stepData.name;
        }
      }
    }

    JsonObject temps = doc["temps"].to<JsonObject>();
    temps["cube"] = g_state.temps.cube;
    temps["columnBottom"] = g_state.temps.columnBottom;
    temps["columnMiddle"] = g_state.temps.columnMiddle;
    temps["columnTop"] = g_state.temps.columnTop;
    temps["reflux"] = g_state.temps.reflux;
    temps["deflegmator"] = g_state.temps.deflegmator;
    temps["product"] = g_state.temps.product;
    temps["tsa"] = g_state.temps.tsa;
    temps["waterIn"] = g_state.temps.waterIn;
    temps["waterOut"] = g_state.temps.waterOut;

    JsonObject pressure = doc["pressure"].to<JsonObject>();
    pressure["cube"] = g_state.pressure.cube;
    pressure["atm"] = g_state.pressure.atmosphere;
    pressure["kpa"] = g_state.pressure.pressure;

    JsonObject power = doc["power"].to<JsonObject>();
    const Heater::Diagnostics heaterDiag = Heater::getDiagnostics();
    power["voltage"] = g_state.power.voltage;
    power["current"] = g_state.power.current;
    power["power"] = g_state.power.power;
    power["available"] = g_state.health.pzemOk;
    power["setPercent"] = heaterDiag.powerSetPercent;
    power["setW"] = heaterDiag.targetPowerWatts;
    power["errorW"] = heaterDiag.powerErrorWatts;
    power["backend"] = heaterDiag.triacMode ? "triac" : "ssr";
    power["boosterEnabled"] = heaterDiag.boosterEnabled;
    power["closedLoopActive"] = heaterDiag.closedLoopActive;
    power["zeroCrossSeen"] = heaterDiag.zeroCrossSeen;
    power["zeroCrossCount"] = heaterDiag.zeroCrossCount;
    power["triacFireCount"] = heaterDiag.triacFireCount;
    power["triacDelayUs"] = heaterDiag.triacDelayUs;
    power["energy"] = g_state.power.energy;
    power["frequency"] = g_state.power.frequency;
    power["pf"] = g_state.power.powerFactor;

    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["speedMlH"] = g_state.pump.speedMlPerHour;
    pump["totalMl"] = g_state.pump.totalVolumeMl;
    pump["running"] = g_state.pump.running;

    JsonObject stirrer = doc["stirrer"].to<JsonObject>();
    fillStirrerJson(stirrer, g_state);

    JsonObject valves = doc["valves"].to<JsonObject>();
    valves["water"] = Valves::getWater();
    valves["heads"] = Valves::getHeads();
    valves["body"] = Valves::getBody();
    valves["bodyAvailable"] = Valves::hasBodyValve();
    valves["tailsAvailable"] = Valves::hasTailsValve();
    valves["uno"] = Valves::getUno();
    valves["startStopDuty"] = Valves::getStartStop();
    valves["tails"] = Valves::getTails();

    JsonObject hydro = doc["hydrometer"].to<JsonObject>();
    hydro["abv"] = g_state.hydrometer.abv;
    hydro["pressure"] = g_state.hydrometer.pressure;
    hydro["valid"] = g_state.hydrometer.valid;

    JsonObject volumes = doc["volumes"].to<JsonObject>();
    volumes["heads"] = g_state.stats.headsVolume;
    volumes["body"] = g_state.stats.bodyVolume;
    volumes["tails"] = g_state.stats.tailsVolume;

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
    if (g_settings.equipment.heaterPowerW > 0) {
      distillation["powerPercent"] =
          static_cast<uint8_t>((static_cast<uint32_t>(distPowerWatts) * 100U +
                                g_settings.equipment.heaterPowerW / 2U) /
                               g_settings.equipment.heaterPowerW);
    }

    JsonObject nbk = doc["nbk"].to<JsonObject>();
    nbk["powerW"] = g_settings.nbk.powerW;
    nbk["pumpSpeedMlH"] = g_settings.nbk.pumpSpeedMlH;
    nbk["columnBottomTempThresholdC"] =
        g_settings.nbk.columnBottomTempThresholdC;
    nbk["phase"] = static_cast<int>(g_state.nbkPhase);
    nbk["phaseStr"] = getNbkPhaseString(g_state.nbkPhase);

    JsonObject fermentation = doc["fermentation"].to<JsonObject>();
    fermentation["targetTempC"] = g_settings.fermentation.targetTempC;
    fermentation["hysteresisC"] = g_settings.fermentation.hysteresisC;
    fermentation["useHeater"] = g_settings.fermentation.useHeater;
    fermentation["phase"] = static_cast<int>(g_state.fermPhase);
    fermentation["phaseStr"] = getFermPhaseString(g_state.fermPhase);

    JsonObject progress = doc["progress"].to<JsonObject>();
    const uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
    const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(g_state, g_settings);
    progress["phaseElapsedSec"] = phaseElapsedSec;
    progress["phaseTargetSec"] = phaseTargetSec;
    progress["phaseRemainingSec"] =
        (phaseTargetSec > phaseElapsedSec) ? (phaseTargetSec - phaseElapsedSec)
                                           : 0;
    progress["phasePercent"] = FSM::getPhaseProgressPercent(g_state, g_settings);

    JsonObject v2 = doc["v2"].to<JsonObject>();
    fillV2StatusJson(v2, ControlV2::getLatestModeStatus(),
                     ControlV2::getLatestMetricsSnapshot());

    const auto displayStats = Display::getRuntimeStats();
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

    JsonObject cloud = doc["cloud"].to<JsonObject>();
    cloud["enabled"] = g_settings.cloud.enabled;
    cloud["tunnelUrl"] = g_settings.cloud.tunnelUrl;
    cloud["connected"] = CloudTunnel::isConnected();
    cloud["authenticated"] = CloudTunnel::isAuthenticated();
    cloud["claimActive"] = CloudTunnel::hasActiveClaim();
    if (CloudTunnel::hasActiveClaim()) {
      cloud["claimCode"] = CloudTunnel::getClaimCode();
      cloud["claimExpiresAt"] = CloudTunnel::getClaimExpiresAt();
    }

    const uint32_t now = millis();

    JsonObject mashing = doc["mashing"].to<JsonObject>();
    mashing["active"] = g_state.mashing.active;
    mashing["phase"] = static_cast<int>(g_state.mashing.phase);
    mashing["phaseStr"] = getMashPhaseString(g_state.mashing.phase);
    mashing["stepCount"] = g_state.mashing.stepCount;
    mashing["currentStep"] = g_state.mashing.currentStep;
    mashing["targetTemp"] = g_state.mashing.targetTemp;
    mashing["stepDurationSec"] = g_state.mashing.stepDuration;
    mashing["tempInRange"] = g_state.mashing.tempInRange;
    mashing["stepName"] = g_state.mashing.stepName;

    uint32_t mashElapsedSec = 0;
    if (g_state.mashing.tempInRange && g_state.mashing.inRangeStartTime > 0 &&
        now >= g_state.mashing.inRangeStartTime) {
      mashElapsedSec = (now - g_state.mashing.inRangeStartTime) / 1000UL;
    }
    mashing["elapsedSec"] = mashElapsedSec;
    mashing["remainingSec"] =
        (g_state.mashing.stepDuration > mashElapsedSec)
            ? (g_state.mashing.stepDuration - mashElapsedSec)
            : 0;

    JsonObject fractionProgram = doc["fractionProgram"].to<JsonObject>();
    fractionProgram["enabled"] = g_settings.fractionProgram.enabled;
    fractionProgram["active"] = g_state.fractionProgram.active;
    fractionProgram["currentStep"] = g_state.fractionProgram.currentStep;
    fractionProgram["waitingForConfirmation"] = g_state.fractionProgram.waitingForConfirmation;
    fractionProgram["routing"] = g_state.fractionProgram.routing;
    fractionProgram["lastEndReason"] = g_state.fractionProgram.lastEndReason;
    fractionProgram["requestedRoute"] = static_cast<uint8_t>(takeoffFeedback.requestedFraction);
    fractionProgram["routedRoute"] = static_cast<uint8_t>(takeoffFeedback.routedFraction);
    fractionProgram["actualRateMlH"] = takeoffFeedback.actualEquivalentRateMlH;
    fractionProgram["collectedMl"] = g_state.pump.totalVolumeMl - g_state.fractionProgram.stepStartVolumeMl;
    fractionProgram["confirmationPrompt"] = g_state.fractionProgram.confirmationPrompt;
    if (g_state.fractionProgram.currentStep < g_settings.fractionProgram.stepCount) {
      const FractionProgramStep &step = g_settings.fractionProgram.steps[g_state.fractionProgram.currentStep];
      fractionProgram["stepName"] = step.name;
      fractionProgram["targetRoute"] = step.routeIndex;
      fractionProgram["targetRateMlH"] = step.pumpRateMlH;
      fractionProgram["allowManualAdvance"] = step.allowManualAdvance;
      fractionProgram["endConditions"] = step.endConditions;
      fractionProgram["endVolumeMl"] = step.endVolumeMl;
      fractionProgram["endDurationSec"] = step.endDurationSec;
      fractionProgram["temperatureSensorIndex"] = step.temperatureSensorIndex;
      fractionProgram["endTemperatureC"] = step.endTemperatureC;
    } else {
      fractionProgram["stepName"] = "";
      fractionProgram["targetRoute"] = 0;
      fractionProgram["targetRateMlH"] = 0.0f;
      fractionProgram["allowManualAdvance"] = false;
      fractionProgram["endConditions"] = FRACTION_PROGRAM_END_NONE;
      fractionProgram["endVolumeMl"] = 0.0f;
      fractionProgram["endDurationSec"] = 0;
      fractionProgram["temperatureSensorIndex"] = 0;
      fractionProgram["endTemperatureC"] = 0.0f;
    }
    JsonObject hold = doc["hold"].to<JsonObject>();
    hold["active"] = g_state.hold.active;
    hold["stepCount"] = g_state.hold.stepCount;
    hold["currentStep"] = g_state.hold.currentStep;
    hold["targetTemp"] = g_state.hold.targetTemp;
    hold["tempInRange"] = g_state.hold.tempInRange;

    uint32_t holdStepDurationSec = 0;
    if (g_state.hold.stepCount > 0 &&
        g_state.hold.currentStep < g_state.hold.stepCount) {
      holdStepDurationSec =
          static_cast<uint32_t>(
              g_state.hold.steps[g_state.hold.currentStep].duration) *
          60UL;
    }
    hold["stepDurationSec"] = holdStepDurationSec;

    uint32_t holdElapsedSec = 0;
    if (g_state.hold.tempInRange && g_state.hold.inRangeStartTime > 0 &&
        now >= g_state.hold.inRangeStartTime) {
      holdElapsedSec = (now - g_state.hold.inRangeStartTime) / 1000UL;
    }
    hold["elapsedSec"] = holdElapsedSec;
    hold["remainingSec"] =
        (holdStepDurationSec > holdElapsedSec)
            ? (holdStepDurationSec - holdElapsedSec)
            : 0;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
}
