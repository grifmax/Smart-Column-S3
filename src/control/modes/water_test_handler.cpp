/**
 * Smart-Column S3 - Обкаточный режим на воде
 *
 * Прогоняет железо и защиты без спирта: вода кипит при 99-100 °C, что выше
 * порогов BODY->TAILS (94.5) и TAILS->FINISH (99.0), поэтому на воде целиком
 * проходится настоящий конечный автомат ректификации.
 *
 * Режим ничего не разрешает и не ослабляет: все штатные интерлоки остаются
 * включёнными, потому что смысл прогона - проверить именно их.
 */

#include "../fsm.h"
#include "../fsm_utils.h"
#include "../power_command_gateway.h"
#include "../rect_takeoff.h"
#include "../water_test.h"
#include "../water_test_logic.h"
#include "../v2/reason_codes.h"
#include "../v2/status_adapter.h"
#include "../../drivers/heater.h"
#include "../../drivers/valves.h"
#include "../../drivers/sensors.h"
#include "../../interface/mqtt.h"
#include "../../storage/logger.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace FSM {
namespace WaterTest {

namespace {

using WaterTestLogic::Verdict;

constexpr uint8_t kMaxChecks = 48;
constexpr uint32_t kPressureStageHoldMs = 150000;  // выдержка на ступени dP

CheckRecord g_checks[kMaxChecks];
uint8_t g_checkCount = 0;

// Замеры, которые нельзя хранить в SystemState (только для отчёта)
float g_powerSum = 0.0f;
uint32_t g_powerSamples = 0;
uint32_t g_lastPowerSampleMs = 0;
float g_cubeTempAtStepStart = 0.0f;
uint32_t g_cubeTempAtStepMs = 0;
uint32_t g_zeroCrossAtStepStart = 0;
uint8_t g_pressureStage = 0;
ProcessStats g_savedStats;
bool g_statsSaved = false;
uint32_t g_fsmRunEnteredMs = 0;
bool g_fsmRunEntered = false;

void record(WaterTestStep step, const char* id, WaterTestVerdict verdict,
            float expected, float actual, const char* note) {
  if (g_checkCount >= kMaxChecks) return;
  CheckRecord& item = g_checks[g_checkCount++];
  item.step = static_cast<uint8_t>(step);
  item.verdict = static_cast<uint8_t>(verdict);
  strlcpy(item.id, id, sizeof(item.id));
  strlcpy(item.note, note ? note : "", sizeof(item.note));
  item.expected = expected;
  item.actual = actual;

  Logger::logf(verdict == WaterTestVerdict::FAIL ? 2 : 0,
               "watertest step=%s check=%s verdict=%s expected=%.2f actual=%.2f %s",
               WaterTestLogic::stepKey(step), id,
               WaterTestLogic::verdictToken(verdict), expected, actual,
               note ? note : "");
}

void recordVerdict(WaterTestStep step, const char* id, const Verdict& verdict,
                   const char* note) {
  record(step, id, verdict.verdict, verdict.expected, verdict.actual, note);
  WaterTestState& wt = g_state.waterTest;
  switch (verdict.verdict) {
    case WaterTestVerdict::PASS: ++wt.checksPass; break;
    case WaterTestVerdict::WARN: ++wt.checksWarn; break;
    case WaterTestVerdict::FAIL: ++wt.checksFail; break;
    default: ++wt.checksSkip; break;
  }
}

void countSkip(WaterTestStep step, const char* id, const char* note) {
  record(step, id, WaterTestVerdict::SKIP, 0.0f, 0.0f, note);
  ++g_state.waterTest.checksSkip;
}

void setHeater(const Settings& settings, uint8_t percent) {
  const uint16_t maxW = getConfiguredHeaterPowerWatts(settings);
  const uint16_t watts =
      static_cast<uint16_t>((static_cast<uint32_t>(maxW) * percent) / 100U);
  PowerCommandGateway::setHeaterPowerWatts(PowerCommandGateway::Owner::FSM,
                                           PowerCommandGateway::Reason::FSM_CONTROL,
                                           watts);
}

void setWater(bool open) {
  PowerCommandGateway::setValve(PowerCommandGateway::Owner::FSM,
                                PowerCommandGateway::Reason::FSM_CONTROL,
                                PowerCommandGateway::Valve::WATER, open);
}

void releaseInjection(SystemState& state) {
  state.waterTest.injectionActive = false;
  state.waterTest.injectionStartedMs = 0;
}

// Гасит всё, что мог включить тест. Вызывается на любом выходе из режима,
// чтобы прогон не оставил после себя работающий ТЭН или закрытую воду.
void safeShutdown(SystemState& state) {
  setHeater(g_settings, 0);
  RectTakeoff::stop();
  releaseInjection(state);
  // Воду оставляем открытой, если куб горячий: тест не должен остывать
  // без охлаждения, даже если его прервали.
  setWater(state.temps.cube >= 45.0f);
}

void enterStep(SystemState& state, WaterTestStep next) {
  WaterTestState& wt = state.waterTest;
  wt.step = next;
  wt.stepStartedMs = millis();
  wt.waitingForOperator = false;
  wt.measuredValue = 0.0f;
  g_powerSum = 0.0f;
  g_powerSamples = 0;
  g_lastPowerSampleMs = 0;
  g_cubeTempAtStepStart = state.temps.cube;
  g_cubeTempAtStepMs = millis();
  g_zeroCrossAtStepStart = Heater::getDiagnostics().zeroCrossCount;

  LOG_I("WaterTest: step -> %s (%s)", WaterTestLogic::stepTitle(next),
        WaterTestLogic::stepKey(next));
  MQTT::publishNotification("Обкатка на воде",
                            (String("Шаг: ") + WaterTestLogic::stepTitle(next)).c_str(),
                            "info");

  switch (next) {
    case WaterTestStep::HEAT_FULL:
      setWater(true);
      setHeater(g_settings, 100);
      break;
    case WaterTestStep::HEAT_LOW:
      setWater(true);
      setHeater(g_settings, 30);
      break;
    case WaterTestStep::COOLING:
      setWater(true);
      setHeater(g_settings, 100);
      break;
    case WaterTestStep::PRESSURE_CURVE:
      g_pressureStage = 0;
      setWater(true);
      setHeater(g_settings, 40);
      break;
    case WaterTestStep::TAKEOFF:
      setHeater(g_settings, 0);
      RectTakeoff::beginSession(g_settings);
      break;
    case WaterTestStep::FSM_RUN: {
      g_statsSaved = true;
      g_savedStats = state.stats;
      g_fsmRunEntered = false;
      g_fsmRunEnteredMs = 0;
      Rectification::initSession(state, g_settings);
      // Малые цели: прогон на воде не должен качать сотни литров.
      Rectification::restoreTargets(WaterTestLogic::kTestHeadsTargetMl,
                                    WaterTestLogic::kTestBodyTargetMl,
                                    WaterTestLogic::kTestTailsTargetMl);
      state.rectPhase = RectPhase::HEATING;
      setPhaseStartTime(millis());
      setPhaseStartVolumeMl(state.pump.totalVolumeMl);
      break;
    }
    case WaterTestStep::INJECTION_WATER_OFF:
      setWater(false);
      state.waterTest.injectionActive = true;
      state.waterTest.injectionStartedMs = millis();
      break;
    case WaterTestStep::INJECTION_SENSOR_DROP:
      state.waterTest.injectionActive = true;
      state.waterTest.injectionStartedMs = millis();
      break;
    case WaterTestStep::INJECTION_PRESSURE_BLOCK:
      state.waterTest.injectionActive = true;
      state.waterTest.injectionStartedMs = millis();
      break;
    case WaterTestStep::REPORT:
      safeShutdown(state);
      break;
    case WaterTestStep::COMPLETED:
      safeShutdown(state);
      state.waterTest.active = false;
      break;
    default:
      break;
  }
}

void advance(SystemState& state) {
  enterStep(state, WaterTestLogic::nextStep(state.waterTest.step));
}

void accumulatePower(const SystemState& state) {
  if (state.power.lastUpdate == 0 || state.power.lastUpdate == g_lastPowerSampleMs) {
    return;
  }
  g_lastPowerSampleMs = state.power.lastUpdate;
  if (!state.power.ok) return;
  g_powerSum += state.power.power;
  ++g_powerSamples;
}

float averagePowerW() {
  return g_powerSamples > 0 ? g_powerSum / static_cast<float>(g_powerSamples)
                            : 0.0f;
}

// --- Шаги -----------------------------------------------------------------

void stepPrecheck(SystemState& state, const Settings& settings, uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::PRECHECK;

  if (!state.temps.valid[TEMP_CUBE] || !state.temps.valid[TEMP_TSA] ||
      !state.temps.valid[TEMP_WATER_OUT]) {
    record(step, "sensors_required", WaterTestVerdict::FAIL, 3.0f, 0.0f,
           "нужны куб, ТСА и вода-выход");
    enterStep(state, WaterTestStep::FAULTED);
    return;
  }
  record(step, "sensors_required", WaterTestVerdict::PASS, 3.0f, 3.0f,
         "куб, ТСА, вода-выход на связи");

  // Разброс между датчиками, погружёнными в одну воду.
  float minC = 1e9f;
  float maxC = -1e9f;
  uint8_t count = 0;
  const float values[TEMP_COUNT] = {
      state.temps.cube,       state.temps.columnBottom, state.temps.columnTop,
      state.temps.reflux,     state.temps.tsa,          state.temps.waterIn,
      state.temps.waterOut};
  for (uint8_t i = 0; i < TEMP_COUNT; ++i) {
    if (!state.temps.valid[i]) continue;
    if (values[i] < minC) minC = values[i];
    if (values[i] > maxC) maxC = values[i];
    ++count;
  }
  if (count >= 2) {
    recordVerdict(step, "sensors_spread",
                  WaterTestLogic::evaluateSensorsSpread(minC, maxC),
                  "разброс по датчикам в одной среде");
  } else {
    countSkip(step, "sensors_spread", "меньше двух валидных датчиков");
  }

  // Присутствие датчика давления: 0 В на входе = датчик не подключён.
  const Verdict presence =
      WaterTestLogic::evaluatePressurePresence(state.pressure.sensorVoltage);
  recordVerdict(step, "pressure_present", presence,
                presence.verdict == WaterTestVerdict::PASS
                    ? "датчик давления подключён"
                    : "0 В на входе: датчик не установлен, шаги давления будут пропущены");
  if (presence.verdict == WaterTestVerdict::PASS) {
    recordVerdict(step, "pressure_zero",
                  WaterTestLogic::evaluatePressureZero(state.pressure.cube),
                  "нуль при атмосфере");
  }

  if (state.temps.cube < 15.0f) {
    record(step, "water_filled", WaterTestVerdict::FAIL, 15.0f, state.temps.cube,
           "куб холоднее 15 C: вода не залита?");
    enterStep(state, WaterTestStep::FAULTED);
    return;
  }

  const float feedL = settings.rectParams.feedVolumeL > 0.1f
                          ? settings.rectParams.feedVolumeL
                          : settings.equipment.cubeVolumeL;
  if (feedL + 0.01f < settings.equipment.minHeaterSubmergeL) {
    record(step, "heater_submerge", WaterTestVerdict::FAIL,
           settings.equipment.minHeaterSubmergeL, feedL,
           "объём ниже минимального уровня над ТЭНом");
    enterStep(state, WaterTestStep::FAULTED);
    return;
  }
  record(step, "heater_submerge", WaterTestVerdict::PASS,
         settings.equipment.minHeaterSubmergeL, feedL, "уровень над ТЭНом");

  // Проток: открываем воду и ждём несколько секунд.
  if (elapsed < 3000) {
    setWater(true);
    return;
  }
  if (!state.waterFlow.sensorSeen) {
    // Датчик потока — опция: без него проток не контролируется, и это не
    // провал прогона, а пропущенная проверка.
    countSkip(step, "water_flow",
              "датчик потока не обнаружен: проток не контролируется (опция)");
  } else if (state.waterFlow.litersPerMin > 0.05f) {
    record(step, "water_flow", WaterTestVerdict::PASS, 0.05f,
           state.waterFlow.litersPerMin, "проток охлаждающей воды есть");
  } else {
    record(step, "water_flow", WaterTestVerdict::FAIL, 0.05f,
           state.waterFlow.litersPerMin,
           "нет протока: проверьте кран и шланг");
    enterStep(state, WaterTestStep::FAULTED);
    return;
  }

  advance(state);
}

void stepHeatFull(SystemState& state, const Settings& settings, uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::HEAT_FULL;
  setWater(true);
  setHeater(settings, 100);
  accumulatePower(state);

  if (elapsed < WaterTestLogic::kHeatFullMs) return;

  const uint16_t configuredW = getConfiguredHeaterPowerWatts(settings);
  recordVerdict(step, "power_match",
                WaterTestLogic::evaluatePowerMatch(configuredW, averagePowerW()),
                "фактическая мощность против уставки 100 %");

  const float dtMin =
      static_cast<float>(elapsed) / 60000.0f;
  const float rate =
      dtMin > 0.01f ? (state.temps.cube - g_cubeTempAtStepStart) / dtMin : 0.0f;
  state.waterTest.heatRateCPerMin = rate;
  recordVerdict(step, "heat_rate", WaterTestLogic::evaluateHeatRate(rate),
                "скорость роста температуры куба");

  const uint32_t zeroCrossNow = Heater::getDiagnostics().zeroCrossCount;
  recordVerdict(step, "zero_cross",
                WaterTestLogic::evaluateZeroCross(
                    zeroCrossNow - g_zeroCrossAtStepStart, elapsed),
                "импульсы детектора zero-cross");
  state.waterTest.heaterPowerAtFullW = averagePowerW();

  advance(state);
}

void stepHeatLow(SystemState& state, const Settings& settings, uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::HEAT_LOW;
  setWater(true);
  setHeater(settings, 30);
  accumulatePower(state);

  if (elapsed < WaterTestLogic::kHeatLowMs) return;

  const float expectedW =
      static_cast<float>(getConfiguredHeaterPowerWatts(settings)) * 0.30f;
  recordVerdict(step, "power_match_low",
                WaterTestLogic::evaluatePowerMatch(expectedW, averagePowerW()),
                "линейность фазового управления на 30 %");
  state.waterTest.heaterPowerAtLowW = averagePowerW();

  advance(state);
}

void stepCooling(SystemState& state, const Settings& settings, uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::COOLING;
  setWater(true);
  setHeater(settings, 100);

  if (elapsed < WaterTestLogic::kCoolingMs) return;

  const float deltaT = state.temps.waterOut - state.temps.waterIn;
  const float heaterW = static_cast<float>(getConfiguredHeaterPowerWatts(settings));
  state.waterTest.coolingFlowLpm = state.waterFlow.litersPerMin;
  state.waterTest.coolingDeltaTC = deltaT;
  state.waterTest.coolingCapacityW =
      WaterTestLogic::coolingCapacityW(state.waterFlow.litersPerMin, deltaT);

  if (!state.waterFlow.sensorSeen) {
    countSkip(step, "cooling_capacity",
              "нет датчика потока: теплосъём не измеряется (опция)");
  } else {
    recordVerdict(step, "cooling_capacity",
                  WaterTestLogic::evaluateCooling(
                      state.waterFlow.litersPerMin, deltaT, heaterW),
                  "теплосъём дефлегматора против мощности ТЭНа");
  }

  advance(state);
}

void stepPressureCurve(SystemState& state, const Settings& settings,
                       uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::PRESSURE_CURVE;

  if (!state.pressure.ok) {
    countSkip(step, "pressure_curve", "датчик давления не подключён");
    advance(state);
    return;
  }

  const uint8_t stage = static_cast<uint8_t>(elapsed / WaterTestLogic::kPressureStageMs);
  if (stage != g_pressureStage) {
    g_pressureStage = stage;
    setHeater(settings, stage == 0 ? 40 : (stage == 1 ? 70 : 100));
  }
  setWater(true);

  if (elapsed % WaterTestLogic::kPressureStageMs < kPressureStageHoldMs) {
    return;  // выдержка на ступени ещё не закончена
  }

  const float p = state.pressure.cube;
  if (stage == 0) state.waterTest.pressureAtLowW = p;
  else if (stage == 1) state.waterTest.pressureAtMidW = p;
  else state.waterTest.pressureAtFullW = p;

  if (stage < 2) return;

  // Все три точки сняты.
  const bool rising = state.waterTest.pressureAtFullW >=
                      state.waterTest.pressureAtMidW &&
                      state.waterTest.pressureAtMidW >=
                          state.waterTest.pressureAtLowW;
  record(step, "pressure_monotonic",
         rising ? WaterTestVerdict::PASS : WaterTestVerdict::WARN,
         state.waterTest.pressureAtLowW, state.waterTest.pressureAtFullW,
         rising ? "dP растёт с мощностью"
                : "dP не растёт: проверить трубку и нуль датчика");

  const float coeff = WaterTestLogic::suggestPackingCoeff(
      state.waterTest.pressureAtFullW, settings.equipment.columnHeightMm);
  state.waterTest.suggestedPackingCoeff = coeff;
  record(step, "packing_coeff",
         coeff > 0.0f ? WaterTestVerdict::WARN : WaterTestVerdict::SKIP,
         settings.equipment.packingCoeff, coeff,
         "оценка коэффициента насадки по dP на полной мощности");

  advance(state);
}

void stepTakeoff(SystemState& state, const Settings& settings, uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::TAKEOFF;
  setHeater(settings, 0);
  setWater(state.temps.cube >= 45.0f);

  if (elapsed < WaterTestLogic::kTakeoffMs) {
    // Ровный дозированный отбор: 300 мл/ч в течение 10 минут = 50 мл.
    const float rate = 300.0f;
    RectTakeoffCommand command;
    command.backendType = settings.rectParams.takeoffBackendType;
    // HEADS, а не BODY: на насосном бэкенде открытым держится именно канал
    // голов, а BODY закрывает все продуктовые клапаны и насос упрётся в тупик.
    command.fraction = RectTakeoffFraction::HEADS;
    command.requestedEquivalentRateMlH = rate;
    command.equivalentRateMlH = rate;
    command.enabled = true;
    command.fullReflux = false;
    command.periodicTakeoff = false;
    command.periodicTakeoffActive = true;
    RectTakeoff::apply(command);
    return;
  }

  RectTakeoff::stop();
  state.waterTest.commandedVolumeMl = 50.0f;

  if (!state.waterTest.waitingForOperator) {
    state.waterTest.waitingForOperator = true;
    MQTT::publishNotification(
        "Обкатка на воде",
        "Отбор 50 мл завершён. Взвесьте собранное и введите объём в /api/water-test/measure",
        "warning");
    return;
  }

  if (state.waterTest.measuredValue <= 0.0f) {
    if (elapsed > WaterTestLogic::kTakeoffMs + WaterTestLogic::kTakeoffWaitMs) {
      countSkip(step, "pump_volume", "замер не введён оператором");
      advance(state);
    }
    return;
  }

  recordVerdict(step, "pump_volume",
                WaterTestLogic::evaluatePumpVolume(
                    state.waterTest.commandedVolumeMl,
                    state.waterTest.measuredValue),
                "сверка дозированного объёма с замером");
  advance(state);
}

void stepFsmRun(SystemState& state, const Settings& settings, uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::FSM_RUN;

  if (!g_fsmRunEntered) {
    g_fsmRunEntered = true;
    g_fsmRunEnteredMs = millis();
    record(step, "fsm_start", WaterTestVerdict::PASS, 0.0f, 0.0f,
           "полный прогон ректификации на тест-профиле");
  }

  const RectPhase before = state.rectPhase;
  Rectification::update(state, settings);
  if (state.rectPhase != before) {
    LOG_I("WaterTest: FSM phase %u -> %u", static_cast<unsigned>(before),
          static_cast<unsigned>(state.rectPhase));
  }

  // FINISH штатно переводит систему в IDLE. Для теста это конец прогона,
  // а не конец режима.
  const bool finished = (state.mode == Mode::IDLE);
  if (!finished && elapsed < WaterTestLogic::kFsrRunMaxMs) return;

  if (finished) {
    state.mode = Mode::WATER_TEST;
    record(step, "fsm_complete", WaterTestVerdict::PASS, 0.0f,
           static_cast<float>(millis() - g_fsmRunEnteredMs) / 1000.0f,
           "все фазы пройдены, процесс дошёл до FINISH");
  } else {
    record(step, "fsm_complete", WaterTestVerdict::FAIL,
           static_cast<float>(WaterTestLogic::kFsrRunMaxMs) / 1000.0f,
           static_cast<float>(elapsed) / 1000.0f,
           "прогон не дошёл до FINISH за отведённое время");
  }
  state.rectPhase = RectPhase::IDLE;
  state.rectResume = RectificationResumeState{};
  if (g_statsSaved) {
    state.stats = g_savedStats;
    g_statsSaved = false;
  }
  advance(state);
}

void stepInjectionWaterOff(SystemState& state, const Settings& settings,
                           uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::INJECTION_WATER_OFF;
  // Держим клапан закрытым не дольше шага: тест не имеет права оставить
  // колонну без охлаждения.
  setWater(false);
  setHeater(settings, 60);

  if (state.currentAlarm.type == AlarmType::LOW_WATER) {
    record(step, "trip_low_water", WaterTestVerdict::PASS, 0.0f,
           static_cast<float>(elapsed) / 1000.0f,
           "авария LOW_WATER сработала");
    releaseInjection(state);
    advance(state);
    return;
  }

  if (elapsed < WaterTestLogic::kInjectionWaterOffMs) return;

  setWater(true);
  record(step, "trip_low_water", WaterTestVerdict::FAIL, 1.0f, 0.0f,
         "LOW_WATER не реализован: интерлока по протоку нет");
  releaseInjection(state);
  advance(state);
}

void stepInjectionSensorDrop(SystemState& state, const Settings& settings,
                             uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::INJECTION_SENSOR_DROP;
  setWater(true);
  setHeater(settings, 30);

  // Имитация мёртвого датчика: держим метку времени ТСА устаревшей, как это
  // делает реальный отвал шины. Значение оставляем, valid не трогаем —
  // проверяем именно контур свежести, а не подмену данных.
  state.temps.lastUpdateBySensor[TEMP_TSA] = millis() - SAFETY_SENSOR_TIMEOUT_MS - 5000;

  if (state.currentAlarm.type == AlarmType::SENSOR_FAILURE) {
    record(step, "trip_sensor_failure", WaterTestVerdict::PASS, 0.0f,
           static_cast<float>(elapsed) / 1000.0f,
           "SENSOR_FAILURE сработала");
    releaseInjection(state);
    advance(state);
    return;
  }

  if (elapsed < WaterTestLogic::kInjectionSensorDropMs) return;

  record(step, "trip_sensor_failure", WaterTestVerdict::FAIL,
         static_cast<float>(SAFETY_SENSOR_TIMEOUT_MS) / 1000.0f,
         static_cast<float>(elapsed) / 1000.0f,
         "авария по отвалу обязательного датчика не сработала");
  releaseInjection(state);
  advance(state);
}

void stepInjectionPressureBlock(SystemState& state, const Settings& settings,
                                uint32_t elapsed) {
  const WaterTestStep step = WaterTestStep::INJECTION_PRESSURE_BLOCK;
  setWater(true);
  setHeater(settings, 60);

  if (!state.pressure.ok) {
    countSkip(step, "pressure_no_load", "датчик давления не подключён");
    releaseInjection(state);
    advance(state);
    return;
  }

  // При кипении и 60 % мощности давление обязано быть выше нуля. Обнуляем
  // измерение: система должна заметить, что датчик «не видит нагрузку».
  state.pressure.cube = 0.0f;

  if (elapsed < WaterTestLogic::kInjectionPressureBlockMs) return;

  record(step, "pressure_no_load", WaterTestVerdict::FAIL, 1.0f, 0.0f,
         "проверки «датчик не видит нагрузку» нет: залипший датчик не ловится");
  releaseInjection(state);
  advance(state);
}

void stepReport(SystemState& state, const Settings& settings, uint32_t elapsed) {
  if (elapsed < WaterTestLogic::kReportMs) return;

  const WaterTestVerdict overall = WaterTestLogic::overallVerdict(
      state.waterTest.checksPass, state.waterTest.checksWarn,
      state.waterTest.checksFail);
  Logger::logf(overall == WaterTestVerdict::PASS ? 0 : 2,
               "watertest summary verdict=%s pass=%lu warn=%lu fail=%lu skip=%lu",
               WaterTestLogic::verdictToken(overall),
               static_cast<unsigned long>(state.waterTest.checksPass),
               static_cast<unsigned long>(state.waterTest.checksWarn),
               static_cast<unsigned long>(state.waterTest.checksFail),
               static_cast<unsigned long>(state.waterTest.checksSkip));
  MQTT::publishNotification(
      "Обкатка на воде завершена",
      (String("Вердикт: ") + WaterTestLogic::verdictToken(overall) +
           ", провалов: " + String(state.waterTest.checksFail)).c_str(),
      overall == WaterTestVerdict::PASS ? "success" : "warning");

  advance(state);
}

}  // namespace

// --- Публичный интерфейс ---------------------------------------------------

bool start(SystemState& state, const Settings& settings) {
  // FSM::startMode выставляет state.mode до вызова обработчика, поэтому
  // собственный режим не считается занятым.
  const auto fail = [&state](const char* reason) {
    strlcpy(state.waterTest.lastError, reason,
            sizeof(state.waterTest.lastError));
    state.waterTest.active = false;
    state.mode = Mode::IDLE;
    LOG_W("WaterTest: start rejected: %s", reason);
    return false;
  };

  if (state.mode != Mode::IDLE && state.mode != Mode::WATER_TEST) {
    return fail("режим уже запущен");
  }
  if (settings.demoMode) {
    return fail("демо-режим несовместим с обкаткой");
  }
  if (!state.temps.valid[TEMP_CUBE]) {
    return fail("нет данных датчика куба");
  }

  g_checkCount = 0;
  state.waterTest = WaterTestState{};
  state.waterTest.active = true;
  state.waterTest.startedMs = millis();
  state.mode = Mode::WATER_TEST;
  state.rectPhase = RectPhase::IDLE;
  state.paused = false;
  g_statsSaved = false;

  ControlV2::notePhaseTransition(
      Mode::WATER_TEST, ControlV2::kNoPhaseIdV2,
      static_cast<uint16_t>(WaterTestStep::PRECHECK),
      ControlV2::ReasonCodeV2::RC_MODE_START_REQUEST,
      "Обкатка на воде запущена");
  Logger::logf(0, "watertest start fw=%s", FW_VERSION);
  enterStep(state, WaterTestStep::PRECHECK);
  return true;
}

void update(SystemState& state, const Settings& settings) {
  WaterTestState& wt = state.waterTest;
  if (!wt.active) return;

  const uint32_t now = millis();
  const uint32_t elapsed = now - wt.stepStartedMs;

  switch (wt.step) {
    case WaterTestStep::PRECHECK:
      stepPrecheck(state, settings, elapsed);
      break;
    case WaterTestStep::HEAT_FULL:
      stepHeatFull(state, settings, elapsed);
      break;
    case WaterTestStep::HEAT_LOW:
      stepHeatLow(state, settings, elapsed);
      break;
    case WaterTestStep::COOLING:
      stepCooling(state, settings, elapsed);
      break;
    case WaterTestStep::PRESSURE_CURVE:
      stepPressureCurve(state, settings, elapsed);
      break;
    case WaterTestStep::TAKEOFF:
      stepTakeoff(state, settings, elapsed);
      break;
    case WaterTestStep::FSM_RUN:
      stepFsmRun(state, settings, elapsed);
      break;
    case WaterTestStep::INJECTION_WATER_OFF:
      stepInjectionWaterOff(state, settings, elapsed);
      break;
    case WaterTestStep::INJECTION_SENSOR_DROP:
      stepInjectionSensorDrop(state, settings, elapsed);
      break;
    case WaterTestStep::INJECTION_PRESSURE_BLOCK:
      stepInjectionPressureBlock(state, settings, elapsed);
      break;
    case WaterTestStep::REPORT:
      stepReport(state, settings, elapsed);
      break;
    case WaterTestStep::FAULTED:
      safeShutdown(state);
      wt.active = false;
      break;
    case WaterTestStep::COMPLETED:
    case WaterTestStep::IDLE:
    default:
      break;
  }
}

void stop(SystemState& state) {
  if (!state.waterTest.active) return;
  LOG_W("WaterTest: stopped, step=%s",
        WaterTestLogic::stepKey(state.waterTest.step));
  safeShutdown(state);
  if (g_statsSaved) {
    state.stats = g_savedStats;
    g_statsSaved = false;
  }
  state.rectPhase = RectPhase::IDLE;
  // Шаг не сбрасываем: отчёт должен показать, на каком шаге прогон прерван.
  state.waterTest.active = false;
}

void confirm(SystemState& state) {
  // Ручное подтверждение используется только шагом отбора: остальные шаги
  // идут по времени.
  if (state.waterTest.step == WaterTestStep::TAKEOFF) {
    state.waterTest.waitingForOperator = true;
  }
}

bool setMeasuredVolume(SystemState& state, float milliliters) {
  if (state.waterTest.step != WaterTestStep::TAKEOFF) return false;
  if (!isfinite(milliliters) || milliliters <= 0.0f || milliliters > 1000.0f) {
    return false;
  }
  state.waterTest.measuredValue = milliliters;
  state.waterTest.waitingForOperator = true;
  return true;
}

uint8_t getChecks(const CheckRecord** out) {
  if (out) *out = g_checks;
  return g_checkCount;
}

void resetReport() { g_checkCount = 0; }

}  // namespace WaterTest
}  // namespace FSM
