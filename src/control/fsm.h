/**
 * Smart-Column S3 - Конечный автомат (FSM)
 * 
 * Управление фазами ректификации и других режимов
 */

#ifndef FSM_H
#define FSM_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace FSM {
    /**
     * Обновление состояния (вызывать в loop)
     */
    void update(SystemState& state, const Settings& settings);
    
    /**
     * Запуск режима
     */
    void startMode(SystemState& state, const Settings& settings, Mode mode);
    
    /**
     * Остановка текущего режима
     */
    void stopMode(SystemState& state);
    void abortMode(SystemState& state);
    
    /**
     * Пауза / Продолжение
     */
    void pause(SystemState& state);
    void resume(SystemState& state);
    
    /**
     * Переход на следующую фракцию (ручной)
     */
    void nextFraction(SystemState& state, const Settings& settings);
    
    // =========================================================================
    // ОБРАБОТЧИКИ РЕЖИМОВ (Реализованы в src/control/modes/*.cpp)
    // =========================================================================
    
    namespace Rectification {
        void update(SystemState& state, const Settings& settings);
        void initSession(const SystemState& state, const Settings& settings);
        void getTargets(float& heads, float& body, float& tails);
    }
    
    namespace ManualRect {
        void update(SystemState& state, const Settings& settings);
        void setPhase(SystemState& state, RectPhase phase);
        void setTakeoffRateMlH(float speedMlH);
    }
    
    namespace Distillation {
        void update(SystemState& state, const Settings& settings);
        bool confirmFractionProgram(SystemState& state, const Settings& settings);
        bool advanceFractionProgram(SystemState& state, const Settings& settings);
        void setParams(float speedMlH, float headsVolumeMl, float targetVolumeMl, float endTempC);
        void setPowerWatts(uint16_t powerWatts);
        void setPowerPercent(uint8_t powerPercent);
        void getParams(float& speedMlH, float& headsVolumeMl, float& targetVolumeMl,
                       float& endTempC, uint16_t& powerWatts);
    }
    
    namespace Mashing {
        void update(SystemState& state, const Settings& settings);
        void setProfile(const MashProfile* profile);
        void nextStep(SystemState& state);
        void start(SystemState& state, const MashProfile* profile);
    }
    
    namespace Hold {
        void update(SystemState& state, const Settings& settings);
        void start(SystemState& state, const TempStep* steps, uint8_t count);
    }
    
    namespace Nbk {
        void update(SystemState& state, const Settings& settings);
    }

    namespace Fermentation {
        void update(SystemState& state, const Settings& settings);
    }

    // =========================================================================
    // ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
    // =========================================================================

    const char* getModeName(Mode mode);
    const char* getPhaseName(RectPhase phase);
    const char* getNbkPhaseName(NbkPhase phase);
    const char* getFermPhaseName(FermentationPhase phase);

    uint32_t getPhaseElapsedSec();
    uint32_t getPhaseTargetSec(const SystemState& state, const Settings& settings);
    uint8_t getPhaseProgressPercent(const SystemState& state, const Settings& settings);

    void getRectTargetsMl(float& headsMl, float& bodyMl, float& tailsMl);
    void getDistillationParams(float& speedMlH, float& headsVolumeMl, float& targetVolumeMl,
                               float& endTempC, uint16_t& powerWatts);
    void getDistillationParams(float& speedMlH, float& headsVolumeMl, float& targetVolumeMl,
                               float& endTempC, uint8_t& powerPercent);
}

#endif // FSM_H
