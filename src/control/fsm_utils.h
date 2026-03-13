#ifndef FSM_UTILS_H
#define FSM_UTILS_H

#include "../types.h"
#include "../config.h"

namespace FSM {
    // Вспомогательные функции, общие для всех режимов
    float clampFloat(float v, float vmin, float vmax);
    float getWaterAutoStartTempC(const Settings& settings);
    float getAtmosphereHpa(const SystemState& state);
    float pressureAdjustedCubeTemp(float baseTempC, const SystemState& state);
    float estimateChargeAbvPercent(const SystemState& state);
    uint8_t getProcessHeaterPower(const SystemState& state, const Settings& settings, uint8_t fallbackPercent);
    
    // Глобальные переменные состояния фаз (остаются в fsm.cpp, но доступны через геттеры/сеттеры)
    uint32_t getPhaseStartTime();
    void setPhaseStartTime(uint32_t time);
    float getPhaseStartVolumeMl();
    void setPhaseStartVolumeMl(float volume);
}

#endif
