#pragma once

#include <cmath>
#include <cstdint>


// Чистая логика обкаточного режима на воде: таблица шагов, критерии годности
// и правила перехода. Здесь нет ни драйверов, ни Arduino — только арифметика
// над уже собранными измерениями, поэтому всё покрывается нативными тестами.
// Шаги обкаточного режима на воде. Объявлены здесь, а не в types.h:
// так логика шагов остаётся свободной от Arduino и тестируется нативно.
enum class WaterTestStep : uint8_t {
  IDLE = 0,
  PRECHECK,
  HEAT_FULL,
  HEAT_LOW,
  COOLING,
  PRESSURE_CURVE,
  TAKEOFF,
  FSM_RUN,
  INJECTION_WATER_OFF,
  INJECTION_SENSOR_DROP,
  INJECTION_PRESSURE_BLOCK,
  REPORT,
  COMPLETED,
  FAULTED
};

// Вердикт отдельной проверки
enum class WaterTestVerdict : uint8_t { SKIP = 0, PASS, WARN, FAIL };

namespace WaterTestLogic {


// --- Длительности шагов -----------------------------------------------------

constexpr uint32_t kPrecheckMs = 5000;
constexpr uint32_t kHeatFullMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kHeatLowMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kCoolingMs = 2UL * 60UL * 1000UL;
constexpr uint32_t kPressureStageMs = 3UL * 60UL * 1000UL;
constexpr uint32_t kTakeoffMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kTakeoffWaitMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kInjectionWaterOffMs = 120UL * 1000UL;
constexpr uint32_t kInjectionSensorDropMs = 30UL * 1000UL;
constexpr uint32_t kInjectionPressureBlockMs = 60UL * 1000UL;
constexpr uint32_t kReportMs = 3000;
constexpr uint32_t kFsrRunMaxMs = 4UL * 60UL * 60UL * 1000UL;

// --- Пороги годности -------------------------------------------------------

constexpr float kPowerMatchPassPercent = 10.0f;   // PZEM против уставки
constexpr float kPowerMatchWarnPercent = 20.0f;
constexpr float kHeatRateMinCPerMin = 0.05f;      // вода должна греться
constexpr float kZeroCrossMinPerSec = 80.0f;      // 50 Гц × 2 фронта = 100/с
constexpr float kCoolingMarginFactor = 1.20f;     // теплосъём ≥ 1.2 × мощности
constexpr float kPumpVolumePassPercent = 5.0f;
constexpr float kPumpVolumeWarnPercent = 10.0f;
constexpr float kSensorsSpreadMaxC = 0.5f;
constexpr float kPressureZeroMaxMmHg = 1.5f;
constexpr float kPressurePresenceMinV = 0.12f;    // MPX5010DP: 0.2 В при 0 кПа
constexpr float kWaterSpecificHeatJPerKgK = 4186.0f;

// --- Тест-профиль полного прогона FSM --------------------------------------
// Малые цели, чтобы прогон на воде занимал часы, а не сутки.
constexpr float kTestHeadsTargetMl = 50.0f;
constexpr float kTestBodyTargetMl = 300.0f;
constexpr float kTestTailsTargetMl = 50.0f;

struct Verdict {
  WaterTestVerdict verdict = WaterTestVerdict::SKIP;
  float expected = 0.0f;
  float actual = 0.0f;
};

inline WaterTestVerdict worst(WaterTestVerdict a, WaterTestVerdict b) {
  return static_cast<uint8_t>(a) >= static_cast<uint8_t>(b) ? a : b;
}

inline const char* verdictToken(WaterTestVerdict verdict) {
  switch (verdict) {
    case WaterTestVerdict::PASS: return "pass";
    case WaterTestVerdict::WARN: return "warn";
    case WaterTestVerdict::FAIL: return "fail";
    case WaterTestVerdict::SKIP:
    default: return "skip";
  }
}

// --- Таблица шагов ---------------------------------------------------------

inline const char* stepTitle(WaterTestStep step) {
  switch (step) {
    case WaterTestStep::PRECHECK: return "Предполётная проверка";
    case WaterTestStep::HEAT_FULL: return "Нагрев 100 %";
    case WaterTestStep::HEAT_LOW: return "Нагрев 30 %";
    case WaterTestStep::COOLING: return "Охлаждение и проток";
    case WaterTestStep::PRESSURE_CURVE: return "Кривая давления";
    case WaterTestStep::TAKEOFF: return "Отбор и дозирование";
    case WaterTestStep::FSM_RUN: return "Полный прогон FSM";
    case WaterTestStep::INJECTION_WATER_OFF: return "Инъекция: нет воды";
    case WaterTestStep::INJECTION_SENSOR_DROP: return "Инъекция: отвал датчика";
    case WaterTestStep::INJECTION_PRESSURE_BLOCK: return "Инъекция: линия давления";
    case WaterTestStep::REPORT: return "Отчёт";
    case WaterTestStep::COMPLETED: return "Завершено";
    case WaterTestStep::FAULTED: return "Прервано";
    case WaterTestStep::IDLE:
    default: return "Ожидание";
  }
}

inline const char* stepKey(WaterTestStep step) {
  switch (step) {
    case WaterTestStep::PRECHECK: return "precheck";
    case WaterTestStep::HEAT_FULL: return "heat_full";
    case WaterTestStep::HEAT_LOW: return "heat_low";
    case WaterTestStep::COOLING: return "cooling";
    case WaterTestStep::PRESSURE_CURVE: return "pressure_curve";
    case WaterTestStep::TAKEOFF: return "takeoff";
    case WaterTestStep::FSM_RUN: return "fsm_run";
    case WaterTestStep::INJECTION_WATER_OFF: return "injection_water_off";
    case WaterTestStep::INJECTION_SENSOR_DROP: return "injection_sensor_drop";
    case WaterTestStep::INJECTION_PRESSURE_BLOCK: return "injection_pressure_block";
    case WaterTestStep::REPORT: return "report";
    case WaterTestStep::COMPLETED: return "completed";
    case WaterTestStep::FAULTED: return "faulted";
    case WaterTestStep::IDLE:
    default: return "idle";
  }
}

inline bool isInjectionStep(WaterTestStep step) {
  return step == WaterTestStep::INJECTION_WATER_OFF ||
         step == WaterTestStep::INJECTION_SENSOR_DROP ||
         step == WaterTestStep::INJECTION_PRESSURE_BLOCK;
}

// Сколько шаг может длиться до принудительного перехода дальше.
inline uint32_t stepMaxMs(WaterTestStep step) {
  switch (step) {
    case WaterTestStep::PRECHECK: return kPrecheckMs;
    case WaterTestStep::HEAT_FULL: return kHeatFullMs;
    case WaterTestStep::HEAT_LOW: return kHeatLowMs;
    case WaterTestStep::COOLING: return kCoolingMs;
    case WaterTestStep::PRESSURE_CURVE: return kPressureStageMs * 3UL;
    case WaterTestStep::TAKEOFF: return kTakeoffMs + kTakeoffWaitMs;
    case WaterTestStep::FSM_RUN: return kFsrRunMaxMs;
    case WaterTestStep::INJECTION_WATER_OFF: return kInjectionWaterOffMs;
    case WaterTestStep::INJECTION_SENSOR_DROP: return kInjectionSensorDropMs;
    case WaterTestStep::INJECTION_PRESSURE_BLOCK: return kInjectionPressureBlockMs;
    case WaterTestStep::REPORT: return kReportMs;
    default: return 0;
  }
}

// --- Проверки --------------------------------------------------------------

// Сверка фактической мощности с уставкой. Ловит пробитый/недогревающий
// симистор, сбитую калибровку ТЭНа и неверно введённую паспортную мощность.
inline Verdict evaluatePowerMatch(float configuredW, float measuredW) {
  Verdict out;
  out.expected = configuredW;
  out.actual = measuredW;
  if (configuredW <= 1.0f) {
    out.verdict = WaterTestVerdict::SKIP;
    return out;
  }
  const float errorPercent =
      fabsf(measuredW - configuredW) * 100.0f / configuredW;
  if (errorPercent <= kPowerMatchPassPercent) {
    out.verdict = WaterTestVerdict::PASS;
  } else if (errorPercent <= kPowerMatchWarnPercent) {
    out.verdict = WaterTestVerdict::WARN;
  } else {
    out.verdict = WaterTestVerdict::FAIL;
  }
  return out;
}

inline Verdict evaluateHeatRate(float rateCPerMin) {
  Verdict out;
  out.expected = kHeatRateMinCPerMin;
  out.actual = rateCPerMin;
  out.verdict = rateCPerMin >= kHeatRateMinCPerMin ? WaterTestVerdict::PASS
                                                   : WaterTestVerdict::FAIL;
  return out;
}

// Детектор zero-cross обязан давать ~100 импульсов в секунду. Ноль или мало —
// ТЭН фактически не управляется, хотя HMI показывает уставку.
inline Verdict evaluateZeroCross(uint32_t deltaCount, uint32_t elapsedMs) {
  Verdict out;
  out.expected = kZeroCrossMinPerSec;
  if (elapsedMs == 0) {
    out.verdict = WaterTestVerdict::SKIP;
    return out;
  }
  const float perSec =
      static_cast<float>(deltaCount) * 1000.0f / static_cast<float>(elapsedMs);
  out.actual = perSec;
  out.verdict = perSec >= kZeroCrossMinPerSec ? WaterTestVerdict::PASS
                                              : WaterTestVerdict::FAIL;
  return out;
}

// Теплосъём дефлегматора: Q = G[л/мин]/60 · c · dT. Должен перекрывать
// мощность ТЭНа с запасом, иначе на спирте получим прорыв по ТСА.
inline float coolingCapacityW(float flowLpm, float deltaTC) {
  if (flowLpm <= 0.0f || deltaTC <= 0.0f) {
    return 0.0f;
  }
  return (flowLpm / 60.0f) * kWaterSpecificHeatJPerKgK * deltaTC;
}

inline Verdict evaluateCooling(float flowLpm, float deltaTC, float heaterW) {
  Verdict out;
  out.expected = heaterW * kCoolingMarginFactor;
  out.actual = coolingCapacityW(flowLpm, deltaTC);
  if (flowLpm <= 0.0f) {
    out.verdict = WaterTestVerdict::FAIL;
    return out;
  }
  out.verdict = out.actual >= out.expected ? WaterTestVerdict::PASS
                                           : WaterTestVerdict::FAIL;
  return out;
}

inline Verdict evaluatePumpVolume(float commandedMl, float measuredMl) {
  Verdict out;
  out.expected = commandedMl;
  out.actual = measuredMl;
  if (commandedMl <= 1.0f || measuredMl <= 0.0f) {
    out.verdict = WaterTestVerdict::SKIP;
    return out;
  }
  const float errorPercent =
      fabsf(measuredMl - commandedMl) * 100.0f / commandedMl;
  if (errorPercent <= kPumpVolumePassPercent) {
    out.verdict = WaterTestVerdict::PASS;
  } else if (errorPercent <= kPumpVolumeWarnPercent) {
    out.verdict = WaterTestVerdict::WARN;
  } else {
    out.verdict = WaterTestVerdict::FAIL;
  }
  return out;
}

inline Verdict evaluateSensorsSpread(float minC, float maxC) {
  Verdict out;
  out.expected = kSensorsSpreadMaxC;
  out.actual = maxC - minC;
  out.verdict = out.actual <= kSensorsSpreadMaxC ? WaterTestVerdict::PASS
                                                 : WaterTestVerdict::FAIL;
  return out;
}

inline Verdict evaluatePressureZero(float pressureMmHg) {
  Verdict out;
  out.expected = kPressureZeroMaxMmHg;
  out.actual = fabsf(pressureMmHg);
  out.verdict = out.actual <= kPressureZeroMaxMmHg ? WaterTestVerdict::PASS
                                                   : WaterTestVerdict::FAIL;
  return out;
}

// Датчик MPX5010DP при 0 кПа всегда выдаёт ~0.2 В. Ноль вольт на входе — это
// не «нулевое давление», а неподключённый или мёртвый датчик.
inline Verdict evaluatePressurePresence(float voltage) {
  Verdict out;
  out.expected = kPressurePresenceMinV;
  out.actual = voltage;
  out.verdict = voltage >= kPressurePresenceMinV ? WaterTestVerdict::PASS
                                                 : WaterTestVerdict::FAIL;
  return out;
}

// Оценка коэффициента насадки по измеренному dP на полной мощности.
// dP пропорционален нагрузке примерно в степени 1.7, поэтому до точки захлёба
// закладываем полуторакратный запас. Это оценка, а не измерение захлёба.
inline float suggestPackingCoeff(float pressureMmHgAtFullPower,
                                 float columnHeightMm) {
  const float heightM = columnHeightMm / 1000.0f;
  if (heightM <= 0.01f || pressureMmHgAtFullPower <= 0.0f) {
    return 0.0f;
  }
  const float coeff = (pressureMmHgAtFullPower * 1.5f) / heightM;
  if (coeff < 0.5f) return 0.5f;
  if (coeff > 60.0f) return 60.0f;
  return coeff;
}

// --- Последовательность ----------------------------------------------------

// Следующий шаг. Шаги, которым не хватает железа, помечаются SKIP вызывающей
// стороной, но последовательность остаётся линейной и предсказуемой.
inline WaterTestStep nextStep(WaterTestStep step) {
  switch (step) {
    case WaterTestStep::IDLE: return WaterTestStep::PRECHECK;
    case WaterTestStep::PRECHECK: return WaterTestStep::HEAT_FULL;
    case WaterTestStep::HEAT_FULL: return WaterTestStep::HEAT_LOW;
    case WaterTestStep::HEAT_LOW: return WaterTestStep::COOLING;
    case WaterTestStep::COOLING: return WaterTestStep::PRESSURE_CURVE;
    case WaterTestStep::PRESSURE_CURVE: return WaterTestStep::TAKEOFF;
    case WaterTestStep::TAKEOFF: return WaterTestStep::FSM_RUN;
    case WaterTestStep::FSM_RUN: return WaterTestStep::INJECTION_WATER_OFF;
    // Инъекции идут от безобидных к той, что гарантированно защёлкнет аварию:
    // после настоящего трипа режим останавливается, поэтому «отвальный» тест
    // должен быть последним, иначе следующие шаги не выполнятся.
    case WaterTestStep::INJECTION_WATER_OFF:
      return WaterTestStep::INJECTION_PRESSURE_BLOCK;
    case WaterTestStep::INJECTION_PRESSURE_BLOCK:
      return WaterTestStep::INJECTION_SENSOR_DROP;
    case WaterTestStep::INJECTION_SENSOR_DROP: return WaterTestStep::REPORT;
    case WaterTestStep::REPORT: return WaterTestStep::COMPLETED;
    default: return WaterTestStep::COMPLETED;
  }
}

// Мощность, которую шаг требует от ТЭНа (в процентах от паспортной).
inline uint8_t stepHeaterPercent(WaterTestStep step) {
  switch (step) {
    case WaterTestStep::HEAT_FULL: return 100;
    case WaterTestStep::HEAT_LOW: return 30;
    case WaterTestStep::COOLING: return 100;
    case WaterTestStep::PRESSURE_CURVE: return 100;
    case WaterTestStep::INJECTION_WATER_OFF: return 60;
    case WaterTestStep::INJECTION_PRESSURE_BLOCK: return 60;
    default: return 0;
  }
}

// Держать ли клапан воды открытым на шаге.
inline bool stepNeedsWater(WaterTestStep step) {
  switch (step) {
    case WaterTestStep::IDLE:
    case WaterTestStep::PRECHECK:
    case WaterTestStep::TAKEOFF:
    case WaterTestStep::COMPLETED:
    case WaterTestStep::FAULTED:
      return false;
    default:
      return true;
  }
}

// Общий вердикт прогона.
inline WaterTestVerdict overallVerdict(uint32_t pass, uint32_t warn,
                                       uint32_t fail) {
  if (fail > 0) return WaterTestVerdict::FAIL;
  if (warn > 0) return WaterTestVerdict::WARN;
  if (pass > 0) return WaterTestVerdict::PASS;
  return WaterTestVerdict::SKIP;
}

// Ожидаемый исход инъекции: тип аварии, который обязан появиться.
// Отдельная функция, чтобы тесты фиксировали контракт, а не текст.
enum class InjectionExpectation : uint8_t {
  NONE = 0,
  LOW_WATER,
  SENSOR_FAILURE,
  PRESSURE_NO_LOAD_WARNING,
};

inline InjectionExpectation injectionExpectation(WaterTestStep step) {
  switch (step) {
    case WaterTestStep::INJECTION_WATER_OFF:
      return InjectionExpectation::LOW_WATER;
    case WaterTestStep::INJECTION_SENSOR_DROP:
      return InjectionExpectation::SENSOR_FAILURE;
    case WaterTestStep::INJECTION_PRESSURE_BLOCK:
      return InjectionExpectation::PRESSURE_NO_LOAD_WARNING;
    default:
      return InjectionExpectation::NONE;
  }
}

inline const char* injectionExpectationToken(InjectionExpectation expectation) {
  switch (expectation) {
    case InjectionExpectation::LOW_WATER: return "low_water";
    case InjectionExpectation::SENSOR_FAILURE: return "sensor_failure";
    case InjectionExpectation::PRESSURE_NO_LOAD_WARNING:
      return "pressure_no_load_warning";
    case InjectionExpectation::NONE:
    default: return "none";
  }
}

}  // namespace WaterTestLogic
