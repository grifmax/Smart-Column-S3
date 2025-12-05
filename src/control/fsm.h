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
     * @param state Состояние системы
     * @param settings Настройки
     */
    void update(SystemState& state, const Settings& settings);
    
    /**
     * Запуск режима
     * @param state Состояние
     * @param settings Настройки
     * @param mode Режим
     */
    void startMode(SystemState& state, const Settings& settings, Mode mode);
    
    /**
     * Остановка текущего режима
     * @param state Состояние
     */
    void stopMode(SystemState& state);
    
    /**
     * Пауза
     * @param state Состояние
     */
    void pause(SystemState& state);
    
    /**
     * Продолжение после паузы
     * @param state Состояние
     */
    void resume(SystemState& state);
    
    /**
     * Переход на следующую фракцию (ручной)
     * @param state Состояние
     * @param settings Настройки
     */
    void nextFraction(SystemState& state, const Settings& settings);
    
    // =========================================================================
    // АВТО-РЕКТИФИКАЦИЯ
    // =========================================================================
    
    namespace Rectification {
        /**
         * Обработка фазы HEATING
         */
        void handleHeating(SystemState& state, const Settings& settings);
        
        /**
         * Обработка фазы STABILIZATION
         */
        void handleStabilization(SystemState& state, const Settings& settings);
        
        /**
         * Обработка фазы HEADS
         */
        void handleHeads(SystemState& state, const Settings& settings);
        
        /**
         * Обработка фазы PURGE
         */
        void handlePurge(SystemState& state, const Settings& settings);
        
        /**
         * Обработка фазы BODY (с Smart Decrement)
         */
        void handleBody(SystemState& state, const Settings& settings);
        
        /**
         * Обработка фазы TAILS
         */
        void handleTails(SystemState& state, const Settings& settings);
        
        /**
         * Обработка фазы FINISH
         */
        void handleFinish(SystemState& state, const Settings& settings);
        
        /**
         * Переход на следующую фазу
         */
        void transitionTo(SystemState& state, RectPhase newPhase);
    }
    
    // =========================================================================
    // РУЧНАЯ РЕКТИФИКАЦИЯ
    // =========================================================================
    
    namespace ManualRect {
        void update(SystemState& state, const Settings& settings);
    }
    
    // =========================================================================
    // ДИСТИЛЛЯЦИЯ
    // =========================================================================
    
    namespace Distillation {
        void update(SystemState& state, const Settings& settings);
    }
    
    // =========================================================================
    // ЗАТИРКА
    // =========================================================================
    
    namespace Mashing {
        void update(SystemState& state, const Settings& settings);
        void setProfile(uint8_t profileIndex);
        void nextStep(SystemState& state);
    }
    
    // =========================================================================
    // HOLD
    // =========================================================================
    
    namespace Hold {
        void update(SystemState& state, const Settings& settings);
        void setSteps(const TempStep* steps, uint8_t count);
    }
    
    /**
     * Получение имени текущей фазы
     * @param phase Фаза
     * @return Строка с именем
     */
    const char* getPhaseName(RectPhase phase);
    
    /**
     * Получение имени режима
     * @param mode Режим
     * @return Строка с именем
     */
    const char* getModeName(Mode mode);
}

#endif // FSM_H
