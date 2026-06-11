#include "history_demo.h"

#include "config.h"
#include "history.h"

#include <FS.h>
#include <math.h>

namespace {

constexpr const char* kDemoDatasetTag = "[demo-dataset-v1]";
constexpr const char* kDemoDeviceId = "demo-dataset-v1";
constexpr uint32_t kRectBaselineId = 1735804800UL;
constexpr uint32_t kRectOptimizedId = 1736413200UL;
constexpr uint32_t kRectSafetyId = 1737021600UL;
constexpr uint32_t kDistillationId = 1737630000UL;

String demoNote(const char* title) {
    return String(kDemoDatasetTag) + " " + title;
}

bool isDemoHistory(const ProcessHistory& history) {
    return history.metadata.deviceId == kDemoDeviceId ||
           history.notes.startsWith(kDemoDatasetTag);
}

bool isDemoHistoryFile(const String& id) {
    ProcessHistory history;
    return loadProcessHistory(id, history) && isDemoHistory(history);
}

void fillTempMetrics(TempMetrics& metrics, float minValue, float maxValue,
                     float avgValue, float finalValue) {
    metrics.min = minValue;
    metrics.max = maxValue;
    metrics.avg = avgValue;
    metrics.final = finalValue;
}

void addPhase(ProcessHistory& history, const char* name, uint32_t startTime,
              uint32_t endTime, float startTemp, float endTemp, uint16_t volume,
              uint16_t avgSpeed, const char* reasonCode,
              const char* operatorMessage = nullptr) {
    ProcessPhase phase;
    phase.name = name;
    phase.startTime = startTime;
    phase.endTime = endTime;
    phase.duration = endTime - startTime;
    phase.startTemp = startTemp;
    phase.endTemp = endTemp;
    phase.volume = volume;
    phase.avgSpeed = avgSpeed;
    phase.reasonCode = reasonCode ? reasonCode : "";
    phase.operatorMessage = operatorMessage ? operatorMessage : "";
    history.phases.push_back(phase);
}

void addWarning(std::vector<ProcessWarning>& target, uint32_t time,
                const char* severity, const char* message,
                const char* reasonCode = nullptr,
                const char* operatorMessage = nullptr) {
    ProcessWarning warning;
    warning.time = time;
    warning.severity = severity ? severity : "info";
    warning.message = message ? message : "";
    warning.reasonCode = reasonCode ? reasonCode : "";
    warning.operatorMessage = operatorMessage ? operatorMessage : "";
    target.push_back(warning);
}

void addAdvisorItem(ProcessAdvisorSnapshot& snapshot, const char* kind,
                    const char* code, const char* tone, const char* title,
                    const char* detail, const char* action,
                    const char* parameterKey = nullptr,
                    float previousValue = 0.0f,
                    float suggestedValue = 0.0f) {
    ProcessAdvisorItem item;
    item.kind = kind ? kind : "";
    item.code = code ? code : "";
    item.tone = tone ? tone : "";
    item.title = title ? title : "";
    item.detail = detail ? detail : "";
    item.action = action ? action : "";
    item.parameterKey = parameterKey ? parameterKey : "";
    item.previousValue = previousValue;
    item.suggestedValue = suggestedValue;
    snapshot.items.push_back(item);
}

void pushRectificationPoint(ProcessHistory& history, uint32_t time,
                            float cube, float columnTop, float columnBottom,
                            float deflegmator, uint16_t power,
                            uint16_t pumpSpeed, float processHealth,
                            float stabilityIndex, float floodRisk,
                            float coolingMarginC, float headsCompletionScore,
                            float bodyEndScore, bool takeoffAllowed) {
    TimeseriesPoint point;
    point.time = time;
    point.cube = cube;
    point.columnTop = columnTop;
    point.columnBottom = columnBottom;
    point.deflegmator = deflegmator;
    point.power = power;
    point.voltage =
        229.0f + sinf(static_cast<float>(history.timeseries.size()) * 0.18f);
    point.current = power / 229.0f;
    point.pumpSpeed = pumpSpeed;
    point.processHealth = processHealth;
    point.stabilityIndex = stabilityIndex;
    point.floodRisk = floodRisk;
    point.coolingMarginC = coolingMarginC;
    point.headsCompletionScore = headsCompletionScore;
    point.bodyEndScore = bodyEndScore;
    point.takeoffAllowed = takeoffAllowed;
    point.sensorFreshnessOk = true;
    history.timeseries.push_back(point);
}

ProcessHistory makeRectBaselineHistory() {
    ProcessHistory history;
    history.id = String(kRectBaselineId);
    history.version = FIRMWARE_VERSION;
    history.metadata.startTime = kRectBaselineId;
    history.metadata.endTime = kRectBaselineId + 23100UL;
    history.metadata.duration =
        history.metadata.endTime - history.metadata.startTime;
    history.metadata.completedSuccessfully = true;
    history.metadata.deviceId = kDemoDeviceId;
    history.process.type = "rectification";
    history.process.mode = "auto";
    history.process.profileId = "rect_spn40_classic";
    history.process.profile = "SPN 40L Classic";
    history.parameters.targetPower = 2350;
    history.parameters.headVolume = 320;
    history.parameters.bodyVolume = 3100;
    history.parameters.tailVolume = 650;
    history.parameters.pumpSpeedHead = 280;
    history.parameters.pumpSpeedBody = 1180;
    history.parameters.stabilizationTime = 2100;
    history.parameters.wattControlEnabled = true;
    history.parameters.smartDecrementEnabled = true;
    fillTempMetrics(history.metrics.cube, 23.4f, 96.8f, 83.1f, 96.2f);
    fillTempMetrics(history.metrics.columnBottom, 22.9f, 79.6f, 77.7f, 79.2f);
    fillTempMetrics(history.metrics.columnTop, 22.5f, 78.41f, 78.14f, 78.37f);
    fillTempMetrics(history.metrics.deflegmator, 20.8f, 34.7f, 28.5f, 30.6f);
    history.metrics.energyUsed = 8.9f;
    history.metrics.avgPower = 2210;
    history.metrics.peakPower = 2350;
    history.metrics.totalVolume = 4070;
    history.metrics.avgSpeed = 1010;
    history.metrics.minProcessHealth = 0.82f;
    history.metrics.avgProcessHealth = 0.91f;
    history.metrics.avgStabilityIndex = 0.89f;
    history.metrics.minCoolingMarginC = 7.8f;
    history.metrics.avgCoolingMarginC = 11.4f;
    history.metrics.maxFloodRisk = 0.28f;
    history.metrics.avgFloodRisk = 0.12f;
    history.metrics.lastHeadsCompletionScore = 0.96f;
    history.metrics.lastBodyEndScore = 0.92f;
    history.metrics.takeoffAllowedSamples = 49;
    history.metrics.sensorFreshnessOkSamples = 49;
    history.metrics.indicatorSamples = 49;
    addPhase(history, "heating", kRectBaselineId, kRectBaselineId + 4800UL,
             23.4f, 78.2f, 0, 0, "RC_HEATING_COMPLETE");
    addPhase(history, "stabilization", kRectBaselineId + 4800UL,
             kRectBaselineId + 6900UL, 78.2f, 78.3f, 0, 0,
             "RC_STABILIZATION_COMPLETE");
    addPhase(history, "heads", kRectBaselineId + 6900UL,
             kRectBaselineId + 11100UL, 78.3f, 78.35f, 320, 280,
             "RC_HEADS_VOLUME_REACHED");
    addPhase(history, "body", kRectBaselineId + 11100UL,
             kRectBaselineId + 20700UL, 78.35f, 78.37f, 3100, 1180,
             "RC_BODY_TARGET_VOLUME_REACHED");
    addPhase(history, "tails", kRectBaselineId + 20700UL,
             history.metadata.endTime, 78.37f, 78.41f, 650, 900,
             "RC_TAILS_TARGET_REACHED");
    for (uint8_t index = 0; index < 49; ++index) {
        const uint32_t time =
            history.metadata.startTime + static_cast<uint32_t>(index) * 480UL;
        uint16_t power = 2350;
        uint16_t pumpSpeed = 0;
        float cube = 23.4f + static_cast<float>(index) * 1.50f;
        float columnTop = 22.5f;
        float columnBottom = 22.9f;
        float deflegmator = 20.8f + static_cast<float>(index) * 0.20f;
        float processHealth = 0.85f + 0.07f * sinf(index * 0.13f);
        float stabilityIndex = 0.86f + 0.05f * cosf(index * 0.15f);
        float floodRisk = 0.08f + 0.05f * fabsf(sinf(index * 0.12f));
        float coolingMargin = 12.4f - 0.06f * index;
        float headsScore = index < 23 ? static_cast<float>(index) / 23.0f : 1.0f;
        float bodyScore =
            index < 25 ? 0.0f : (static_cast<float>(index) - 25.0f) / 22.0f;
        bool takeoffAllowed = false;

        if (index >= 10) {
            columnTop = 78.18f + 0.02f * sinf(index * 0.30f);
            columnBottom = 77.45f + 0.10f * sinf(index * 0.24f);
        }
        if (index >= 15 && index < 24) {
            pumpSpeed = 280;
            takeoffAllowed = true;
        } else if (index >= 24 && index < 44) {
            pumpSpeed = 1180;
            takeoffAllowed = true;
            bodyScore = (static_cast<float>(index) - 24.0f) / 20.0f;
        } else if (index >= 44) {
            pumpSpeed = 900;
            takeoffAllowed = true;
            floodRisk = 0.18f;
            coolingMargin = 8.8f - 0.10f * (index - 44);
            bodyScore = 1.0f;
        }

        pushRectificationPoint(history, time, cube, columnTop, columnBottom,
                               deflegmator, power, pumpSpeed, processHealth,
                               stabilityIndex, floodRisk, coolingMargin,
                               headsScore, bodyScore, takeoffAllowed);
    }
    history.results.headsCollected = 320;
    history.results.bodyCollected = 3100;
    history.results.tailsCollected = 650;
    history.results.totalCollected = 4070;
    history.results.status = "completed";
    addWarning(history.results.warnings, kRectBaselineId + 17640UL, "warning",
               "Body phase finished close to profile limit; dataset keeps this as a mild tail-end reference.",
               "RC_BODY_END_DETECTED",
               "Тело завершилось ровно у ожидаемого хвостового порога.");
    history.notes = demoNote(
        "Эталонный успешный baseline ректификации для сравнения Run Advisor.");
    return history;
}

ProcessHistory makeRectOptimizedHistory() {
    ProcessHistory history;
    history.id = String(kRectOptimizedId);
    history.version = FIRMWARE_VERSION;
    history.metadata.startTime = kRectOptimizedId;
    history.metadata.endTime = kRectOptimizedId + 22380UL;
    history.metadata.duration =
        history.metadata.endTime - history.metadata.startTime;
    history.metadata.completedSuccessfully = true;
    history.metadata.deviceId = kDemoDeviceId;
    history.process.type = "rectification";
    history.process.mode = "auto";
    history.process.profileId = "rect_spn40_classic";
    history.process.profile = "SPN 40L Classic";
    history.parameters.targetPower = 2300;
    history.parameters.headVolume = 320;
    history.parameters.bodyVolume = 3200;
    history.parameters.tailVolume = 620;
    history.parameters.pumpSpeedHead = 300;
    history.parameters.pumpSpeedBody = 1240;
    history.parameters.stabilizationTime = 2280;
    history.parameters.wattControlEnabled = true;
    history.parameters.smartDecrementEnabled = true;
    fillTempMetrics(history.metrics.cube, 24.1f, 96.9f, 83.5f, 96.4f);
    fillTempMetrics(history.metrics.columnBottom, 23.4f, 79.7f, 77.8f, 79.3f);
    fillTempMetrics(history.metrics.columnTop, 22.8f, 78.46f, 78.16f, 78.39f);
    fillTempMetrics(history.metrics.deflegmator, 21.3f, 35.5f, 28.7f, 31.2f);
    history.metrics.energyUsed = 8.5f;
    history.metrics.avgPower = 2160;
    history.metrics.peakPower = 2300;
    history.metrics.totalVolume = 4140;
    history.metrics.avgSpeed = 1058;
    history.metrics.minProcessHealth = 0.79f;
    history.metrics.avgProcessHealth = 0.89f;
    history.metrics.avgStabilityIndex = 0.86f;
    history.metrics.minCoolingMarginC = 6.2f;
    history.metrics.avgCoolingMarginC = 10.5f;
    history.metrics.maxFloodRisk = 0.36f;
    history.metrics.avgFloodRisk = 0.15f;
    history.metrics.lastHeadsCompletionScore = 0.98f;
    history.metrics.lastBodyEndScore = 0.94f;
    history.metrics.takeoffAllowedSamples = 50;
    history.metrics.sensorFreshnessOkSamples = 50;
    history.metrics.indicatorSamples = 50;
    addPhase(history, "heating", kRectOptimizedId, kRectOptimizedId + 4620UL,
             24.1f, 78.2f, 0, 0, "RC_HEATING_COMPLETE");
    addPhase(history, "stabilization", kRectOptimizedId + 4620UL,
             kRectOptimizedId + 6900UL, 78.2f, 78.3f, 0, 0,
             "RC_STABILIZATION_COMPLETE",
             "Стабилизацию слегка увеличили относительно baseline для более спокойного старта.");
    addPhase(history, "heads", kRectOptimizedId + 6900UL,
             kRectOptimizedId + 10740UL, 78.3f, 78.34f, 320, 300,
             "RC_HEADS_VOLUME_REACHED");
    addPhase(history, "body", kRectOptimizedId + 10740UL,
             kRectOptimizedId + 20220UL, 78.34f, 78.39f, 3200, 1240,
             "RC_BODY_TARGET_VOLUME_REACHED");
    addPhase(history, "tails", kRectOptimizedId + 20220UL,
             history.metadata.endTime, 78.39f, 78.46f, 620, 920,
             "RC_TAILS_TARGET_REACHED");
    for (uint8_t index = 0; index < 50; ++index) {
        const uint32_t time =
            history.metadata.startTime + static_cast<uint32_t>(index) * 456UL;
        uint16_t power = 2300;
        uint16_t pumpSpeed = 0;
        float cube = 24.1f + static_cast<float>(index) * 1.47f;
        float columnTop = 22.8f;
        float columnBottom = 23.4f;
        float deflegmator = 21.3f + static_cast<float>(index) * 0.20f;
        float processHealth = 0.84f + 0.05f * sinf(index * 0.11f);
        float stabilityIndex = 0.84f + 0.05f * cosf(index * 0.16f);
        float floodRisk = 0.10f + 0.04f * fabsf(sinf(index * 0.14f));
        float coolingMargin = 11.8f - 0.07f * index;
        float headsScore = index < 22 ? static_cast<float>(index) / 22.0f : 1.0f;
        float bodyScore =
            index < 24 ? 0.0f : (static_cast<float>(index) - 24.0f) / 24.0f;
        bool takeoffAllowed = false;

        if (index >= 10) {
            columnTop = 78.17f + 0.025f * sinf(index * 0.29f);
            columnBottom = 77.50f + 0.12f * sinf(index * 0.22f);
        }
        if (index >= 15 && index < 23) {
            pumpSpeed = 300;
            takeoffAllowed = true;
        } else if (index >= 23 && index < 45) {
            pumpSpeed = 1240;
            takeoffAllowed = true;
            if (index >= 33 && index <= 36) {
                floodRisk = 0.31f + 0.05f * sinf(index);
                coolingMargin = 6.6f + 0.2f * cosf(index);
                processHealth -= 0.08f;
                stabilityIndex -= 0.10f;
            }
        } else if (index >= 45) {
            pumpSpeed = 920;
            takeoffAllowed = true;
            floodRisk = 0.22f;
            coolingMargin = 7.4f - 0.08f * (index - 45);
            bodyScore = 1.0f;
        }

        pushRectificationPoint(history, time, cube, columnTop, columnBottom,
                               deflegmator, power, pumpSpeed, processHealth,
                               stabilityIndex, floodRisk, coolingMargin,
                               headsScore, bodyScore, takeoffAllowed);
    }
    history.results.headsCollected = 320;
    history.results.bodyCollected = 3200;
    history.results.tailsCollected = 620;
    history.results.totalCollected = 4140;
    history.results.status = "completed";
    addWarning(history.results.warnings, kRectOptimizedId + 14900UL, "warning",
               "Cooling margin narrowed during the late body phase, but automation held the run without trip.",
               "RC_SAFETY_LIMIT_TAKEOFF",
               "Во второй половине тела автоматика кратко поджимала отбор из-за сужения cooling margin.");
    history.advisorSnapshot.schemaVersion = "run-advisor-v3";
    history.advisorSnapshot.createdAt = history.metadata.endTime;
    history.advisorSnapshot.baselineProcessId = String(kRectBaselineId);
    history.advisorSnapshot.baselineProfile = history.process.profile;
    addAdvisorItem(history.advisorSnapshot, "summary", "baseline_better_energy",
                   "good", "Энергия лучше baseline",
                   "Этот прогон завершился быстрее и с меньшим расходом энергии, чем предыдущий успешный baseline того же профиля.",
                   "Фиксируйте этот запуск как рабочий эталон для профиля SPN 40L Classic.");
    addAdvisorItem(history.advisorSnapshot, "tuning", "stabilization_hold",
                   "warn", "Стабилизация помогла колонне",
                   "Увеличенная стабилизация сделала старт спокойнее, но к концу тела запас охлаждения всё равно сузился.",
                   "Оставьте стабилизацию около 38 минут и проверьте охлаждение перед ускорением тела.",
                   "stabilizationTime", 2100.0f, 2280.0f);
    addAdvisorItem(history.advisorSnapshot, "tuning", "body_takeoff_margin",
                   "warn", "Тело почти упёрлось в cooling margin",
                   "Средняя производительность тела выросла, но поздний участок тела подошёл близко к ограничению по охлаждению.",
                   "Если нужен ещё запас стабильности, уменьшите скорость тела на 40-60 мл/ч или улучшите поток воды.",
                   "pumpSpeedBody", 1180.0f, 1240.0f);
    history.notes = demoNote(
        "Успешный прогон после применения советов Run Advisor относительно baseline.");
    return history;
}

ProcessHistory makeRectSafetyHistory() {
    ProcessHistory history;
    history.id = String(kRectSafetyId);
    history.version = FIRMWARE_VERSION;
    history.metadata.startTime = kRectSafetyId;
    history.metadata.endTime = kRectSafetyId + 15840UL;
    history.metadata.duration =
        history.metadata.endTime - history.metadata.startTime;
    history.metadata.completedSuccessfully = false;
    history.metadata.deviceId = kDemoDeviceId;
    history.process.type = "rectification";
    history.process.mode = "auto";
    history.process.profileId = "rect_spn40_classic";
    history.process.profile = "SPN 40L Classic";
    history.parameters.targetPower = 2300;
    history.parameters.headVolume = 320;
    history.parameters.bodyVolume = 3200;
    history.parameters.tailVolume = 620;
    history.parameters.pumpSpeedHead = 300;
    history.parameters.pumpSpeedBody = 1280;
    history.parameters.stabilizationTime = 2280;
    history.parameters.wattControlEnabled = true;
    history.parameters.smartDecrementEnabled = true;
    fillTempMetrics(history.metrics.cube, 23.7f, 93.1f, 79.3f, 91.6f);
    fillTempMetrics(history.metrics.columnBottom, 23.0f, 79.8f, 77.4f, 79.5f);
    fillTempMetrics(history.metrics.columnTop, 22.6f, 78.66f, 78.23f, 78.62f);
    fillTempMetrics(history.metrics.deflegmator, 21.1f, 39.8f, 31.0f, 39.2f);
    history.metrics.energyUsed = 6.4f;
    history.metrics.avgPower = 2190;
    history.metrics.peakPower = 2300;
    history.metrics.totalVolume = 1890;
    history.metrics.avgSpeed = 780;
    history.metrics.minProcessHealth = 0.43f;
    history.metrics.avgProcessHealth = 0.76f;
    history.metrics.avgStabilityIndex = 0.67f;
    history.metrics.minCoolingMarginC = 1.8f;
    history.metrics.avgCoolingMarginC = 7.4f;
    history.metrics.maxFloodRisk = 0.71f;
    history.metrics.avgFloodRisk = 0.24f;
    history.metrics.lastHeadsCompletionScore = 1.0f;
    history.metrics.lastBodyEndScore = 0.38f;
    history.metrics.takeoffAllowedSamples = 29;
    history.metrics.sensorFreshnessOkSamples = 34;
    history.metrics.indicatorSamples = 34;
    addPhase(history, "heating", kRectSafetyId, kRectSafetyId + 4680UL, 23.7f,
             78.2f, 0, 0, "RC_HEATING_COMPLETE");
    addPhase(history, "stabilization", kRectSafetyId + 4680UL,
             kRectSafetyId + 6960UL, 78.2f, 78.3f, 0, 0,
             "RC_STABILIZATION_COMPLETE");
    addPhase(history, "heads", kRectSafetyId + 6960UL,
             kRectSafetyId + 10620UL, 78.3f, 78.36f, 320, 300,
             "RC_HEADS_VOLUME_REACHED");
    addPhase(history, "body", kRectSafetyId + 10620UL, history.metadata.endTime,
             78.36f, 78.62f, 1570, 1280, "RC_SAFETY_TRIP_OVERHEAT",
             "Охлаждение перестало держать late body, поэтому safety supervisor остановил процесс.");
    for (uint8_t index = 0; index < 34; ++index) {
        const uint32_t time =
            history.metadata.startTime + static_cast<uint32_t>(index) * 466UL;
        uint16_t power = 2300;
        uint16_t pumpSpeed = 0;
        float cube = 23.7f + static_cast<float>(index) * 2.0f;
        float columnTop = 22.6f;
        float columnBottom = 23.0f;
        float deflegmator = 21.1f + static_cast<float>(index) * 0.40f;
        float processHealth = 0.88f - 0.012f * index;
        float stabilityIndex = 0.86f - 0.013f * index;
        float floodRisk = 0.08f + 0.02f * index;
        float coolingMargin = 13.0f - 0.33f * index;
        float headsScore = index < 18 ? static_cast<float>(index) / 18.0f : 1.0f;
        float bodyScore =
            index < 22 ? 0.0f : (static_cast<float>(index) - 22.0f) / 10.0f;
        bool takeoffAllowed = false;

        if (index >= 10) {
            columnTop = 78.18f + 0.03f * sinf(index * 0.27f);
            columnBottom = 77.48f + 0.12f * sinf(index * 0.24f);
        }
        if (index >= 15 && index < 22) {
            pumpSpeed = 300;
            takeoffAllowed = true;
        } else if (index >= 22) {
            pumpSpeed = 1280;
            takeoffAllowed = index < 33;
            if (index >= 28) {
                deflegmator += 2.2f + 0.35f * (index - 28);
                columnTop += 0.08f + 0.02f * (index - 28);
                floodRisk = 0.48f + 0.05f * (index - 28);
                coolingMargin = 4.2f - 0.6f * (index - 28);
                processHealth -= 0.12f;
                stabilityIndex -= 0.14f;
            }
        }

        pushRectificationPoint(history, time, cube, columnTop, columnBottom,
                               deflegmator, power, pumpSpeed, processHealth,
                               stabilityIndex, floodRisk, coolingMargin,
                               headsScore, bodyScore, takeoffAllowed);
    }
    history.results.headsCollected = 320;
    history.results.bodyCollected = 1570;
    history.results.tailsCollected = 0;
    history.results.totalCollected = 1890;
    history.results.status = "error";
    addWarning(history.results.warnings, kRectSafetyId + 14520UL, "warning",
               "Cooling margin dropped below the stable window and anti-oscillation hold delayed another phase decision.",
               "RC_SAFETY_PHASE_BLOCKED",
               "Автоматика заморозила фазовый переход, потому что indicators стали нестабильными.");
    addWarning(history.results.errors, history.metadata.endTime, "error",
               "Safety stop due to overheated cooling circuit during late body.",
               "RC_SAFETY_TRIP_OVERHEAT",
               "Позднее тело ушло в перегрев воды, и процесс был остановлен защитой.");
    history.advisorSnapshot.schemaVersion = "run-advisor-v3";
    history.advisorSnapshot.createdAt = history.metadata.endTime;
    history.advisorSnapshot.baselineProcessId = String(kRectOptimizedId);
    history.advisorSnapshot.baselineProfile = history.process.profile;
    addAdvisorItem(history.advisorSnapshot, "safety", "cooling_trip", "danger",
                   "Аварийный stop по охлаждению",
                   "Этот прогон потерял cooling margin заметно раньше, чем успешный baseline того же профиля.",
                   "Перед следующим запуском проверьте расход воды, температуру входа и не ускоряйте тело выше baseline.");
    addAdvisorItem(history.advisorSnapshot, "tuning", "body_speed_reduce",
                   "warn", "Тело было слишком агрессивным",
                   "Скорость тела оказалась выше безопасного окна для текущего охлаждения и привела к limit/trip событиям.",
                   "Снизьте скорость тела хотя бы до baseline и сравните late body с предыдущим успешным запуском.",
                   "pumpSpeedBody", 1240.0f, 1160.0f);
    history.notes = demoNote(
        "Негативный rect run с safety stop для проверки аварийной аналитики и human-readable explanations.");
    return history;
}

ProcessHistory makeDistillationHistory() {
    ProcessHistory history;
    history.id = String(kDistillationId);
    history.version = FIRMWARE_VERSION;
    history.metadata.startTime = kDistillationId;
    history.metadata.endTime = kDistillationId + 14700UL;
    history.metadata.duration =
        history.metadata.endTime - history.metadata.startTime;
    history.metadata.completedSuccessfully = true;
    history.metadata.deviceId = kDemoDeviceId;
    history.process.type = "distillation";
    history.process.mode = "auto";
    history.process.profileId = "dist_pot_37l";
    history.process.profile = "Potstill 37L Demo";
    history.parameters.targetPower = 3200;
    history.parameters.headVolume = 150;
    history.parameters.bodyVolume = 4800;
    history.parameters.tailVolume = 700;
    history.parameters.pumpSpeedHead = 0;
    history.parameters.pumpSpeedBody = 0;
    history.parameters.stabilizationTime = 0;
    history.parameters.wattControlEnabled = true;
    history.parameters.smartDecrementEnabled = false;
    fillTempMetrics(history.metrics.cube, 21.9f, 98.6f, 86.7f, 98.1f);
    fillTempMetrics(history.metrics.columnBottom, 21.7f, 87.1f, 74.8f, 86.4f);
    fillTempMetrics(history.metrics.columnTop, 21.4f, 92.8f, 79.6f, 92.2f);
    fillTempMetrics(history.metrics.deflegmator, 0.0f, 0.0f, 0.0f, 0.0f);
    history.metrics.energyUsed = 11.2f;
    history.metrics.avgPower = 3010;
    history.metrics.peakPower = 3200;
    history.metrics.totalVolume = 5650;
    history.metrics.avgSpeed = 0;
    history.metrics.minProcessHealth = 0.74f;
    history.metrics.avgProcessHealth = 0.87f;
    history.metrics.avgStabilityIndex = 0.77f;
    history.metrics.minCoolingMarginC = 0.0f;
    history.metrics.avgCoolingMarginC = 0.0f;
    history.metrics.maxFloodRisk = 0.0f;
    history.metrics.avgFloodRisk = 0.0f;
    history.metrics.lastHeadsCompletionScore = 0.0f;
    history.metrics.lastBodyEndScore = 1.0f;
    history.metrics.takeoffAllowedSamples = 0;
    history.metrics.sensorFreshnessOkSamples = 33;
    history.metrics.indicatorSamples = 33;
    addPhase(history, "heating", kDistillationId, kDistillationId + 5100UL,
             21.9f, 78.5f, 0, 0, "RC_HEATING_COMPLETE");
    addPhase(history, "working", kDistillationId + 5100UL,
             history.metadata.endTime, 78.5f, 98.1f, 5650, 0,
             "RC_DISTILLATION_TARGET_VOLUME_REACHED");
    for (uint8_t index = 0; index < 33; ++index) {
        const uint32_t time =
            history.metadata.startTime + static_cast<uint32_t>(index) * 445UL;
        TimeseriesPoint point;
        point.time = time;
        point.cube = 21.9f + static_cast<float>(index) * 2.33f;
        point.columnTop = 21.4f + static_cast<float>(index) * 2.17f;
        point.columnBottom = 21.7f + static_cast<float>(index) * 1.97f;
        point.deflegmator = 0.0f;
        point.power = index < 6 ? 3200 : 2850;
        point.voltage = 228.0f + 1.2f * sinf(index * 0.22f);
        point.current = point.power / 228.0f;
        point.pumpSpeed = 0;
        point.processHealth = 0.84f + 0.05f * sinf(index * 0.20f);
        point.stabilityIndex = 0.72f + 0.06f * cosf(index * 0.17f);
        point.floodRisk = 0.0f;
        point.coolingMarginC = 0.0f;
        point.headsCompletionScore = 0.0f;
        point.bodyEndScore = static_cast<float>(index) / 32.0f;
        point.takeoffAllowed = false;
        point.sensorFreshnessOk = true;
        history.timeseries.push_back(point);
    }
    history.results.headsCollected = 150;
    history.results.bodyCollected = 4800;
    history.results.tailsCollected = 700;
    history.results.totalCollected = 5650;
    history.results.status = "completed";
    addWarning(history.results.warnings, kDistillationId + 10240UL, "warning",
               "Cube temperature approached the finish window; this run is useful as a non-rectification chart sample.",
               "RC_DISTILLATION_END_TEMP_REACHED",
               "Дистилляция завершилась по целевому окну без аварий.");
    history.notes = demoNote(
        "Дистилляция для проверки фильтров списка истории, графиков и модального окна.");
    return history;
}

bool saveDemoHistory(const ProcessHistory& history, DemoHistorySeedResult& result) {
    const String filename = String(HISTORY_DIR) + "/process_" + history.id + ".json";
    if (LittleFS.exists(filename)) {
        result.skipped++;
        return true;
    }

    if (!saveProcessHistory(history)) {
        return false;
    }

    result.imported++;
    return true;
}

String extractHistoryId(const String& filename) {
    static const String prefix = String(HISTORY_DIR) + "/process_";
    return filename.substring(prefix.length(), filename.length() - 5);
}

}  // namespace

uint16_t countPublicDemoDatasetEntries() {
    uint16_t count = 0;
    File root = LittleFS.open(HISTORY_DIR);
    if (!root || !root.isDirectory()) {
        return 0;
    }

    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const String filename = file.name();
            if (filename.startsWith(String(HISTORY_DIR) + "/process_") &&
                filename.endsWith(".json")) {
                if (isDemoHistoryFile(extractHistoryId(filename))) {
                    count++;
                }
            }
        }
        file = root.openNextFile();
    }

    return count;
}

bool clearPublicDemoDataset(DemoHistorySeedResult& result) {
    File root = LittleFS.open(HISTORY_DIR);
    if (!root || !root.isDirectory()) {
        return false;
    }

    std::vector<String> demoIds;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const String filename = file.name();
            if (filename.startsWith(String(HISTORY_DIR) + "/process_") &&
                filename.endsWith(".json")) {
                const String id = extractHistoryId(filename);
                if (isDemoHistoryFile(id)) {
                    demoIds.push_back(id);
                }
            }
        }
        file = root.openNextFile();
    }

    for (const String& id : demoIds) {
        if (deleteProcess(id)) {
            result.removed++;
        }
    }

    return true;
}

bool seedPublicDemoDataset(DemoHistorySeedResult& result, bool replaceExisting) {
    if (replaceExisting) {
        if (!clearPublicDemoDataset(result)) {
            return false;
        }
    }

    if (!saveDemoHistory(makeRectBaselineHistory(), result)) {
        return false;
    }
    if (!saveDemoHistory(makeRectOptimizedHistory(), result)) {
        return false;
    }
    if (!saveDemoHistory(makeRectSafetyHistory(), result)) {
        return false;
    }
    if (!saveDemoHistory(makeDistillationHistory(), result)) {
        return false;
    }

    return true;
}
