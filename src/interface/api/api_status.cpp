#include "api_routes.h"

#include "config.h"
#include "control/fsm.h"
#include "control/rect_takeoff.h"
#include "drivers/display.h"
#include "drivers/heater.h"
#include "drivers/sensors.h"
#include "drivers/valves.h"
#include "interface/cloud_tunnel.h"
#include "control/state_snapshot.h"
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
  const uint8_t installedCount = Safety::getInstalledTempSensorCount(equipment);
  topology["installedCount"] = installedCount;
  topology["configurationTier"] =
      installedCount < TEMP_COUNT ? "lite" : "full";
  topology["isLite"] = installedCount < TEMP_COUNT;
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
    // Снимок состояния: g_state обновляет главный цикл на другом ядре,
    // поэтому сетевой обработчик читает согласованную копию.
    SystemState snapshot;
    StateSnapshot::readOrLive(snapshot);
    const SystemState &st = snapshot;

    ControlV2::updateRuntime(st, g_settings);
    syncStirrerState();

    JsonDocument doc;
    const String activeProfileId = getActiveProfileId();
    Profile activeProfile;
    const bool activeProfileLoaded =
        !activeProfileId.isEmpty() && loadProfile(activeProfileId, activeProfile);

    doc["mode"] = static_cast<int>(st.mode);
    doc["modeStr"] = getModeString(st.mode);
    int activePhase = static_cast<int>(st.rectPhase);
    const char *activePhaseStr = getPhaseString(st.rectPhase);
    if (st.mode == Mode::NBK) {
      activePhase = static_cast<int>(st.nbkPhase);
      activePhaseStr = getNbkPhaseString(st.nbkPhase);
    } else if (st.mode == Mode::FERMENTATION) {
      activePhase = static_cast<int>(st.fermPhase);
      activePhaseStr = getFermPhaseString(st.fermPhase);
    }
    doc["phase"] = activePhase;
    doc["phaseStr"] = activePhaseStr;
    doc["nbkPhase"] = static_cast<int>(st.nbkPhase);
    doc["nbkPhaseStr"] = getNbkPhaseString(st.nbkPhase);
    doc["fermPhase"] = static_cast<int>(st.fermPhase);
    doc["fermPhaseStr"] = getFermPhaseString(st.fermPhase);
    doc["paused"] = st.paused;
    doc["safetyOk"] = st.safetyOk;
    doc["uptime"] = st.uptime;
    doc["deviceId"] = CloudTunnel::getDeviceId();
    JsonObject alarm = doc["alarm"].to<JsonObject>();
    fillAlarmJson(alarm, st, g_settings);
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
    temps["cube"] = st.temps.cube;
    temps["columnBottom"] = st.temps.columnBottom;
    temps["columnMiddle"] = st.temps.columnMiddle;
    temps["columnTop"] = st.temps.columnTop;
    temps["reflux"] = st.temps.reflux;
    temps["deflegmator"] = st.temps.deflegmator;
    temps["product"] = st.temps.product;
    temps["tsa"] = st.temps.tsa;
    temps["waterIn"] = st.temps.waterIn;
    temps["waterOut"] = st.temps.waterOut;

    JsonObject pressure = doc["pressure"].to<JsonObject>();
    pressure["cube"] = st.pressure.cube;
    pressure["atm"] = st.pressure.atmosphere;
    pressure["kpa"] = st.pressure.pressure;

    JsonObject leak = doc["leak"].to<JsonObject>();
    leak["enabled"] = g_settings.equipment.leakSensorEnabled;
    leak["valid"] = st.leak.valid;
    leak["triggered"] = st.leak.triggered;
    leak["adc"] = st.leak.adc;
    leak["voltage"] = st.leak.voltage;
    leak["thresholdV"] = g_settings.equipment.leakThresholdV;
    leak["triggerAbove"] = g_settings.equipment.leakTriggerAbove;
    leak["lastUpdate"] = st.leak.lastUpdate;
    JsonObject vapor = doc["vaporSensors"].to<JsonObject>();
    vapor["enabled"] = VAPOR_SENSOR_ENABLED != 0;
    vapor["thresholdMv"] = VAPOR_SENSOR_THRESHOLD_MV;
    vapor["triggerAbove"] = VAPOR_SENSOR_TRIGGER_ABOVE != 0;
    vapor["primaryValid"] = st.vaporPrimary.valid;
    vapor["primaryTriggered"] = st.vaporPrimary.triggered;
    vapor["primaryVoltage"] = st.vaporPrimary.voltage;
    vapor["secondaryValid"] = st.vaporSecondary.valid;
    vapor["secondaryTriggered"] = st.vaporSecondary.triggered;
    vapor["secondaryVoltage"] = st.vaporSecondary.voltage;

    JsonObject power = doc["power"].to<JsonObject>();
    const Heater::Diagnostics heaterDiag = Heater::getDiagnostics();
    power["voltage"] = st.power.voltage;
    power["current"] = st.power.current;
    power["power"] = st.power.power;
    power["available"] = st.health.pzemOk;
    power["setPercent"] = heaterDiag.powerSetPercent;
    power["setW"] = heaterDiag.targetPowerWatts;
    power["errorW"] = heaterDiag.powerErrorWatts;
    power["backend"] = heaterDiag.triacMode ? "triac" : "ssr";
    power["boosterEnabled"] = heaterDiag.boosterEnabled;
    power["safetyRelayAvailable"] = heaterDiag.safetyRelayAvailable;
    power["safetyRelayEnergized"] = heaterDiag.safetyRelayEnergized;
    power["closedLoopActive"] = heaterDiag.closedLoopActive;
    power["zeroCrossSeen"] = heaterDiag.zeroCrossSeen;
    power["zeroCrossCount"] = heaterDiag.zeroCrossCount;
    power["triacFireCount"] = heaterDiag.triacFireCount;
    power["triacDelayUs"] = heaterDiag.triacDelayUs;
    power["energy"] = st.power.energy;
    power["frequency"] = st.power.frequency;
    power["pf"] = st.power.powerFactor;

    JsonObject pump = doc["pump"].to<JsonObject>();
    pump["speedMlH"] = st.pump.speedMlPerHour;
    pump["totalMl"] = st.pump.totalVolumeMl;
    pump["running"] = st.pump.running;

    JsonObject stirrer = doc["stirrer"].to<JsonObject>();
    fillStirrerJson(stirrer, st);

    JsonObject valves = doc["valves"].to<JsonObject>();
    valves["water"] = Valves::getWater();
    valves["heads"] = Valves::getHeads();
    valves["body"] = Valves::getBody();
    valves["bodyAvailable"] = Valves::hasBodyValve();
    valves["tailsAvailable"] = Valves::hasTailsValve();
    valves["uno"] = Valves::getUno();
    valves["startStopDuty"] = Valves::getStartStop();
    valves["normallyOpenMask"] = g_settings.valves.normallyOpenMask;
    valves["tails"] = Valves::getTails();

    JsonObject hydro = doc["hydrometer"].to<JsonObject>();
    hydro["abv"] = st.hydrometer.abv;
    hydro["pressure"] = st.hydrometer.pressure;
    hydro["valid"] = st.hydrometer.valid;

    JsonObject volumes = doc["volumes"].to<JsonObject>();
    volumes["heads"] = st.stats.headsVolume;
    volumes["body"] = st.stats.bodyVolume;
    volumes["tails"] = st.stats.tailsVolume;

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
    rect["bodyContainerCount"] = g_settings.rectParams.bodyContainerCount;
    rect["activeBodyContainer"] = st.rectBodyContainerIndex + 1;
    rect["activeBodyContainerVolumeMl"] = st.rectBodyContainerVolumeMl;
    rect["activeBodyContainerStartVolumeMl"] =
        st.rectBodyContainerStartVolumeMl;
    rect["activeBodyContainerLevelEnabled"] =
        g_settings.equipment.bodyLevelSensorEnabled;
    rect["activeBodyContainerLevelReached"] =
        st.rectBodyContainerLevelReached;
    rect["headsContainerLevelReached"] =
        st.rectHeadsContainerLevelReached;
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
    rect["pressureControlEnabled"] =
        g_settings.rectParams.pressureControlEnabled;
    rect["pressureMinPowerPercent"] =
        g_settings.rectParams.pressureMinPowerPercent;

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
    distillation["vaporTempControlEnabled"] =
        g_settings.distillationUi.vaporTempControlEnabled;
    distillation["vaporTempTargetC"] = g_settings.distillationUi.vaporTempTargetC;
    distillation["vaporTempMinPowerPercent"] =
        g_settings.distillationUi.vaporTempMinPowerPercent;
    distillation["vaporTempMaxPowerPercent"] =
        g_settings.distillationUi.vaporTempMaxPowerPercent;
    distillation["vaporTempTimeoutMin"] =
        g_settings.distillationUi.vaporTempTimeoutMin;
    JsonObject distillationFractionProgram =
        distillation["fractionProgram"].to<JsonObject>();
    fillFractionProgramJson(distillationFractionProgram,
                            g_settings.fractionProgram);
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
    nbk["feedBackend"] = "peristaltic_internal";
    nbk["requestedFeedMlH"] = st.nbk.requestedFeedMlH;
    nbk["correctedFeedMlH"] = st.nbk.correctedFeedMlH;
    nbk["actualFeedMlH"] = st.pump.speedMlPerHour;
    nbk["feedVolumeMl"] = st.pump.totalVolumeMl;
    nbk["feedCorrectionPercent"] = st.nbk.feedCorrectionPercent;
    nbk["limitingFactor"] = st.nbk.limitingFactor;
    nbk["phase"] = static_cast<int>(st.nbkPhase);
    nbk["phaseStr"] = getNbkPhaseString(st.nbkPhase);

    JsonObject fermentation = doc["fermentation"].to<JsonObject>();
    fermentation["targetTempC"] = g_settings.fermentation.targetTempC;
    fermentation["hysteresisC"] = g_settings.fermentation.hysteresisC;
    fermentation["useHeater"] = g_settings.fermentation.useHeater;
    fermentation["phase"] = static_cast<int>(st.fermPhase);
    fermentation["phaseStr"] = getFermPhaseString(st.fermPhase);

    JsonObject progress = doc["progress"].to<JsonObject>();
    const uint32_t phaseElapsedSec = FSM::getPhaseElapsedSec();
    const uint32_t phaseTargetSec = FSM::getPhaseTargetSec(st, g_settings);
    progress["phaseElapsedSec"] = phaseElapsedSec;
    progress["phaseTargetSec"] = phaseTargetSec;
    progress["phaseRemainingSec"] =
        (phaseTargetSec > phaseElapsedSec) ? (phaseTargetSec - phaseElapsedSec)
                                           : 0;
    progress["phasePercent"] = FSM::getPhaseProgressPercent(st, g_settings);

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
    mashing["active"] = st.mashing.active;
    mashing["phase"] = static_cast<int>(st.mashing.phase);
    mashing["phaseStr"] = getMashPhaseString(st.mashing.phase);
    mashing["stepCount"] = st.mashing.stepCount;
    mashing["currentStep"] = st.mashing.currentStep;
    mashing["targetTemp"] = st.mashing.targetTemp;
    mashing["stepDurationSec"] = st.mashing.stepDuration;
    mashing["tempInRange"] = st.mashing.tempInRange;
    mashing["stepName"] = st.mashing.stepName;
    mashing["stepType"] = mashStepTypeToString(st.mashing.stepType);
    mashing["currentTemp"] = st.temps.cube;
    mashing["operatorAction"] = st.mashing.operatorAction;

    uint32_t mashElapsedSec = 0;
    if (st.mashing.tempInRange && st.mashing.inRangeStartTime > 0 &&
        now >= st.mashing.inRangeStartTime) {
      mashElapsedSec = (now - st.mashing.inRangeStartTime) / 1000UL;
    }
    mashing["elapsedSec"] = mashElapsedSec;
    mashing["remainingSec"] =
        (st.mashing.stepDuration > mashElapsedSec)
            ? (st.mashing.stepDuration - mashElapsedSec)
            : 0;

    JsonObject fractionProgram = doc["fractionProgram"].to<JsonObject>();
    fillFractionProgramRuntimeJson(fractionProgram, st, g_settings,
                                   takeoffFeedback);
    JsonObject hold = doc["hold"].to<JsonObject>();
    hold["active"] = st.hold.active;
    hold["stepCount"] = st.hold.stepCount;
    hold["currentStep"] = st.hold.currentStep;
    hold["targetTemp"] = st.hold.targetTemp;
    hold["tempInRange"] = st.hold.tempInRange;

    uint32_t holdStepDurationSec = 0;
    if (st.hold.stepCount > 0 &&
        st.hold.currentStep < st.hold.stepCount) {
      holdStepDurationSec =
          static_cast<uint32_t>(
              st.hold.steps[st.hold.currentStep].duration) *
          60UL;
    }
    hold["stepDurationSec"] = holdStepDurationSec;

    uint32_t holdElapsedSec = 0;
    if (st.hold.tempInRange && st.hold.inRangeStartTime > 0 &&
        now >= st.hold.inRangeStartTime) {
      holdElapsedSec = (now - st.hold.inRangeStartTime) / 1000UL;
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
