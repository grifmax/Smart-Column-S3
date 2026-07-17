/**
 * Smart-Column S3 - Profiles Manager Implementation
 */

#include "profiles.h"
#include "drivers/sensors.h"
#include "history.h"
#include "interface/webserver_shared.h"
#include <FS.h>
#include <algorithm>
#include <cmath>
#include "storage/nvs_manager.h"
#include "types.h"

namespace {

String g_activeProfileId;
String g_activeProfileName;

constexpr uint8_t PROFILE_MASHING_MAX_STEPS = 10;
constexpr uint16_t PROFILE_MASHING_MAX_DURATION_MIN = 240;
constexpr float PROFILE_MASHING_MIN_TEMP_C = 20.0f;
constexpr float PROFILE_MASHING_MAX_TEMP_C = 100.0f;

float computeEnergyPerLiter(const ProcessHistory& history) {
    const float energyUsed = history.metrics.energyUsed;
    const float totalCollectedMl =
        static_cast<float>(history.results.totalCollected);
    if (energyUsed <= 0.0f || totalCollectedMl <= 0.0f) {
        return 0.0f;
    }
    return energyUsed / (totalCollectedMl / 1000.0f);
}

float updateRollingAverage(float currentAverage, float sample, uint16_t sampleCount) {
    if (sampleCount <= 1) {
        return sample;
    }
    return ((currentAverage * static_cast<float>(sampleCount - 1)) + sample) /
           static_cast<float>(sampleCount);
}

uint32_t updateRollingAverageU32(uint32_t currentAverage,
                                 uint32_t sample,
                                 uint16_t sampleCount) {
    if (sampleCount <= 1) {
        return sample;
    }
    const uint64_t total =
        (static_cast<uint64_t>(currentAverage) * static_cast<uint64_t>(sampleCount - 1)) +
        static_cast<uint64_t>(sample);
    return static_cast<uint32_t>(total / sampleCount);
}

uint16_t updateRollingAverageU16(uint16_t currentAverage,
                                 uint16_t sample,
                                 uint16_t sampleCount) {
    if (sampleCount <= 1) {
        return sample;
    }
    const uint32_t total =
        (static_cast<uint32_t>(currentAverage) * static_cast<uint32_t>(sampleCount - 1)) +
        static_cast<uint32_t>(sample);
    return static_cast<uint16_t>(total / sampleCount);
}

const ProcessPhase* findPhase(const ProcessHistory& history, const char* name) {
    for (const auto& phase : history.phases) {
        if (phase.name == name) {
            return &phase;
        }
    }
    return nullptr;
}

String packingTypeToString(PackingType type) {
    switch (type) {
        case PackingType::SPN_3_5:
            return "spn_3_5";
        case PackingType::SPN_4_0:
            return "spn_4_0";
        case PackingType::RASCHIG:
            return "raschig";
        case PackingType::CUSTOM:
            return "custom";
        default:
            return "unknown";
    }
}

float hpaToMmHg(float valueHpa) {
    if (valueHpa <= 0.0f) {
        return 0.0f;
    }
    return valueHpa * 0.75006156f;
}

String normalizeMashStepName(const String& value, size_t index) {
    String name = value;
    name.trim();
    if (name.length() > 31) {
        name = name.substring(0, 31);
    }
    if (name.isEmpty()) {
        name = String("Шаг ") + String(static_cast<uint32_t>(index + 1));
    }
    return name;
}

float clampMashTemperature(float value) {
    if (value < PROFILE_MASHING_MIN_TEMP_C) {
        return PROFILE_MASHING_MIN_TEMP_C;
    }
    if (value > PROFILE_MASHING_MAX_TEMP_C) {
        return PROFILE_MASHING_MAX_TEMP_C;
    }
    return value;
}

uint16_t clampMashDuration(uint16_t value) {
    if (value == 0) {
        return 0;
    }
    if (value > PROFILE_MASHING_MAX_DURATION_MIN) {
        return PROFILE_MASHING_MAX_DURATION_MIN;
    }
    return value;
}

uint8_t clampProfileU8(uint32_t value, uint8_t minValue, uint8_t maxValue) {
    return static_cast<uint8_t>(
        std::min<uint32_t>(maxValue, std::max<uint32_t>(minValue, value)));
}

uint16_t clampProfileU16(uint32_t value, uint16_t minValue, uint16_t maxValue) {
    return static_cast<uint16_t>(
        std::min<uint32_t>(maxValue, std::max<uint32_t>(minValue, value)));
}

float clampProfileFloat(float value, float minValue, float maxValue) {
    return std::min(maxValue, std::max(minValue, value));
}

void appendMashingJson(JsonObject parameters, const MashingParams& mashing) {
    JsonObject mashingJson = parameters["mashing"].to<JsonObject>();
    JsonArray steps = mashingJson["steps"].to<JsonArray>();
    for (size_t index = 0;
         index < mashing.steps.size() && index < PROFILE_MASHING_MAX_STEPS;
         index++) {
        const auto& source = mashing.steps[index];
        JsonObject step = steps.add<JsonObject>();
        step["type"] = mashStepTypeToString(source.type);
        step["temperature"] = source.temperature;
        step["duration"] = source.duration;
        step["name"] = source.name;
    }
}

void appendRectificationJson(JsonObject parameters,
                             const RectificationParams& rectification) {
    JsonObject rectificationJson = parameters["rectification"].to<JsonObject>();
    rectificationJson["stabilizationMin"] = rectification.stabilizationMin;
    rectificationJson["headsVolume"] = rectification.headsVolume;
    rectificationJson["bodyVolume"] = rectification.bodyVolume;
    rectificationJson["tailsVolume"] = rectification.tailsVolume;
    rectificationJson["headsSpeed"] = rectification.headsSpeed;
    rectificationJson["bodySpeed"] = rectification.bodySpeed;
    rectificationJson["tailsSpeed"] = rectification.tailsSpeed;
    rectificationJson["purgeMin"] = rectification.purgeMin;
    rectificationJson["baroCorrectionEnabled"] =
        rectification.baroCorrectionEnabled;
    rectificationJson["pressureControlEnabled"] =
        rectification.pressureControlEnabled;
    rectificationJson["pressureMinPowerPercent"] =
        rectification.pressureMinPowerPercent;
    rectificationJson["takeoffBackendType"] =
        static_cast<uint8_t>(rectification.takeoffBackendType);
    rectificationJson["refluxMode"] =
        static_cast<uint8_t>(rectification.refluxMode);
    rectificationJson["srTarget"] = rectification.srTarget;
    rectificationJson["srRatio"] = rectification.srTarget;
    rectificationJson["autonomousCycleSec"] =
        rectification.autonomousCycleSec;
    rectificationJson["autonomousPauseSec"] =
        rectification.autonomousPauseSec;
    rectificationJson["chimAutoPercent"] = rectification.chimAutoPercent;
    rectificationJson["chimTimePerH"] = rectification.chimTimePerH;
    rectificationJson["chimBegPercent"] = rectification.chimBegPercent;
    rectificationJson["chimMinPercent"] = rectification.chimMinPercent;
    rectificationJson["usePbMode"] = rectification.usePbMode;
    rectificationJson["timpPbMs"] = rectification.timpPbMs;
    rectificationJson["routingSettlingMs"] =
        rectification.routingSettlingMs;
    rectificationJson["routingRetargetMinMs"] =
        rectification.routingRetargetMinMs;
    rectificationJson["valvePulsePeriodMs"] =
        rectification.valvePulsePeriodMs;
    rectificationJson["valvePulseMinOpenMs"] =
        rectification.valvePulseMinOpenMs;
    rectificationJson["valvePulseMaxOpenMs"] =
        rectification.valvePulseMaxOpenMs;

    JsonArray phasePowerPercent =
        rectificationJson["phasePowerPercent"].to<JsonArray>();
    for (uint8_t index = 0; index < RECT_POWER_COUNT; ++index) {
        phasePowerPercent.add(rectification.phasePowerPercent[index]);
    }

    rectificationJson["phasePowerStabilization"] =
        rectification.phasePowerPercent[RECT_POWER_STABILIZATION];
    rectificationJson["phasePowerHeads"] =
        rectification.phasePowerPercent[RECT_POWER_HEADS];
    rectificationJson["phasePowerBody"] =
        rectification.phasePowerPercent[RECT_POWER_BODY];
    rectificationJson["phasePowerTails"] =
        rectification.phasePowerPercent[RECT_POWER_TAILS];
}

void loadRectificationParamsFromJson(JsonVariantConst rectificationVariant,
                                     RectificationParams& rectification) {
    if (!rectificationVariant.is<JsonObjectConst>()) {
        return;
    }

    JsonObjectConst rectificationJson =
        rectificationVariant.as<JsonObjectConst>();
    rectification.stabilizationMin =
        rectificationJson["stabilizationMin"] | rectification.stabilizationMin;
    rectification.headsVolume =
        rectificationJson["headsVolume"] | rectification.headsVolume;
    rectification.bodyVolume =
        rectificationJson["bodyVolume"] | rectification.bodyVolume;
    rectification.tailsVolume =
        rectificationJson["tailsVolume"] | rectification.tailsVolume;
    rectification.headsSpeed =
        rectificationJson["headsSpeed"] | rectification.headsSpeed;
    rectification.bodySpeed =
        rectificationJson["bodySpeed"] | rectification.bodySpeed;
    rectification.tailsSpeed =
        rectificationJson["tailsSpeed"] | rectification.tailsSpeed;
    rectification.purgeMin =
        rectificationJson["purgeMin"] | rectification.purgeMin;
    rectification.baroCorrectionEnabled =
        rectificationJson["baroCorrectionEnabled"] |
        rectification.baroCorrectionEnabled;
    rectification.pressureControlEnabled =
        rectificationJson["pressureControlEnabled"] |
        rectification.pressureControlEnabled;
    rectification.pressureMinPowerPercent = clampProfileU8(
        rectificationJson["pressureMinPowerPercent"] |
            rectification.pressureMinPowerPercent,
        0, 100);
    rectification.refluxMode = static_cast<RectRefluxMode>(
        clampProfileU8(
            rectificationJson["refluxMode"] |
                static_cast<uint8_t>(rectification.refluxMode),
            0,
            static_cast<uint8_t>(RectRefluxMode::AUTONOMOUS)));
    if (!rectificationJson["srTarget"].isNull()) {
        rectification.srTarget =
            clampProfileFloat(rectificationJson["srTarget"].as<float>(),
                              0.0f,
                              20.0f);
    } else if (!rectificationJson["srRatio"].isNull()) {
        rectification.srTarget =
            clampProfileFloat(rectificationJson["srRatio"].as<float>(),
                              0.0f,
                              20.0f);
    }
    rectification.autonomousCycleSec =
        clampProfileU16(rectificationJson["autonomousCycleSec"] |
                            rectification.autonomousCycleSec,
                        1,
                        7200);
    rectification.autonomousPauseSec =
        clampProfileU16(rectificationJson["autonomousPauseSec"] |
                            rectification.autonomousPauseSec,
                        0,
                        7199);
    rectification.chimAutoPercent =
        clampProfileFloat(rectificationJson["chimAutoPercent"] |
                              rectification.chimAutoPercent,
                          0.0f,
                          200.0f);
    rectification.chimTimePerH =
        clampProfileFloat(rectificationJson["chimTimePerH"] |
                              rectification.chimTimePerH,
                          -2000.0f,
                          2000.0f);
    rectification.chimBegPercent =
        clampProfileFloat(rectificationJson["chimBegPercent"] |
                              rectification.chimBegPercent,
                          -100.0f,
                          200.0f);
    rectification.chimMinPercent =
        clampProfileFloat(rectificationJson["chimMinPercent"] |
                              rectification.chimMinPercent,
                          0.0f,
                          100.0f);
    rectification.usePbMode =
        clampProfileU8(rectificationJson["usePbMode"] |
                           rectification.usePbMode,
                       0,
                       3);
    rectification.timpPbMs = std::min<uint32_t>(
        600000UL, rectificationJson["timpPbMs"] | rectification.timpPbMs);
    rectification.routingSettlingMs =
        clampProfileU16(rectificationJson["routingSettlingMs"] |
                            rectification.routingSettlingMs,
                        0,
                        10000);
    rectification.routingRetargetMinMs =
        clampProfileU16(rectificationJson["routingRetargetMinMs"] |
                            rectification.routingRetargetMinMs,
                        0,
                        30000);
    rectification.valvePulsePeriodMs =
        clampProfileU16(rectificationJson["valvePulsePeriodMs"] |
                            rectification.valvePulsePeriodMs,
                        100,
                        5000);
    rectification.valvePulseMinOpenMs =
        clampProfileU16(rectificationJson["valvePulseMinOpenMs"] |
                            rectification.valvePulseMinOpenMs,
                        0,
                        rectification.valvePulsePeriodMs);
    rectification.valvePulseMaxOpenMs =
        clampProfileU16(rectificationJson["valvePulseMaxOpenMs"] |
                            rectification.valvePulseMaxOpenMs,
                        rectification.valvePulseMinOpenMs,
                        rectification.valvePulsePeriodMs);

    rectification.takeoffBackendType = static_cast<RectTakeoffBackendType>(
        clampProfileU8(
            rectificationJson["takeoffBackendType"] |
                static_cast<uint8_t>(RectTakeoffBackendType::PUMP),
            0,
            static_cast<uint8_t>(
                RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)));

    JsonVariantConst phasePowerVariant = rectificationJson["phasePowerPercent"];
    if (phasePowerVariant.is<JsonArrayConst>()) {
        size_t index = 0;
        for (JsonVariantConst value : phasePowerVariant.as<JsonArrayConst>()) {
            if (index >= RECT_POWER_COUNT) {
                break;
            }
            rectification.phasePowerPercent[index] =
                clampProfileU8(value | rectification.phasePowerPercent[index], 1,
                               100);
            index++;
        }
    }
    rectification.phasePowerPercent[RECT_POWER_STABILIZATION] =
        clampProfileU8(
            rectificationJson["phasePowerStabilization"] |
                rectification.phasePowerPercent[RECT_POWER_STABILIZATION],
            1,
            100);
    rectification.phasePowerPercent[RECT_POWER_HEADS] = clampProfileU8(
        rectificationJson["phasePowerHeads"] |
            rectification.phasePowerPercent[RECT_POWER_HEADS],
        1,
        100);
    rectification.phasePowerPercent[RECT_POWER_BODY] = clampProfileU8(
        rectificationJson["phasePowerBody"] |
            rectification.phasePowerPercent[RECT_POWER_BODY],
        1,
        100);
    rectification.phasePowerPercent[RECT_POWER_TAILS] = clampProfileU8(
        rectificationJson["phasePowerTails"] |
            rectification.phasePowerPercent[RECT_POWER_TAILS],
        1,
        100);
    if (rectification.autonomousPauseSec >= rectification.autonomousCycleSec) {
        rectification.autonomousPauseSec =
            rectification.autonomousCycleSec > 0
                ? rectification.autonomousCycleSec - 1
                : 0;
    }
}

void appendDistillationJson(JsonObject parameters,
                            const DistillationParams& distillation) {
    JsonObject distillationJson = parameters["distillation"].to<JsonObject>();
    distillationJson["headsVolume"] = distillation.headsVolume;
    distillationJson["targetVolume"] = distillation.targetVolume;
    distillationJson["tailsVolume"] = distillation.tailsVolume;
    distillationJson["speed"] = distillation.speed;
    distillationJson["endTemp"] = distillation.endTemp;
    distillationJson["takeoffBackendType"] =
        static_cast<uint8_t>(distillation.takeoffBackendType);
    distillationJson["valveSafeVentConfirmed"] =
        distillation.valveSafeVentConfirmed;
    distillationJson["vaporTempControlEnabled"] =
        distillation.vaporTempControlEnabled;
    distillationJson["vaporTempTargetC"] = distillation.vaporTempTargetC;
    distillationJson["vaporTempMinPowerPercent"] =
        distillation.vaporTempMinPowerPercent;
    distillationJson["vaporTempMaxPowerPercent"] =
        distillation.vaporTempMaxPowerPercent;
    distillationJson["vaporTempTimeoutMin"] = distillation.vaporTempTimeoutMin;

    JsonObject fractionProgram =
        distillationJson["fractionProgram"].to<JsonObject>();
    fractionProgram["schemaVersion"] = distillation.fractionProgram.schemaVersion;
    fractionProgram["enabled"] = distillation.fractionProgram.enabled;
    fractionProgram["stepCount"] = distillation.fractionProgram.stepCount;
    fractionProgram["heatingTemperatureSensorIndex"] =
        distillation.fractionProgram.heatingTemperatureSensorIndex;
    fractionProgram["heatingTargetTemperatureC"] =
        distillation.fractionProgram.heatingTargetTemperatureC;

    JsonArray fractionSteps = fractionProgram["steps"].to<JsonArray>();
    for (uint8_t index = 0;
         index < distillation.fractionProgram.stepCount &&
         index < FRACTION_PROGRAM_MAX_STEPS;
         ++index) {
        const FractionProgramStep& source =
            distillation.fractionProgram.steps[index];
        JsonObject item = fractionSteps.add<JsonObject>();
        item["name"] = source.name;
        item["routeIndex"] = source.routeIndex;
        item["pumpRateMlH"] = source.pumpRateMlH;
        item["heaterPowerW"] = source.heaterPowerW;
        item["requireOperatorConfirmation"] =
            source.requireOperatorConfirmation;
        item["confirmationPrompt"] = source.confirmationPrompt;
        item["endConditions"] = source.endConditions;
        item["endVolumeMl"] = source.endVolumeMl;
        item["endDurationSec"] = source.endDurationSec;
        item["temperatureSensorIndex"] = source.temperatureSensorIndex;
        item["endTemperatureC"] = source.endTemperatureC;
        item["allowManualAdvance"] = source.allowManualAdvance;
    }
}

void loadDistillationParamsFromJson(JsonVariantConst distillationVariant,
                                    DistillationParams& distillation) {
    if (!distillationVariant.is<JsonObjectConst>()) {
        return;
    }

    JsonObjectConst distillationJson =
        distillationVariant.as<JsonObjectConst>();
    distillation.headsVolume =
        clampProfileU16(distillationJson["headsVolume"] |
                            distillation.headsVolume,
                        0,
                        10000);
    distillation.targetVolume =
        clampProfileU16(distillationJson["targetVolume"] |
                            distillation.targetVolume,
                        1,
                        50000);
    distillation.tailsVolume =
        clampProfileU16(distillationJson["tailsVolume"] |
                            distillation.tailsVolume,
                        0,
                        50000);
    distillation.speed = clampProfileU16(distillationJson["speed"] |
                                             distillation.speed,
                                         50,
                                         65000);
    distillation.endTemp = clampProfileFloat(distillationJson["endTemp"] |
                                                 distillation.endTemp,
                                             50.0f,
                                             110.0f);
    distillation.takeoffBackendType = static_cast<RectTakeoffBackendType>(
        clampProfileU8(
            distillationJson["takeoffBackendType"] |
                static_cast<uint8_t>(RectTakeoffBackendType::PUMP),
            0,
            static_cast<uint8_t>(
                RectTakeoffBackendType::VALVE_SINGLE_SWITCHED)));
    distillation.valveSafeVentConfirmed =
        distillationJson["valveSafeVentConfirmed"] |
        distillation.valveSafeVentConfirmed;
    distillation.vaporTempControlEnabled =
        distillationJson["vaporTempControlEnabled"] |
        distillation.vaporTempControlEnabled;
    distillation.vaporTempTargetC = clampProfileFloat(
        distillationJson["vaporTempTargetC"] | distillation.vaporTempTargetC,
        20.0f, 110.0f);
    distillation.vaporTempMinPowerPercent = clampProfileU8(
        distillationJson["vaporTempMinPowerPercent"] |
            distillation.vaporTempMinPowerPercent,
        0, 100);
    distillation.vaporTempMaxPowerPercent = clampProfileU8(
        distillationJson["vaporTempMaxPowerPercent"] |
            distillation.vaporTempMaxPowerPercent,
        distillation.vaporTempMinPowerPercent, 100);
    distillation.vaporTempTimeoutMin = clampProfileU16(
        distillationJson["vaporTempTimeoutMin"] | distillation.vaporTempTimeoutMin,
        0, 720);

    JsonVariantConst programVariant = distillationJson["fractionProgram"];
    if (!programVariant.is<JsonObjectConst>()) {
        return;
    }

    JsonObjectConst programJson = programVariant.as<JsonObjectConst>();
    FractionProgram program = distillation.fractionProgram;
    program.schemaVersion =
        clampProfileU16(programJson["schemaVersion"] | program.schemaVersion,
                        0,
                        65535);
    program.enabled = programJson["enabled"] | program.enabled;
    program.heatingTemperatureSensorIndex = clampProfileU8(
        programJson["heatingTemperatureSensorIndex"] |
            program.heatingTemperatureSensorIndex,
        0,
        TEMP_COUNT - 1);
    program.heatingTargetTemperatureC = clampProfileFloat(
        programJson["heatingTargetTemperatureC"] |
            program.heatingTargetTemperatureC,
        0.0f,
        110.0f);
    program.stepCount = 0;

    JsonVariantConst stepsVariant = programJson["steps"];
    if (stepsVariant.is<JsonArrayConst>()) {
        for (JsonObjectConst item : stepsVariant.as<JsonArrayConst>()) {
            if (program.stepCount >= FRACTION_PROGRAM_MAX_STEPS) {
                break;
            }
            FractionProgramStep& step = program.steps[program.stepCount++];
            strlcpy(step.name,
                    item["name"] | step.name,
                    sizeof(step.name));
            step.routeIndex =
                clampProfileU8(item["routeIndex"] | step.routeIndex, 0, 4);
            step.pumpRateMlH =
                clampProfileFloat(item["pumpRateMlH"] | step.pumpRateMlH,
                                  0.0f,
                                  65000.0f);
            step.heaterPowerW =
                clampProfileU16(item["heaterPowerW"] | step.heaterPowerW,
                                0,
                                10000);
            step.requireOperatorConfirmation =
                item["requireOperatorConfirmation"] |
                step.requireOperatorConfirmation;
            strlcpy(step.confirmationPrompt,
                    item["confirmationPrompt"] | step.confirmationPrompt,
                    sizeof(step.confirmationPrompt));
            step.endConditions =
                clampProfileU8(item["endConditions"] | step.endConditions,
                               0,
                               255);
            step.endVolumeMl =
                clampProfileFloat(item["endVolumeMl"] | step.endVolumeMl,
                                  0.0f,
                                  50000.0f);
            step.endDurationSec = std::min<uint32_t>(
                864000UL, item["endDurationSec"] | step.endDurationSec);
            step.temperatureSensorIndex = clampProfileU8(
                item["temperatureSensorIndex"] | step.temperatureSensorIndex,
                0,
                TEMP_COUNT - 1);
            step.endTemperatureC =
                clampProfileFloat(item["endTemperatureC"] |
                                      step.endTemperatureC,
                                  0.0f,
                                  110.0f);
            step.allowManualAdvance =
                item["allowManualAdvance"] | step.allowManualAdvance;
        }
    }
    program.stepCount =
        clampProfileU8(programJson["stepCount"] | program.stepCount,
                       0,
                       program.stepCount);
    distillation.fractionProgram = program;
}

void applyRectificationSettings(const RectificationParams& rectification) {
    g_settings.rectParams.stabilizationMin = rectification.stabilizationMin;
    g_settings.rectParams.purgeMin = rectification.purgeMin;
    g_settings.rectParams.headsSpeedMlHKw = rectification.headsSpeed;
    g_settings.rectParams.bodySpeedMlHKw = rectification.bodySpeed;
    g_settings.rectParams.baroCorrectionEnabled =
        rectification.baroCorrectionEnabled;
    g_settings.rectParams.pressureControlEnabled =
        rectification.pressureControlEnabled;
    g_settings.rectParams.pressureMinPowerPercent =
        rectification.pressureMinPowerPercent;
    g_settings.rectParams.takeoffBackendType = rectification.takeoffBackendType;
    g_settings.rectParams.refluxMode = rectification.refluxMode;
    g_settings.rectParams.srRatio = rectification.srTarget;
    g_settings.rectParams.autonomousCycleSec =
        rectification.autonomousCycleSec;
    g_settings.rectParams.autonomousPauseSec =
        rectification.autonomousPauseSec;
    g_settings.rectParams.chimAutoPercent = rectification.chimAutoPercent;
    g_settings.rectParams.chimTimePerH = rectification.chimTimePerH;
    g_settings.rectParams.chimBegPercent = rectification.chimBegPercent;
    g_settings.rectParams.chimMinPercent = rectification.chimMinPercent;
    g_settings.rectParams.usePbMode = rectification.usePbMode;
    g_settings.rectParams.timpPbMs = rectification.timpPbMs;
    g_settings.rectParams.routingSettlingMs =
        rectification.routingSettlingMs;
    g_settings.rectParams.routingRetargetMinMs =
        rectification.routingRetargetMinMs;
    g_settings.rectParams.valvePulsePeriodMs =
        rectification.valvePulsePeriodMs;
    g_settings.rectParams.valvePulseMinOpenMs =
        rectification.valvePulseMinOpenMs;
    g_settings.rectParams.valvePulseMaxOpenMs =
        rectification.valvePulseMaxOpenMs;
    for (uint8_t index = 0; index < RECT_POWER_COUNT; ++index) {
        g_settings.rectParams.phasePowerPercent[index] =
            rectification.phasePowerPercent[index];
    }
}

void captureRectificationSettings(RectificationParams& rectification) {
    rectification.stabilizationMin = g_settings.rectParams.stabilizationMin;
    rectification.purgeMin = g_settings.rectParams.purgeMin;
    rectification.headsSpeed = g_settings.rectParams.headsSpeedMlHKw;
    rectification.bodySpeed = g_settings.rectParams.bodySpeedMlHKw;
    rectification.baroCorrectionEnabled =
        g_settings.rectParams.baroCorrectionEnabled;
    rectification.pressureControlEnabled =
        g_settings.rectParams.pressureControlEnabled;
    rectification.pressureMinPowerPercent =
        g_settings.rectParams.pressureMinPowerPercent;
    rectification.takeoffBackendType = g_settings.rectParams.takeoffBackendType;
    rectification.refluxMode = g_settings.rectParams.refluxMode;
    rectification.srTarget = g_settings.rectParams.srRatio;
    rectification.autonomousCycleSec =
        g_settings.rectParams.autonomousCycleSec;
    rectification.autonomousPauseSec =
        g_settings.rectParams.autonomousPauseSec;
    rectification.chimAutoPercent = g_settings.rectParams.chimAutoPercent;
    rectification.chimTimePerH = g_settings.rectParams.chimTimePerH;
    rectification.chimBegPercent = g_settings.rectParams.chimBegPercent;
    rectification.chimMinPercent = g_settings.rectParams.chimMinPercent;
    rectification.usePbMode = g_settings.rectParams.usePbMode;
    rectification.timpPbMs = g_settings.rectParams.timpPbMs;
    rectification.routingSettlingMs =
        g_settings.rectParams.routingSettlingMs;
    rectification.routingRetargetMinMs =
        g_settings.rectParams.routingRetargetMinMs;
    rectification.valvePulsePeriodMs =
        g_settings.rectParams.valvePulsePeriodMs;
    rectification.valvePulseMinOpenMs =
        g_settings.rectParams.valvePulseMinOpenMs;
    rectification.valvePulseMaxOpenMs =
        g_settings.rectParams.valvePulseMaxOpenMs;
    for (uint8_t index = 0; index < RECT_POWER_COUNT; ++index) {
        rectification.phasePowerPercent[index] =
            g_settings.rectParams.phasePowerPercent[index];
    }
}

void applyDistillationSettings(const DistillationParams& distillation) {
    g_settings.distillationUi.headsVolumeMl = distillation.headsVolume;
    g_settings.distillationUi.targetVolumeMl = distillation.targetVolume;
    g_settings.distillationUi.tailsVolumeMl = distillation.tailsVolume;
    g_settings.distillationUi.speedMlH = distillation.speed;
    g_settings.distillationUi.endTempC = distillation.endTemp;
    g_settings.distillationUi.takeoffBackendType =
        distillation.takeoffBackendType;
    g_settings.distillationUi.valveSafeVentConfirmed =
        distillation.valveSafeVentConfirmed;
    g_settings.distillationUi.vaporTempControlEnabled =
        distillation.vaporTempControlEnabled;
    g_settings.distillationUi.vaporTempTargetC = distillation.vaporTempTargetC;
    g_settings.distillationUi.vaporTempMinPowerPercent =
        distillation.vaporTempMinPowerPercent;
    g_settings.distillationUi.vaporTempMaxPowerPercent =
        distillation.vaporTempMaxPowerPercent;
    g_settings.distillationUi.vaporTempTimeoutMin =
        distillation.vaporTempTimeoutMin;
    g_settings.fractionProgram = distillation.fractionProgram;
}

void captureDistillationSettings(DistillationParams& distillation) {
    distillation.headsVolume =
        static_cast<uint16_t>(g_settings.distillationUi.headsVolumeMl);
    distillation.targetVolume =
        static_cast<uint16_t>(g_settings.distillationUi.targetVolumeMl);
    distillation.tailsVolume =
        static_cast<uint16_t>(g_settings.distillationUi.tailsVolumeMl);
    distillation.speed =
        static_cast<uint16_t>(g_settings.distillationUi.speedMlH);
    distillation.endTemp = g_settings.distillationUi.endTempC;
    distillation.takeoffBackendType =
        g_settings.distillationUi.takeoffBackendType;
    distillation.valveSafeVentConfirmed =
        g_settings.distillationUi.valveSafeVentConfirmed;
    distillation.vaporTempControlEnabled =
        g_settings.distillationUi.vaporTempControlEnabled;
    distillation.vaporTempTargetC = g_settings.distillationUi.vaporTempTargetC;
    distillation.vaporTempMinPowerPercent =
        g_settings.distillationUi.vaporTempMinPowerPercent;
    distillation.vaporTempMaxPowerPercent =
        g_settings.distillationUi.vaporTempMaxPowerPercent;
    distillation.vaporTempTimeoutMin =
        g_settings.distillationUi.vaporTempTimeoutMin;
    distillation.fractionProgram = g_settings.fractionProgram;
}

void loadMashingParamsFromJson(JsonVariantConst stepsVariant,
                               MashingParams& mashing) {
    mashing.steps.clear();
    if (!stepsVariant.is<JsonArrayConst>()) {
        return;
    }

    size_t index = 0;
    for (JsonObjectConst step : stepsVariant.as<JsonArrayConst>()) {
        if (index >= PROFILE_MASHING_MAX_STEPS) {
            break;
        }

        const float temperature =
            clampMashTemperature(step["temperature"] | 0.0f);
        const uint16_t duration =
            clampMashDuration(step["duration"] | 0);
        if (temperature <= 0.0f || duration == 0) {
            index++;
            continue;
        }

        MashingStepParams item;
        item.type = mashStepTypeFromString(step["type"] | "heat_hold");
        item.temperature = temperature;
        item.duration = duration;
        item.name = normalizeMashStepName(step["name"].as<String>(), index);
        mashing.steps.push_back(item);
        index++;
    }
}

void fillDefaultMashingSteps(MashingParams& mashing) {
    mashing.steps.clear();

    const struct {
        float temperature;
        uint16_t duration;
        const char* name;
    } defaults[] = {
        {38.0f, 20, "Кислотная пауза"},
        {52.0f, 20, "Белковая пауза"},
        {63.0f, 40, "Мальтозная пауза"},
        {72.0f, 20, "Осахаривание"},
        {78.0f, 10, "Мэш-аут"},
    };

    for (size_t index = 0; index < (sizeof(defaults) / sizeof(defaults[0])); index++) {
        MashingStepParams item;
        item.temperature = defaults[index].temperature;
        item.duration = defaults[index].duration;
        item.name = defaults[index].name;
        mashing.steps.push_back(item);
    }
}

float getCurrentAtmosphereMmHg() {
    const float atmosphereHpa = g_state.pressure.atmosphere;
    if (atmosphereHpa < 850.0f || atmosphereHpa > 1100.0f) {
        return 0.0f;
    }
    return hpaToMmHg(atmosphereHpa);
}

String buildValidationEquipmentSnapshotJson() {
    JsonDocument doc;
    doc["heaterPowerW"] = g_settings.equipment.heaterPowerW;
    doc["columnHeightMm"] = g_settings.equipment.columnHeightMm;
    doc["cubeVolumeL"] = g_settings.equipment.cubeVolumeL;
    doc["minHeaterSubmergeL"] = g_settings.equipment.minHeaterSubmergeL;
    doc["waterAutoStartCubeTempC"] = g_settings.equipment.waterAutoStartCubeTempC;
    doc["boosterHeaterEnabled"] = g_settings.equipment.boosterHeaterEnabled;
    doc["boosterHeaterPowerW"] = g_settings.equipment.boosterHeaterPowerW;
    doc["boosterHeaterStopCubeTempC"] =
        g_settings.equipment.boosterHeaterStopCubeTempC;
    doc["coolingPwmEnabled"] = g_settings.equipment.coolingPwmEnabled;
    doc["coolingPwmMinDuty"] = g_settings.equipment.coolingPwmMinDuty;
    doc["coolingPwmMaxDuty"] = g_settings.equipment.coolingPwmMaxDuty;
    doc["coolingPwmStartupDuty"] = g_settings.equipment.coolingPwmStartupDuty;
    doc["useDs2482ForTemps"] = g_settings.equipment.useDs2482ForTemps;
    doc["ds2482Address"] = g_settings.equipment.ds2482Address;
    doc["tempBusGpioPin"] = PIN_ONEWIRE;
    doc["temperatureBusSource"] = Sensors::getTemperatureBusSourceKey();
    doc["temperatureBusSourceLabel"] = Sensors::getTemperatureBusSourceLabel();
    doc["bodyLevelSensorEnabled"] = g_settings.equipment.bodyLevelSensorEnabled;
    doc["bodyLevelThresholdV"] = g_settings.equipment.bodyLevelThresholdV;
    doc["bodyLevelTriggerAbove"] = g_settings.equipment.bodyLevelTriggerAbove;
    doc["leakSensorEnabled"] = g_settings.equipment.leakSensorEnabled;
    doc["leakThresholdV"] = g_settings.equipment.leakThresholdV;
    doc["leakTriggerAbove"] = g_settings.equipment.leakTriggerAbove;
    doc["packingType"] = packingTypeToString(g_settings.equipment.packingType);
    doc["packingCoeff"] = g_settings.equipment.packingCoeff;

    JsonObject boardProfile = doc["boardProfile"].to<JsonObject>();
    boardProfile["rev"] = BOARD_REV_LABEL;
    boardProfile["name"] = BOARD_PROFILE_NAME;
    boardProfile["code"] = BOARD_REV;

    JsonObject temperatureTopology = doc["temperatureTopology"].to<JsonObject>();
    fillTemperatureTopologyJson(temperatureTopology, g_settings.equipment);

    JsonObject supportedModes = doc["supportedModes"].to<JsonObject>();
    fillTemperatureModeSupportJson(supportedModes, g_settings);

    JsonObject modules = doc["modules"].to<JsonObject>();
    fillEquipmentModulesJson(modules);

    JsonObject safetyChannels = doc["safetyChannels"].to<JsonObject>();
    fillSafetyChannelsJson(safetyChannels);

    String json;
    serializeJson(doc, json);
    return json;
}

ProfileValidationSnapshot buildValidationSnapshot(const ProcessHistory& history) {
    ProfileValidationSnapshot snapshot;
    snapshot.validatedAt = history.metadata.endTime > 0
                               ? history.metadata.endTime
                               : history.metadata.startTime;
    snapshot.sourceProcessId = history.id;
    snapshot.atmosphereHpa = g_state.pressure.atmosphere;
    snapshot.atmosphereMmHg = hpaToMmHg(snapshot.atmosphereHpa);
    snapshot.columnHeightMm = g_settings.equipment.columnHeightMm;
    snapshot.packingType = packingTypeToString(g_settings.equipment.packingType);
    snapshot.packingCoeff = g_settings.equipment.packingCoeff;
    snapshot.heaterPowerW = g_settings.equipment.heaterPowerW;
    snapshot.targetPowerW = history.parameters.targetPower;
    snapshot.feedVolumeL = g_settings.rectParams.feedVolumeL;
    snapshot.feedAbvPercent = g_settings.rectParams.feedAbvPercent;
    if (g_settings.equipment.cubeVolumeL > 0.0f) {
        snapshot.cubeChargePercent =
            (snapshot.feedVolumeL / g_settings.equipment.cubeVolumeL) * 100.0f;
    }
    snapshot.headsActualMl = history.results.headsCollected;
    snapshot.bodyActualMl = history.results.bodyCollected;
    snapshot.tailsActualMl = history.results.tailsCollected;

    const ProcessPhase* headsPhase = findPhase(history, "heads");
    const ProcessPhase* bodyPhase = findPhase(history, "body");
    const ProcessPhase* tailsPhase = findPhase(history, "tails");
    snapshot.headsCutColumnTopC = headsPhase ? headsPhase->endTemp : 0.0f;
    snapshot.bodyCutColumnTopC = bodyPhase ? bodyPhase->endTemp : 0.0f;
    snapshot.tailsCutColumnTopC = tailsPhase ? tailsPhase->endTemp : 0.0f;
    snapshot.cubeFinalC = history.metrics.cube.final;
    snapshot.columnTopFinalC = history.metrics.columnTop.final;
    snapshot.avgStabilityIndex = history.metrics.avgStabilityIndex;
    snapshot.avgProcessHealth = history.metrics.avgProcessHealth;
    snapshot.equipmentSnapshotJson = buildValidationEquipmentSnapshotJson();
    return snapshot;
}

void appendLearningJson(JsonObject learning, const ProfileLearningSnapshot& snapshot) {
    learning["successfulRuns"] = snapshot.successfulRuns;
    learning["failedRuns"] = snapshot.failedRuns;
    learning["avgEnergyUsed"] = snapshot.avgEnergyUsed;
    learning["avgEnergyPerLiter"] = snapshot.avgEnergyPerLiter;
    learning["avgProcessHealth"] = snapshot.avgProcessHealth;
    learning["avgStabilityIndex"] = snapshot.avgStabilityIndex;
    learning["typicalCubeFinalTemp"] = snapshot.typicalCubeFinalTemp;
    learning["typicalColumnTopFinalTemp"] = snapshot.typicalColumnTopFinalTemp;
    learning["lastProcessId"] = snapshot.lastProcessId;
    learning["lastSuccessfulProcessId"] = snapshot.lastSuccessfulProcessId;
}

void appendValidationJson(JsonObject validation,
                          const ProfileValidationSnapshot& snapshot) {
    validation["validatedAt"] = snapshot.validatedAt;
    validation["sourceProcessId"] = snapshot.sourceProcessId;
    validation["atmosphereHpa"] = snapshot.atmosphereHpa;
    validation["atmosphereMmHg"] = snapshot.atmosphereMmHg;
    validation["columnHeightMm"] = snapshot.columnHeightMm;
    validation["packingType"] = snapshot.packingType;
    validation["packingCoeff"] = snapshot.packingCoeff;
    validation["heaterPowerW"] = snapshot.heaterPowerW;
    validation["targetPowerW"] = snapshot.targetPowerW;
    validation["feedVolumeL"] = snapshot.feedVolumeL;
    validation["feedAbvPercent"] = snapshot.feedAbvPercent;
    validation["cubeChargePercent"] = snapshot.cubeChargePercent;
    validation["headsActualMl"] = snapshot.headsActualMl;
    validation["bodyActualMl"] = snapshot.bodyActualMl;
    validation["tailsActualMl"] = snapshot.tailsActualMl;
    validation["headsCutColumnTopC"] = snapshot.headsCutColumnTopC;
    validation["bodyCutColumnTopC"] = snapshot.bodyCutColumnTopC;
    validation["tailsCutColumnTopC"] = snapshot.tailsCutColumnTopC;
    validation["cubeFinalC"] = snapshot.cubeFinalC;
    validation["columnTopFinalC"] = snapshot.columnTopFinalC;
    validation["avgStabilityIndex"] = snapshot.avgStabilityIndex;
    validation["avgProcessHealth"] = snapshot.avgProcessHealth;
    validation["equipmentSnapshotJson"] = snapshot.equipmentSnapshotJson;
}

void appendStoredProfileValidationJson(JsonObject validation, const Profile& profile) {
    appendValidationJson(validation, profile.validation);
}

} // namespace

void appendProfileValidationJson(JsonObject validation, const Profile& profile) {
    appendStoredProfileValidationJson(validation, profile);
}

ProfileBaroCorrectionSummary evaluateProfileBaroCorrection(
    const Profile& profile,
    int enabledOverride) {
    ProfileBaroCorrectionSummary summary;
    summary.enabled = enabledOverride >= 0
                          ? (enabledOverride != 0)
                          : g_settings.rectParams.baroCorrectionEnabled;
    summary.strength = PROFILE_BARO_CORRECTION_STRENGTH;
    summary.maxShiftC = PROFILE_BARO_CORRECTION_MAX_SHIFT_C;
    summary.baselinePressureMmHg = profile.validation.atmosphereMmHg;
    summary.currentPressureMmHg = getCurrentAtmosphereMmHg();

    if (!summary.enabled) {
        summary.note = "Барокоррекция отключена в настройках.";
        return summary;
    }

    if (summary.baselinePressureMmHg <= 0.0f) {
        summary.note = "Для профиля ещё нет валидного baseline по атмосферному давлению.";
        return summary;
    }

    if (summary.currentPressureMmHg <= 0.0f) {
        summary.note = "Нет актуального атмосферного давления от BMP280.";
        return summary;
    }

    summary.applicable = true;
    summary.pressureDeltaMmHg =
        summary.currentPressureMmHg - summary.baselinePressureMmHg;
    summary.boilingShiftC = summary.pressureDeltaMmHg * 0.037f;

    const float softShift = summary.boilingShiftC * summary.strength;
    if (softShift < -summary.maxShiftC) {
        summary.appliedShiftC = -summary.maxShiftC;
    } else if (softShift > summary.maxShiftC) {
        summary.appliedShiftC = summary.maxShiftC;
    } else {
        summary.appliedShiftC = softShift;
    }
    summary.applied = std::fabs(summary.appliedShiftC) >= 0.02f;

    if (!summary.applied) {
        summary.note = "Отклонение давления есть, но для мягкой коррекции оно слишком мало.";
        return summary;
    }

    summary.note = "Температурные пороги профиля мягко сдвинуты относительно baseline.";
    return summary;
}

TemperatureParams getEffectiveProfileTemperatures(
    const Profile& profile,
    ProfileBaroCorrectionSummary* summary,
    int enabledOverride) {
    ProfileBaroCorrectionSummary localSummary =
        evaluateProfileBaroCorrection(profile, enabledOverride);
    if (summary) {
        *summary = localSummary;
    }

    TemperatureParams effective = profile.parameters.temperatures;
    if (!localSummary.applied) {
        return effective;
    }

    effective.maxColumn += localSummary.appliedShiftC;
    effective.headsEnd += localSummary.appliedShiftC;
    effective.bodyStart += localSummary.appliedShiftC;
    effective.bodyEnd += localSummary.appliedShiftC;
    return effective;
}

// ============================================================================
// Инициализация системы профилей
// ============================================================================

bool initProfiles() {
    Serial.println("Инициализация системы профилей...");

    // Проверить, смонтирована ли LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("Ошибка: не удалось инициализировать LittleFS");
        return false;
    }

    // Проверить существование директории /profiles
    if (!LittleFS.exists(PROFILES_DIR)) {
        Serial.println("Создание директории /profiles");
        if (!LittleFS.mkdir(PROFILES_DIR)) {
            Serial.println("Ошибка: не удалось создать директорию /profiles");
            return false;
        }
    }

    // Загрузить встроенные рецепты если их нет
    if (getProfileCount() == 0) {
        Serial.println("Загрузка встроенных рецептов...");
        loadBuiltinProfiles();
    }

    // Провести ротацию профилей
    rotateProfiles();

    Serial.println("Система профилей инициализирована");
    Serial.printf("Профилей: %d\n", getProfileCount());

    return true;
}

// ============================================================================
// Сохранение профиля
// ============================================================================

bool saveProfile(const Profile& profile) {
    // Валидация
    if (!validateProfile(profile)) {
        Serial.println("Ошибка: профиль не прошел валидацию");
        return false;
    }

    String filename = String(PROFILES_DIR) + "/profile_" + profile.id + ".json";
    Serial.printf("Сохранение профиля: %s\n", filename.c_str());

    File file = LittleFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("Ошибка: не удалось создать файл профиля");
        return false;
    }

    // Создать JSON документ
    JsonDocument doc;

    doc["id"] = profile.id;

    // Метаданные
    JsonObject metadata = doc["metadata"].to<JsonObject>();
    metadata["name"] = profile.metadata.name;
    metadata["description"] = profile.metadata.description;
    metadata["category"] = profile.metadata.category;

    JsonArray tags = metadata["tags"].to<JsonArray>();
    for (const auto& tag : profile.metadata.tags) {
        tags.add(tag);
    }

    metadata["created"] = profile.metadata.created;
    metadata["updated"] = profile.metadata.updated;
    metadata["author"] = profile.metadata.author;
    metadata["isBuiltin"] = profile.metadata.isBuiltin;

    // Параметры
    JsonObject parameters = doc["parameters"].to<JsonObject>();
    parameters["mode"] = profile.parameters.mode;
    parameters["model"] = profile.parameters.model;

    // Нагреватель
    JsonObject heater = parameters["heater"].to<JsonObject>();
    heater["maxPower"] = profile.parameters.heater.maxPower;
    heater["autoMode"] = profile.parameters.heater.autoMode;
    heater["pidKp"] = profile.parameters.heater.pidKp;
    heater["pidKi"] = profile.parameters.heater.pidKi;
    heater["pidKd"] = profile.parameters.heater.pidKd;
    heater["boosterEnabled"] = profile.parameters.heater.boosterEnabled;
    heater["boosterStopCubeTempC"] =
        profile.parameters.heater.boosterStopCubeTempC;

    // Ректификация
    appendRectificationJson(parameters, profile.parameters.rectification);

    // Дистилляция
    appendDistillationJson(parameters, profile.parameters.distillation);
    appendMashingJson(parameters, profile.parameters.mashing);

    // Температуры
    JsonObject temperatures = parameters["temperatures"].to<JsonObject>();
    temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
    temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
    temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
    temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
    temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

    // Безопасность
    JsonObject safety = parameters["safety"].to<JsonObject>();
    safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
    safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
    safety["pressureMax"] = profile.parameters.safety.pressureMax;

    // Статистика
    JsonObject statistics = doc["statistics"].to<JsonObject>();
    statistics["useCount"] = profile.statistics.useCount;
    statistics["lastUsed"] = profile.statistics.lastUsed;
    statistics["avgDuration"] = profile.statistics.avgDuration;
    statistics["avgYield"] = profile.statistics.avgYield;
    statistics["successRate"] = profile.statistics.successRate;
    appendLearningJson(doc["learning"].to<JsonObject>(), profile.learning);
    appendProfileValidationJson(doc["validation"].to<JsonObject>(), profile);

    // Сериализовать в файл
    if (serializeJson(doc, file) == 0) {
        Serial.println("Ошибка: не удалось записать JSON");
        file.close();
        return false;
    }

    file.close();
    Serial.printf("Профиль сохранён (%d байт)\n", file.size());

    // Провести ротацию
    rotateProfiles();

    return true;
}

// ============================================================================
// Загрузка профиля
// ============================================================================

bool loadProfile(const String& id, Profile& profile) {
    String filename = String(PROFILES_DIR) + "/profile_" + id + ".json";

    if (!LittleFS.exists(filename)) {
        Serial.printf("Ошибка: файл не найден: %s\n", filename.c_str());
        return false;
    }

    File file = LittleFS.open(filename, FILE_READ);
    if (!file) {
        Serial.println("Ошибка: не удалось открыть файл");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("Ошибка парсинга JSON: %s\n", error.c_str());
        return false;
    }

    // Загрузить данные из JSON
    profile.learning = ProfileLearningSnapshot();
    profile.validation = ProfileValidationSnapshot();
    profile.id = doc["id"].as<String>();

    // Метаданные
    profile.metadata.name = doc["metadata"]["name"].as<String>();
    profile.metadata.description = doc["metadata"]["description"].as<String>();
    profile.metadata.category = doc["metadata"]["category"].as<String>();

    profile.metadata.tags.clear();
    JsonArray tags = doc["metadata"]["tags"];
    for (JsonVariant tag : tags) {
        profile.metadata.tags.push_back(tag.as<String>());
    }

    profile.metadata.created = doc["metadata"]["created"];
    profile.metadata.updated = doc["metadata"]["updated"];
    profile.metadata.author = doc["metadata"]["author"].as<String>();
    profile.metadata.isBuiltin = doc["metadata"]["isBuiltin"];
    
    // ... (остальной код загрузки полей остается прежним, так как as<T>() и [] работают в обеих версиях)


    // Параметры
    profile.parameters.mode = doc["parameters"]["mode"].as<String>();
    profile.parameters.model = doc["parameters"]["model"].as<String>();

    // Нагреватель
    profile.parameters.heater.maxPower = doc["parameters"]["heater"]["maxPower"];
    profile.parameters.heater.autoMode = doc["parameters"]["heater"]["autoMode"];
    profile.parameters.heater.pidKp = doc["parameters"]["heater"]["pidKp"];
    profile.parameters.heater.pidKi = doc["parameters"]["heater"]["pidKi"];
    profile.parameters.heater.pidKd = doc["parameters"]["heater"]["pidKd"];
    profile.parameters.heater.boosterEnabled =
        doc["parameters"]["heater"]["boosterEnabled"] |
        g_settings.equipment.boosterHeaterEnabled;
    profile.parameters.heater.boosterStopCubeTempC =
        doc["parameters"]["heater"]["boosterStopCubeTempC"] |
        g_settings.equipment.boosterHeaterStopCubeTempC;

    // Ректификация
    loadRectificationParamsFromJson(doc["parameters"]["rectification"],
                                    profile.parameters.rectification);

    // Дистилляция
    loadDistillationParamsFromJson(doc["parameters"]["distillation"],
                                   profile.parameters.distillation);
    loadMashingParamsFromJson(doc["parameters"]["mashing"]["steps"],
                              profile.parameters.mashing);

    // Температуры
    profile.parameters.temperatures.maxCube = doc["parameters"]["temperatures"]["maxCube"];
    profile.parameters.temperatures.maxColumn = doc["parameters"]["temperatures"]["maxColumn"];
    profile.parameters.temperatures.headsEnd = doc["parameters"]["temperatures"]["headsEnd"];
    profile.parameters.temperatures.bodyStart = doc["parameters"]["temperatures"]["bodyStart"];
    profile.parameters.temperatures.bodyEnd = doc["parameters"]["temperatures"]["bodyEnd"];

    // Безопасность
    profile.parameters.safety.maxRuntime = doc["parameters"]["safety"]["maxRuntime"];
    profile.parameters.safety.waterFlowMin = doc["parameters"]["safety"]["waterFlowMin"];
    profile.parameters.safety.pressureMax = doc["parameters"]["safety"]["pressureMax"];

    // Статистика
    profile.statistics.useCount = doc["statistics"]["useCount"];
    profile.statistics.lastUsed = doc["statistics"]["lastUsed"];
    profile.statistics.avgDuration = doc["statistics"]["avgDuration"];
    profile.statistics.avgYield = doc["statistics"]["avgYield"];
    profile.statistics.successRate = doc["statistics"]["successRate"];
    profile.learning.successfulRuns = doc["learning"]["successfulRuns"] | 0;
    profile.learning.failedRuns = doc["learning"]["failedRuns"] | 0;
    profile.learning.avgEnergyUsed = doc["learning"]["avgEnergyUsed"] | 0.0f;
    profile.learning.avgEnergyPerLiter = doc["learning"]["avgEnergyPerLiter"] | 0.0f;
    profile.learning.avgProcessHealth = doc["learning"]["avgProcessHealth"] | 0.0f;
    profile.learning.avgStabilityIndex = doc["learning"]["avgStabilityIndex"] | 0.0f;
    profile.learning.typicalCubeFinalTemp =
        doc["learning"]["typicalCubeFinalTemp"] | 0.0f;
    profile.learning.typicalColumnTopFinalTemp =
        doc["learning"]["typicalColumnTopFinalTemp"] | 0.0f;
    profile.learning.lastProcessId = doc["learning"]["lastProcessId"].as<String>();
    profile.learning.lastSuccessfulProcessId =
        doc["learning"]["lastSuccessfulProcessId"].as<String>();
    profile.validation.validatedAt = doc["validation"]["validatedAt"] | 0;
    profile.validation.sourceProcessId =
        doc["validation"]["sourceProcessId"].as<String>();
    profile.validation.atmosphereHpa =
        doc["validation"]["atmosphereHpa"] | 0.0f;
    profile.validation.atmosphereMmHg =
        doc["validation"]["atmosphereMmHg"] | 0.0f;
    profile.validation.columnHeightMm =
        doc["validation"]["columnHeightMm"] | 0;
    profile.validation.packingType =
        doc["validation"]["packingType"].as<String>();
    profile.validation.packingCoeff =
        doc["validation"]["packingCoeff"] | 0.0f;
    profile.validation.heaterPowerW =
        doc["validation"]["heaterPowerW"] | 0;
    profile.validation.targetPowerW =
        doc["validation"]["targetPowerW"] | 0;
    profile.validation.feedVolumeL =
        doc["validation"]["feedVolumeL"] | 0.0f;
    profile.validation.feedAbvPercent =
        doc["validation"]["feedAbvPercent"] | 0.0f;
    profile.validation.cubeChargePercent =
        doc["validation"]["cubeChargePercent"] | 0.0f;
    profile.validation.headsActualMl =
        doc["validation"]["headsActualMl"] | 0;
    profile.validation.bodyActualMl =
        doc["validation"]["bodyActualMl"] | 0;
    profile.validation.tailsActualMl =
        doc["validation"]["tailsActualMl"] | 0;
    profile.validation.headsCutColumnTopC =
        doc["validation"]["headsCutColumnTopC"] | 0.0f;
    profile.validation.bodyCutColumnTopC =
        doc["validation"]["bodyCutColumnTopC"] | 0.0f;
    profile.validation.tailsCutColumnTopC =
        doc["validation"]["tailsCutColumnTopC"] | 0.0f;
    profile.validation.cubeFinalC =
        doc["validation"]["cubeFinalC"] | 0.0f;
    profile.validation.columnTopFinalC =
        doc["validation"]["columnTopFinalC"] | 0.0f;
    profile.validation.avgStabilityIndex =
        doc["validation"]["avgStabilityIndex"] | 0.0f;
    profile.validation.avgProcessHealth =
        doc["validation"]["avgProcessHealth"] | 0.0f;
    profile.validation.equipmentSnapshotJson =
        doc["validation"]["equipmentSnapshotJson"] | "";

    Serial.printf("Профиль загружен: %s\n", profile.metadata.name.c_str());
    return true;
}

// ============================================================================
// Получение списка профилей
// ============================================================================

std::vector<ProfileListItem> getProfileList() {
    std::vector<ProfileListItem> list;

    File root = LittleFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return list;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();

            // Проверить, что это файл профиля
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                // Быстрая загрузка только необходимых полей
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, file);

                if (!error) {
                    ProfileListItem item;
                    item.id = doc["id"].as<String>();
                    item.name = doc["metadata"]["name"].as<String>();
                    item.description = doc["metadata"]["description"].as<String>();
                    item.category = doc["metadata"]["category"].as<String>();
                    item.tags.clear();
                    if (doc["metadata"]["tags"].is<JsonArray>()) {
                        for (JsonVariant tag : doc["metadata"]["tags"].as<JsonArray>()) {
                            const String value = tag.as<String>();
                            if (!value.isEmpty()) {
                                item.tags.push_back(value);
                            }
                        }
                    }
                    item.author = doc["metadata"]["author"].as<String>();
                    item.useCount = doc["statistics"]["useCount"];
                    item.lastUsed = doc["statistics"]["lastUsed"];
                    item.updated = doc["metadata"]["updated"] | 0;
                    item.successRate = doc["statistics"]["successRate"] | 0.0f;
                    item.successfulRuns = doc["learning"]["successfulRuns"] | 0;
                    item.isBuiltin = doc["metadata"]["isBuiltin"];

                    list.push_back(item);
                }
            }
        }
        file = root.openNextFile();
    }

    // Сортировать: встроенные первые, затем по частоте использования
    std::sort(list.begin(), list.end(), [](const ProfileListItem& a, const ProfileListItem& b) {
        if (a.isBuiltin != b.isBuiltin) return a.isBuiltin;
        return a.useCount > b.useCount;
    });

    return list;
}

// ============================================================================
// Удаление профиля
// ============================================================================

bool deleteProfile(const String& id) {
    // Сначала загрузить профиль чтобы проверить isBuiltin
    Profile profile;
    if (loadProfile(id, profile)) {
        if (profile.metadata.isBuiltin) {
            Serial.println("Ошибка: нельзя удалить встроенный рецепт");
            return false;
        }
    }

    String filename = String(PROFILES_DIR) + "/profile_" + id + ".json";

    if (!LittleFS.exists(filename)) {
        Serial.printf("Файл не найден: %s\n", filename.c_str());
        return false;
    }

    if (LittleFS.remove(filename)) {
        Serial.printf("Профиль удалён: %s\n", id.c_str());
        return true;
    }

    return false;
}

// ============================================================================
// Очистка всех профилей (кроме встроенных)
// ============================================================================

bool clearProfiles() {
    File root = LittleFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return false;
    }

    int deleted = 0;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();

            // Проверить, что это профиль
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                // Проверить, не встроенный ли
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, file);

                if (!error && !doc["metadata"]["isBuiltin"].as<bool>()) {
                    LittleFS.remove(filename);
                    deleted++;
                }
            }
        }
        file = root.openNextFile();
    }

    Serial.printf("Удалено профилей: %d\n", deleted);
    return true;
}

// ============================================================================
// Загрузка встроенных рецептов
// ============================================================================

bool loadBuiltinProfiles() {
    uint32_t now = millis() / 1000;

    // ========================================================================
    // 1. Сахарная брага 40%
    // ========================================================================
    {
        Profile profile;
        profile.id = "builtin_sugar_40";

        profile.metadata.name = "Сахарная брага 40%";
        profile.metadata.description = "Классическая ректификация сахарной браги с крепостью 40%";
        profile.metadata.category = "rectification";
        profile.metadata.tags = {"сахар", "классика", "40%"};
        profile.metadata.created = now;
        profile.metadata.updated = now;
        profile.metadata.author = "system";
        profile.metadata.isBuiltin = true;

        profile.parameters.mode = "rectification";
        profile.parameters.model = "classic";

        profile.parameters.heater.maxPower = 3000;
        profile.parameters.heater.autoMode = true;
        profile.parameters.heater.pidKp = 2.0;
        profile.parameters.heater.pidKi = 0.5;
        profile.parameters.heater.pidKd = 1.0;
        profile.parameters.heater.boosterEnabled = true;
        profile.parameters.heater.boosterStopCubeTempC = 78.0f;

        profile.parameters.rectification.stabilizationMin = 20;
        profile.parameters.rectification.headsVolume = 50;
        profile.parameters.rectification.bodyVolume = 2000;
        profile.parameters.rectification.tailsVolume = 100;
        profile.parameters.rectification.headsSpeed = 150;
        profile.parameters.rectification.bodySpeed = 300;
        profile.parameters.rectification.tailsSpeed = 400;
        profile.parameters.rectification.purgeMin = 5;

        profile.parameters.temperatures.maxCube = 98.0;
        profile.parameters.temperatures.maxColumn = 82.0;
        profile.parameters.temperatures.headsEnd = 78.5;
        profile.parameters.temperatures.bodyStart = 78.0;
        profile.parameters.temperatures.bodyEnd = 85.0;

        profile.parameters.safety.maxRuntime = 720;
        profile.parameters.safety.waterFlowMin = 2.0;
        profile.parameters.safety.pressureMax = 150;

        profile.statistics.useCount = 0;
        profile.statistics.lastUsed = 0;
        profile.statistics.avgDuration = 0;
        profile.statistics.avgYield = 0;
        profile.statistics.successRate = 0;

        saveProfile(profile);
    }

    // ========================================================================
    // 2. Зерновая брага 12%
    // ========================================================================
    {
        Profile profile;
        profile.id = "builtin_grain_12";

        profile.metadata.name = "Зерновая брага 12%";
        profile.metadata.description = "Бережная ректификация зерновой браги с сохранением органолептики";
        profile.metadata.category = "rectification";
        profile.metadata.tags = {"зерно", "пшеница", "12%"};
        profile.metadata.created = now;
        profile.metadata.updated = now;
        profile.metadata.author = "system";
        profile.metadata.isBuiltin = true;

        profile.parameters.mode = "rectification";
        profile.parameters.model = "classic";

        profile.parameters.heater.maxPower = 2500;
        profile.parameters.heater.autoMode = true;
        profile.parameters.heater.pidKp = 2.0;
        profile.parameters.heater.pidKi = 0.5;
        profile.parameters.heater.pidKd = 1.0;
        profile.parameters.heater.boosterEnabled = true;
        profile.parameters.heater.boosterStopCubeTempC = 78.0f;

        profile.parameters.rectification.stabilizationMin = 30;
        profile.parameters.rectification.headsVolume = 100;
        profile.parameters.rectification.bodyVolume = 2500;
        profile.parameters.rectification.tailsVolume = 150;
        profile.parameters.rectification.headsSpeed = 120;
        profile.parameters.rectification.bodySpeed = 250;
        profile.parameters.rectification.tailsSpeed = 350;
        profile.parameters.rectification.purgeMin = 5;

        profile.parameters.temperatures.maxCube = 98.0;
        profile.parameters.temperatures.maxColumn = 82.0;
        profile.parameters.temperatures.headsEnd = 78.0;
        profile.parameters.temperatures.bodyStart = 77.5;
        profile.parameters.temperatures.bodyEnd = 84.0;

        profile.parameters.safety.maxRuntime = 720;
        profile.parameters.safety.waterFlowMin = 2.0;
        profile.parameters.safety.pressureMax = 150;

        profile.statistics.useCount = 0;
        profile.statistics.lastUsed = 0;
        profile.statistics.avgDuration = 0;
        profile.statistics.avgYield = 0;
        profile.statistics.successRate = 0;

        saveProfile(profile);
    }

    // ========================================================================
    // 3. Фруктовая дистилляция
    // ========================================================================
    {
        Profile profile;
        profile.id = "builtin_fruit_dist";

        profile.metadata.name = "Фруктовая дистилляция";
        profile.metadata.description = "Бережная дистилляция фруктовых браг с сохранением ароматики";
        profile.metadata.category = "distillation";
        profile.metadata.tags = {"фрукты", "дистилляция", "аромат"};
        profile.metadata.created = now;
        profile.metadata.updated = now;
        profile.metadata.author = "system";
        profile.metadata.isBuiltin = true;

        profile.parameters.mode = "distillation";
        profile.parameters.model = "classic";

        profile.parameters.heater.maxPower = 2000;
        profile.parameters.heater.autoMode = false;
        profile.parameters.heater.pidKp = 2.0;
        profile.parameters.heater.pidKi = 0.5;
        profile.parameters.heater.pidKd = 1.0;
        profile.parameters.heater.boosterEnabled = true;
        profile.parameters.heater.boosterStopCubeTempC = 78.0f;

        profile.parameters.distillation.headsVolume = 30;
        profile.parameters.distillation.targetVolume = 3000;
        profile.parameters.distillation.speed = 500;
        profile.parameters.distillation.endTemp = 96.0;

        profile.parameters.temperatures.maxCube = 98.0;
        profile.parameters.temperatures.maxColumn = 90.0;
        profile.parameters.temperatures.headsEnd = 82.0;
        profile.parameters.temperatures.bodyStart = 78.0;
        profile.parameters.temperatures.bodyEnd = 96.0;

        profile.parameters.safety.maxRuntime = 480;
        profile.parameters.safety.waterFlowMin = 1.5;
        profile.parameters.safety.pressureMax = 100;

        profile.statistics.useCount = 0;
        profile.statistics.lastUsed = 0;
        profile.statistics.avgDuration = 0;
        profile.statistics.avgYield = 0;
        profile.statistics.successRate = 0;

        saveProfile(profile);
    }

    Serial.println("Встроенные рецепты загружены");
    return true;
}

// ============================================================================
// Ротация профилей
// ============================================================================

void rotateProfiles() {
    std::vector<String> files;
    std::vector<bool> builtinFlags;

    // Собрать список файлов
    File root = LittleFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                // Проверить, встроенный ли
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, file);
                bool isBuiltin = false;
                if (!error) {
                    isBuiltin = doc["metadata"]["isBuiltin"].as<bool>();
                }

                files.push_back(filename);
                builtinFlags.push_back(isBuiltin);
            }
        }
        file = root.openNextFile();
    }

    // Посчитать пользовательские профили
    int userProfiles = 0;
    for (bool isBuiltin : builtinFlags) {
        if (!isBuiltin) userProfiles++;
    }

    // Удалить старые пользовательские если превышен лимит
    if (userProfiles > (MAX_PROFILES - MAX_BUILTIN_PROFILES)) {
        // Сортировать по времени (старые первые)
        std::sort(files.begin(), files.end());

        int toDelete = userProfiles - (MAX_PROFILES - MAX_BUILTIN_PROFILES);
        for (size_t i = 0; i < files.size() && toDelete > 0; i++) {
            if (!builtinFlags[i]) {
                Serial.printf("Удаление старого профиля: %s\n", files[i].c_str());
                LittleFS.remove(files[i]);
                toDelete--;
            }
        }
    }
}

// ============================================================================
// Вспомогательные функции
// ============================================================================

uint16_t getProfileCount() {
    uint16_t count = 0;

    File root = LittleFS.open(PROFILES_DIR);
    if (!root || !root.isDirectory()) {
        return 0;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = file.name();
            if (filename.startsWith("profile_") && filename.endsWith(".json")) {
                count++;
            }
        }
        file = root.openNextFile();
    }

    return count;
}

bool validateProfile(const Profile& profile) {
    // Проверить название
    if (profile.metadata.name.length() == 0 ||
        profile.metadata.name.length() > MAX_PROFILE_NAME_LEN) {
        Serial.println("Валидация: неверное название");
        return false;
    }

    // Проверить описание
    if (profile.metadata.description.length() > MAX_PROFILE_DESC_LEN) {
        Serial.println("Валидация: слишком длинное описание");
        return false;
    }

    // Проверить категорию
    if (profile.metadata.category != "rectification" &&
        profile.metadata.category != "distillation" &&
        profile.metadata.category != "mashing") {
        Serial.println("Валидация: неверная категория");
        return false;
    }

    // Проверить объемы
    if (profile.parameters.mode == "rectification") {
        if (profile.parameters.rectification.headsVolume == 0 ||
            profile.parameters.rectification.bodyVolume == 0) {
            Serial.println("Валидация: неверные объемы для ректификации");
            return false;
        }
    } else if (profile.parameters.mode == "mashing" ||
               profile.metadata.category == "mashing") {
        if (profile.parameters.mashing.steps.empty()) {
            Serial.println("Валидация: для затирки нужен хотя бы один шаг");
            return false;
        }
        if (profile.parameters.mashing.steps.size() > PROFILE_MASHING_MAX_STEPS) {
            Serial.println("Валидация: слишком много шагов затирки");
            return false;
        }
        for (const auto& step : profile.parameters.mashing.steps) {
            if (step.temperature < PROFILE_MASHING_MIN_TEMP_C ||
                step.temperature > PROFILE_MASHING_MAX_TEMP_C ||
                step.duration == 0 ||
                step.duration > PROFILE_MASHING_MAX_DURATION_MIN) {
                Serial.println("Валидация: некорректные шаги затирки");
                return false;
            }
        }
    }

    // Проверить температуры
    if (profile.parameters.temperatures.maxCube < 0 ||
        profile.parameters.temperatures.maxCube > 120) {
        Serial.println("Валидация: неверная температура куба");
        return false;
    }

    if (profile.parameters.heater.boosterStopCubeTempC < 20.0f ||
        profile.parameters.heater.boosterStopCubeTempC > 100.0f) {
        Serial.println("Валидация: неверный порог отключения booster");
        return false;
    }

    // Все проверки пройдены
    return true;
}

// ============================================================================
// Применение профиля
// ============================================================================

bool applyProfile(const String& id) {
    Profile profile;
    if (!loadProfile(id, profile)) {
        return false;
    }

    Serial.printf("Применение профиля: %s\n", profile.metadata.name.c_str());

    // Общие
    g_settings.equipment.heaterPowerW = profile.parameters.heater.maxPower;
    g_settings.equipment.boosterHeaterEnabled =
        profile.parameters.heater.boosterEnabled;
    g_settings.equipment.boosterHeaterStopCubeTempC =
        profile.parameters.heater.boosterStopCubeTempC;

    // Безопасность
    g_settings.safety.pressureMaxMmHg = profile.parameters.safety.pressureMax;

    if (profile.metadata.category == "rectification" || profile.parameters.mode == "rectification") {
        applyRectificationSettings(profile.parameters.rectification);

        // Пересчет абсолютных объемов в проценты от сырья (aaMl = volumeL * 10 * abv)
        float aaMl = g_settings.rectParams.feedVolumeL * 10.0f * g_settings.rectParams.feedAbvPercent;
        if (aaMl > 0) {
            g_settings.rectParams.headsPercent = profile.parameters.rectification.headsVolume * 100.0f / aaMl;
            g_settings.rectParams.bodyPercent = profile.parameters.rectification.bodyVolume * 100.0f / aaMl;
            g_settings.rectParams.tailsPercent = profile.parameters.rectification.tailsVolume * 100.0f / aaMl;
        }
    } else if (profile.metadata.category == "distillation" || profile.parameters.mode == "distillation") {
        applyDistillationSettings(profile.parameters.distillation);
        
        // приблизительный процент мощности
        float powerPct = (float)profile.parameters.heater.maxPower / 3000.0f * 100.0f;
        if (powerPct > 100.0f) powerPct = 100.0f;
        if (powerPct < 0.0f) powerPct = 0.0f;
        g_settings.distillationUi.powerPercent = profile.parameters.heater.autoMode ? 100.0f : powerPct;
    }

    // Сохранение в NVS
    NVSManager::saveSettings(g_settings);
    setActiveProfile(profile.id, profile.metadata.name);

    Serial.println("Профиль успешно применён и сохранён в глобальные настройки.");
    return true;
}

// ============================================================================
// Обновление статистики
// ============================================================================

void updateProfileStatistics(const String& id, bool success, uint32_t duration, uint16_t yield) {
    Profile profile;
    if (!loadProfile(id, profile)) {
        return;
    }

    // Обновить статистику
    profile.statistics.useCount++;
    profile.statistics.lastUsed = millis() / 1000;

    // Обновить среднюю длительность
    if (profile.statistics.avgDuration == 0) {
        profile.statistics.avgDuration = duration;
    } else {
        profile.statistics.avgDuration =
            (profile.statistics.avgDuration * (profile.statistics.useCount - 1) + duration) /
            profile.statistics.useCount;
    }

    // Обновить средний выход
    if (profile.statistics.avgYield == 0) {
        profile.statistics.avgYield = yield;
    } else {
        profile.statistics.avgYield =
            (profile.statistics.avgYield * (profile.statistics.useCount - 1) + yield) /
            profile.statistics.useCount;
    }

    // Обновить процент успеха
    int successCount = (int)(profile.statistics.successRate * (profile.statistics.useCount - 1) / 100.0f);
    if (success) successCount++;
    profile.statistics.successRate = (float)successCount / profile.statistics.useCount * 100.0f;

    // Обновить timestamp
    profile.metadata.updated = millis() / 1000;

    // Сохранить обратно
    saveProfile(profile);

    Serial.printf("Статистика профиля обновлена: %s\n", profile.metadata.name.c_str());
}

// ============================================================================
// Создание профиля из текущих настроек
// ============================================================================

void updateProfileLearning(const ProcessHistory& history) {
    const String profileId = history.process.profileId;
    if (profileId.isEmpty()) {
        return;
    }

    Profile profile;
    if (!loadProfile(profileId, profile)) {
        return;
    }

    const bool success = history.metadata.completedSuccessfully;
    profile.statistics.useCount++;
    profile.statistics.lastUsed = history.metadata.endTime > 0
                                      ? history.metadata.endTime
                                      : history.metadata.startTime;
    profile.statistics.successRate =
        ((success ? 1.0f : 0.0f) +
         (profile.statistics.successRate / 100.0f) *
             static_cast<float>(profile.statistics.useCount - 1)) /
        static_cast<float>(profile.statistics.useCount) * 100.0f;

    profile.learning.lastProcessId = history.id;
    if (success) {
        profile.learning.successfulRuns++;
        profile.statistics.avgDuration = updateRollingAverageU32(
            profile.statistics.avgDuration, history.metadata.duration,
            profile.learning.successfulRuns);
        profile.statistics.avgYield = updateRollingAverageU16(
            profile.statistics.avgYield, history.results.totalCollected,
            profile.learning.successfulRuns);
        profile.learning.avgEnergyUsed = updateRollingAverage(
            profile.learning.avgEnergyUsed, history.metrics.energyUsed,
            profile.learning.successfulRuns);
        profile.learning.avgEnergyPerLiter = updateRollingAverage(
            profile.learning.avgEnergyPerLiter, computeEnergyPerLiter(history),
            profile.learning.successfulRuns);
        profile.learning.avgProcessHealth = updateRollingAverage(
            profile.learning.avgProcessHealth, history.metrics.avgProcessHealth,
            profile.learning.successfulRuns);
        profile.learning.avgStabilityIndex = updateRollingAverage(
            profile.learning.avgStabilityIndex, history.metrics.avgStabilityIndex,
            profile.learning.successfulRuns);
        profile.learning.typicalCubeFinalTemp = updateRollingAverage(
            profile.learning.typicalCubeFinalTemp, history.metrics.cube.final,
            profile.learning.successfulRuns);
        profile.learning.typicalColumnTopFinalTemp = updateRollingAverage(
            profile.learning.typicalColumnTopFinalTemp, history.metrics.columnTop.final,
            profile.learning.successfulRuns);
        profile.learning.lastSuccessfulProcessId = history.id;
        profile.validation = buildValidationSnapshot(history);
    } else {
        profile.learning.failedRuns++;
    }

    profile.metadata.updated = profile.statistics.lastUsed;
    saveProfile(profile);
}

void setActiveProfile(const String& id, const String& name) {
    g_activeProfileId = id;
    g_activeProfileName = name;
}

void clearActiveProfile() {
    g_activeProfileId = "";
    g_activeProfileName = "";
}

String getActiveProfileId() {
    return g_activeProfileId;
}

String getActiveProfileName() {
    return g_activeProfileName;
}

String createProfileFromSettings(const String& name, const String& description, const String& category) {
    Profile profile;

    // Generate an installation-local ID and avoid overwriting profiles when a
    // batch import contains several entries in the same second.
    const uint32_t now = millis() / 1000;
    const String baseId = String(now);
    String candidateId = baseId;
    uint16_t suffix = 0;
    while (LittleFS.exists(String(PROFILES_DIR) + "/profile_" + candidateId + ".json") &&
           suffix < 1000) {
        candidateId = baseId + "_" + String(++suffix);
    }
    if (suffix >= 1000) {
        Serial.println("Profile import rejected: unable to allocate a unique ID");
        return "";
    }
    profile.id = candidateId;

    profile.metadata.name = name;
    profile.metadata.description = description;
    profile.metadata.category = category;
    profile.metadata.created = now;
    profile.metadata.updated = now;
    profile.metadata.author = "user";
    profile.metadata.isBuiltin = false;

    profile.parameters.mode = category;
    profile.parameters.model = "classic";

    // Нагреватель
    profile.parameters.heater.maxPower = g_settings.equipment.heaterPowerW > 0 ? g_settings.equipment.heaterPowerW : 3000;
    profile.parameters.heater.autoMode = true;
    profile.parameters.heater.pidKp = 2.0;
    profile.parameters.heater.pidKi = 0.5;
    profile.parameters.heater.pidKd = 1.0;
    profile.parameters.heater.boosterEnabled =
        g_settings.equipment.boosterHeaterEnabled;
    profile.parameters.heater.boosterStopCubeTempC =
        g_settings.equipment.boosterHeaterStopCubeTempC;

    // Безопасность
    profile.parameters.safety.maxRuntime = 720;
    profile.parameters.safety.waterFlowMin = 2.0;
    profile.parameters.safety.pressureMax = g_settings.safety.pressureMaxMmHg > 0 ? (uint16_t)g_settings.safety.pressureMaxMmHg : 150;

    // Температуры (оставляем общими разумными дефолтами)
    profile.parameters.temperatures.maxCube = 98.0;
    profile.parameters.temperatures.maxColumn = 82.0;
    profile.parameters.temperatures.headsEnd = 78.5;
    profile.parameters.temperatures.bodyStart = 78.0;
    profile.parameters.temperatures.bodyEnd = 85.0;

    if (category == "rectification" || category == "manual_rect") {
        captureRectificationSettings(profile.parameters.rectification);
        profile.parameters.rectification.tailsSpeed = g_settings.rectParams.bodySpeedMlHKw; // fallback

        // Пересчет процентов в абсолютные мл
        float aaMl = g_settings.rectParams.feedVolumeL * 10.0f * g_settings.rectParams.feedAbvPercent;
        profile.parameters.rectification.headsVolume = (uint16_t)(aaMl * g_settings.rectParams.headsPercent / 100.0f);
        profile.parameters.rectification.bodyVolume = (uint16_t)(aaMl * g_settings.rectParams.bodyPercent / 100.0f);
        profile.parameters.rectification.tailsVolume = (uint16_t)(aaMl * g_settings.rectParams.tailsPercent / 100.0f);
    } else if (category == "distillation") {
        captureDistillationSettings(profile.parameters.distillation);
    } else if (category == "mashing") {
        fillDefaultMashingSteps(profile.parameters.mashing);
    }

    // Статистика
    profile.statistics.useCount = 0;
    profile.statistics.lastUsed = 0;
    profile.statistics.avgDuration = 0;
    profile.statistics.avgYield = 0;
    profile.statistics.successRate = 0;

    if (saveProfile(profile)) {
        return profile.id;
    }

    return "";
}

// ============================================================================
// Экспорт/Импорт профилей
// ============================================================================

String exportProfileToJSON(const String& id) {
    Profile profile;
    if (!loadProfile(id, profile)) {
        Serial.printf("Ошибка: профиль не найден для экспорта: %s\n", id.c_str());
        return "";
    }

    JsonDocument doc;

    // Используем ту же структуру, что и при сохранении
    doc["id"] = profile.id;

    JsonObject metadata = doc["metadata"].to<JsonObject>();
    metadata["name"] = profile.metadata.name;
    metadata["description"] = profile.metadata.description;
    metadata["category"] = profile.metadata.category;

    JsonArray tags = metadata["tags"].to<JsonArray>();
    for (const auto& tag : profile.metadata.tags) {
        tags.add(tag);
    }

    metadata["created"] = profile.metadata.created;
    metadata["updated"] = profile.metadata.updated;
    metadata["author"] = profile.metadata.author;
    metadata["isBuiltin"] = profile.metadata.isBuiltin;

    JsonObject parameters = doc["parameters"].to<JsonObject>();
    parameters["mode"] = profile.parameters.mode;
    parameters["model"] = profile.parameters.model;

    JsonObject heater = parameters["heater"].to<JsonObject>();
    heater["maxPower"] = profile.parameters.heater.maxPower;
    heater["autoMode"] = profile.parameters.heater.autoMode;
    heater["pidKp"] = profile.parameters.heater.pidKp;
    heater["pidKi"] = profile.parameters.heater.pidKi;
    heater["pidKd"] = profile.parameters.heater.pidKd;

    appendRectificationJson(parameters, profile.parameters.rectification);

    appendDistillationJson(parameters, profile.parameters.distillation);
    appendMashingJson(parameters, profile.parameters.mashing);

    JsonObject temperatures = parameters["temperatures"].to<JsonObject>();
    temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
    temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
    temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
    temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
    temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

    JsonObject safety = parameters["safety"].to<JsonObject>();
    safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
    safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
    safety["pressureMax"] = profile.parameters.safety.pressureMax;

    JsonObject statistics = doc["statistics"].to<JsonObject>();
    statistics["useCount"] = profile.statistics.useCount;
    statistics["lastUsed"] = profile.statistics.lastUsed;
    statistics["avgDuration"] = profile.statistics.avgDuration;
    statistics["avgYield"] = profile.statistics.avgYield;
    statistics["successRate"] = profile.statistics.successRate;
    appendLearningJson(doc["learning"].to<JsonObject>(), profile.learning);
    appendProfileValidationJson(doc["validation"].to<JsonObject>(), profile);

    String json;
    serializeJson(doc, json);

    Serial.printf("Профиль экспортирован: %s (%d байт)\n", profile.metadata.name.c_str(), json.length());
    return json;
}

String exportAllProfilesToJSON(bool includeBuiltin) {
    std::vector<ProfileListItem> profiles = getProfileList();

    JsonDocument doc; 
    JsonArray array = doc.to<JsonArray>();

    int exported = 0;
    for (const auto& item : profiles) {
        // Пропустить встроенные если не требуется
        if (!includeBuiltin && item.isBuiltin) {
            continue;
        }

        // Загрузить полный профиль
        Profile profile;
        if (loadProfile(item.id, profile)) {
            JsonObject obj = array.add<JsonObject>();

            obj["id"] = profile.id;

            JsonObject metadata = obj["metadata"].to<JsonObject>();
            metadata["name"] = profile.metadata.name;
            metadata["description"] = profile.metadata.description;
            metadata["category"] = profile.metadata.category;

            JsonArray tags = metadata["tags"].to<JsonArray>();
            for (const auto& tag : profile.metadata.tags) {
                tags.add(tag);
            }

            metadata["created"] = profile.metadata.created;
            metadata["updated"] = profile.metadata.updated;
            metadata["author"] = profile.metadata.author;
            metadata["isBuiltin"] = profile.metadata.isBuiltin;

            JsonObject parameters = obj["parameters"].to<JsonObject>();
            parameters["mode"] = profile.parameters.mode;
            parameters["model"] = profile.parameters.model;

            JsonObject heater = parameters["heater"].to<JsonObject>();
            heater["maxPower"] = profile.parameters.heater.maxPower;
            heater["autoMode"] = profile.parameters.heater.autoMode;
            heater["pidKp"] = profile.parameters.heater.pidKp;
            heater["pidKi"] = profile.parameters.heater.pidKi;
            heater["pidKd"] = profile.parameters.heater.pidKd;
            heater["boosterEnabled"] = profile.parameters.heater.boosterEnabled;
            heater["boosterStopCubeTempC"] =
                profile.parameters.heater.boosterStopCubeTempC;

            appendRectificationJson(parameters, profile.parameters.rectification);

            appendDistillationJson(parameters,
                                   profile.parameters.distillation);
            appendMashingJson(parameters, profile.parameters.mashing);

            JsonObject temperatures = parameters["temperatures"].to<JsonObject>();
            temperatures["maxCube"] = profile.parameters.temperatures.maxCube;
            temperatures["maxColumn"] = profile.parameters.temperatures.maxColumn;
            temperatures["headsEnd"] = profile.parameters.temperatures.headsEnd;
            temperatures["bodyStart"] = profile.parameters.temperatures.bodyStart;
            temperatures["bodyEnd"] = profile.parameters.temperatures.bodyEnd;

            JsonObject safety = parameters["safety"].to<JsonObject>();
            safety["maxRuntime"] = profile.parameters.safety.maxRuntime;
            safety["waterFlowMin"] = profile.parameters.safety.waterFlowMin;
            safety["pressureMax"] = profile.parameters.safety.pressureMax;

            JsonObject statistics = obj["statistics"].to<JsonObject>();
            statistics["useCount"] = profile.statistics.useCount;
            statistics["lastUsed"] = profile.statistics.lastUsed;
            statistics["avgDuration"] = profile.statistics.avgDuration;
            statistics["avgYield"] = profile.statistics.avgYield;
            statistics["successRate"] = profile.statistics.successRate;
            appendLearningJson(obj["learning"].to<JsonObject>(), profile.learning);
            appendProfileValidationJson(obj["validation"].to<JsonObject>(),
                                        profile);

            exported++;
        }
    }

    String json;
    serializeJson(doc, json);

    Serial.printf("Экспортировано профилей: %d (%d байт)\n", exported, json.length());
    return json;
}

String importProfileFromJSON(const String& jsonStr) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.printf("Ошибка парсинга JSON при импорте: %s\n", error.c_str());
        return "";
    }

    Profile profile;

    // Генерируем новый ID на основе текущего времени
    uint32_t now = millis() / 1000;
    profile.id = String(now);

    // Метаданные
    profile.metadata.name = doc["metadata"]["name"].as<String>();
    profile.metadata.description = doc["metadata"]["description"].as<String>();
    profile.metadata.category = doc["metadata"]["category"].as<String>();

    profile.metadata.tags.clear();
    JsonArray tags = doc["metadata"]["tags"];
    for (JsonVariant tag : tags) {
        profile.metadata.tags.push_back(tag.as<String>());
    }

    profile.metadata.created = now; // Новое время создания
    profile.metadata.updated = now;
    profile.metadata.author = doc["metadata"]["author"] | "imported";
    profile.metadata.isBuiltin = false; // Импортированные профили не встроенные

    // Параметры
    profile.parameters.mode = doc["parameters"]["mode"].as<String>();
    profile.parameters.model = doc["parameters"]["model"] | "classic";

    profile.parameters.heater.maxPower = doc["parameters"]["heater"]["maxPower"];
    profile.parameters.heater.autoMode = doc["parameters"]["heater"]["autoMode"];
    profile.parameters.heater.pidKp = doc["parameters"]["heater"]["pidKp"];
    profile.parameters.heater.pidKi = doc["parameters"]["heater"]["pidKi"];
    profile.parameters.heater.pidKd = doc["parameters"]["heater"]["pidKd"];
    profile.parameters.heater.boosterEnabled =
        doc["parameters"]["heater"]["boosterEnabled"] |
        g_settings.equipment.boosterHeaterEnabled;
    profile.parameters.heater.boosterStopCubeTempC =
        doc["parameters"]["heater"]["boosterStopCubeTempC"] |
        g_settings.equipment.boosterHeaterStopCubeTempC;

    loadRectificationParamsFromJson(doc["parameters"]["rectification"],
                                    profile.parameters.rectification);

    loadDistillationParamsFromJson(doc["parameters"]["distillation"],
                                   profile.parameters.distillation);
    loadMashingParamsFromJson(doc["parameters"]["mashing"]["steps"],
                              profile.parameters.mashing);

    profile.parameters.temperatures.maxCube = doc["parameters"]["temperatures"]["maxCube"];
    profile.parameters.temperatures.maxColumn = doc["parameters"]["temperatures"]["maxColumn"];
    profile.parameters.temperatures.headsEnd = doc["parameters"]["temperatures"]["headsEnd"];
    profile.parameters.temperatures.bodyStart = doc["parameters"]["temperatures"]["bodyStart"];
    profile.parameters.temperatures.bodyEnd = doc["parameters"]["temperatures"]["bodyEnd"];

    profile.parameters.safety.maxRuntime = doc["parameters"]["safety"]["maxRuntime"];
    profile.parameters.safety.waterFlowMin = doc["parameters"]["safety"]["waterFlowMin"];
    profile.parameters.safety.pressureMax = doc["parameters"]["safety"]["pressureMax"];

    // Сбросить статистику для импортированного профиля
    profile.statistics.useCount = 0;
    profile.statistics.lastUsed = 0;
    profile.statistics.avgDuration = 0;
    profile.statistics.avgYield = 0;
    profile.statistics.successRate = 0;
    profile.learning = ProfileLearningSnapshot();
    profile.validation = ProfileValidationSnapshot();

    // Keep the exported validation baseline so a community profile remains
    // auditable after import. Runtime usage statistics are intentionally reset.
    JsonObject validation = doc["validation"].as<JsonObject>();
    if (!validation.isNull()) {
        profile.validation.validatedAt = validation["validatedAt"] | 0;
        profile.validation.sourceProcessId = validation["sourceProcessId"].as<String>();
        profile.validation.atmosphereHpa = validation["atmosphereHpa"] | 0.0f;
        profile.validation.atmosphereMmHg = validation["atmosphereMmHg"] | 0.0f;
        profile.validation.columnHeightMm = validation["columnHeightMm"] | 0;
        profile.validation.packingType = validation["packingType"].as<String>();
        profile.validation.packingCoeff = validation["packingCoeff"] | 0.0f;
        profile.validation.heaterPowerW = validation["heaterPowerW"] | 0;
        profile.validation.targetPowerW = validation["targetPowerW"] | 0;
        profile.validation.feedVolumeL = validation["feedVolumeL"] | 0.0f;
        profile.validation.feedAbvPercent = validation["feedAbvPercent"] | 0.0f;
        profile.validation.cubeChargePercent = validation["cubeChargePercent"] | 0.0f;
        profile.validation.headsActualMl = validation["headsActualMl"] | 0;
        profile.validation.bodyActualMl = validation["bodyActualMl"] | 0;
        profile.validation.tailsActualMl = validation["tailsActualMl"] | 0;
        profile.validation.headsCutColumnTopC = validation["headsCutColumnTopC"] | 0.0f;
        profile.validation.bodyCutColumnTopC = validation["bodyCutColumnTopC"] | 0.0f;
        profile.validation.tailsCutColumnTopC = validation["tailsCutColumnTopC"] | 0.0f;
        profile.validation.cubeFinalC = validation["cubeFinalC"] | 0.0f;
        profile.validation.columnTopFinalC = validation["columnTopFinalC"] | 0.0f;
        profile.validation.avgStabilityIndex = validation["avgStabilityIndex"] | 0.0f;
        profile.validation.avgProcessHealth = validation["avgProcessHealth"] | 0.0f;
        profile.validation.equipmentSnapshotJson = validation["equipmentSnapshotJson"] | "";
    }

    if (saveProfile(profile)) {
        Serial.printf("Профиль импортирован: %s (новый ID: %s)\n",
                      profile.metadata.name.c_str(), profile.id.c_str());
        return profile.id;
    }

    return "";
}

uint16_t importProfilesFromJSON(const String& jsonStr) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.printf("Ошибка парсинга JSON массива при импорте: %s\n", error.c_str());
        return 0;
    }

    JsonArray array;
    JsonDocument normalizedDoc;
    if (doc.is<JsonArray>()) {
        array = doc.as<JsonArray>();
    } else if (doc.is<JsonObject>() && doc["profiles"].is<JsonArray>()) {
        normalizedDoc.set(doc["profiles"]);
        array = normalizedDoc.as<JsonArray>();
    } else if (doc.is<JsonObject>() && doc["profile"].is<JsonObject>()) {
        JsonArray single = normalizedDoc.to<JsonArray>();
        single.add(doc["profile"].as<JsonObject>());
        array = normalizedDoc.as<JsonArray>();
    } else if (doc.is<JsonObject>() && doc["metadata"].is<JsonObject>() &&
               doc["parameters"].is<JsonObject>()) {
        JsonArray single = normalizedDoc.to<JsonArray>();
        single.add(doc.as<JsonObject>());
        array = normalizedDoc.as<JsonArray>();
    } else {
        Serial.println("Ошибка: JSON импорта не является профилем, массивом или snapshot-объектом");
        return 0;
    }
    uint16_t imported = 0;

    for (JsonObject obj : array) {
        // Сериализуем каждый объект обратно в строку для передачи в importProfileFromJSON
        String profileJson;
        serializeJson(obj, profileJson);

        if (!importProfileFromJSON(profileJson).isEmpty()) {
            imported++;
        }
    }

    Serial.printf("Импортировано профилей: %d из %d\n", imported, array.size());
    return imported;
}
