/**
 * Smart-Column S3 - Localization
 * Поддержка русского и английского языков
 */

#ifndef LOCALIZATION_H
#define LOCALIZATION_H

#include "types.h"

enum class Msg : uint16_t {
    // Tabs
    MONITOR,
    CONTROL,
    SETTINGS,
    SERVICE,

    // Dashboard
    CUBE_TEMP,
    HEATER_POWER,
    TOP_T,
    REFLUX_T,
    COLUMN_BOTTOM,
    WATER_IN,
    WATER_OUT,
    PUMP,
    TSA_T,

    // Control
    AUTO_RECTIFY,
    DISTILLATION,
    MANUAL_RECT,
    MASHING,
    HOLD_MODE,
    MANUAL_PUMP,
    STOP,
    PAUSE,
    RESUME,

    // Settings
    EQUIPMENT,
    RECT_PARAMS,
    DIST_PARAMS,
    CALIBRATION,
    THEME,
    SOUND,
    LANGUAGE,

    // Equipment
    COLUMN_HEIGHT,
    CUBE_VOLUME,
    PACKING_COEFF,

    // Rectification
    HEADS_PERCENT,
    HEADS_SPEED,
    BODY_SPEED,
    STABILIZATION,
    PURGE_TIME,

    // Distillation
    DIST_SPEED,
    HEADS_VOLUME,
    TARGET_VOLUME,
    END_TEMP,

    // Calibration
    PUMP_CALIBRATION,
    TOUCH_CALIBRATION,

    // Manual
    MANUAL_MODE,
    VALVE_WATER,
    VALVE_HEADS,
    VALVE_UNO,

    // Value Edit
    SAVE_AND_CLOSE,
    TAP_TO_EDIT,
    BTN_BACK,

    // Service
    VERSION,
    UPTIME,
    FREE_HEAP,

    // Touch Cal
    TOUCH_CAL_TITLE,
    TOUCH_CAL_TAP_N,
    TOUCH_CAL_TOUCH_TARGET,

    // Modes
    MODE_IDLE,
    MODE_RECTIFICATION,
    MODE_DISTILLATION,
    MODE_MANUAL_RECT,
    MODE_MASHING,
    MODE_HOLD,

    // Phases
    PHASE_IDLE,
    PHASE_HEATING,
    PHASE_STABILIZATION,
    PHASE_HEADS,
    PHASE_POST_HEADS_STAB,
    PHASE_BODY,
    PHASE_TAILS,
    PHASE_PURGE,
    PHASE_FINISH,
    PHASE_COMPLETED,
    
    // Units
    UNIT_W,
    UNIT_MM,
    UNIT_L,
    UNIT_MIN,
    UNIT_ML_H,
    UNIT_ML_H_K,
    UNIT_ML_R,
    UNIT_PERCENT
};

inline const char* msg(Msg id) {
    bool ru = (g_settings.language == 0);
    
    switch (id) {
        // Tabs
        case Msg::MONITOR:       return ru ? "Монитор" : "Monitor";
        case Msg::CONTROL:       return ru ? "Управление" : "Control";
        case Msg::SETTINGS:      return ru ? "Настройки" : "Settings";
        case Msg::SERVICE:       return ru ? "Сервис" : "Service";

        // Dashboard
        case Msg::CUBE_TEMP:     return ru ? "КУБ ТЕМП" : "CUBE TEMP";
        case Msg::HEATER_POWER:  return ru ? "МОЩНОСТЬ" : "HEATER POWER";
        case Msg::TOP_T:         return ru ? "ВЕРХ Т" : "TOP T";
        case Msg::REFLUX_T:      return ru ? "ДЕФ Т" : "REFLUX T";
        case Msg::COLUMN_BOTTOM: return ru ? "НИЗ К Т" : "BTM T";
        case Msg::WATER_IN:      return ru ? "ОХЛ ВХ" : "WTR IN";
        case Msg::WATER_OUT:     return ru ? "ОХЛ ВЫХ" : "WTR OUT";
        case Msg::PUMP:          return ru ? "НАСОС" : "PUMP";
        case Msg::TSA_T:         return ru ? "ТСА Т" : "TSA T";

        // Control
        case Msg::AUTO_RECTIFY:  return ru ? "Авто Ректиф" : "Auto Rectify";
        case Msg::DISTILLATION:  return ru ? "Дистилляция" : "Distillation";
        case Msg::MANUAL_RECT:   return ru ? "Ручная Рекст" : "Manual Rect";
        case Msg::MASHING:       return ru ? "Затирка" : "Mashing";
        case Msg::HOLD_MODE:     return ru ? "Удержание" : "Hold Mode";
        case Msg::MANUAL_PUMP:   return ru ? "Ручной Насос" : "Manual Pump";
        case Msg::STOP:          return ru ? "СТОП" : "STOP";
        case Msg::PAUSE:         return ru ? "ПАУЗА" : "PAUSE";
        case Msg::RESUME:        return ru ? "ПУСК" : "RESUME";

        // Settings
        case Msg::EQUIPMENT:     return ru ? "Оборудование" : "Equipment";
        case Msg::RECT_PARAMS:   return ru ? "Параметры Рек" : "Rect Params";
        case Msg::DIST_PARAMS:   return ru ? "Параметры Дис" : "Dist Params";
        case Msg::CALIBRATION:   return ru ? "Калибровка" : "Calibration";
        case Msg::THEME:         return ru ? "Тема" : "Theme";
        case Msg::SOUND:         return ru ? "Звук" : "Sound";
        case Msg::LANGUAGE:      return ru ? "Язык" : "Language";

        // Equipment
        case Msg::COLUMN_HEIGHT: return ru ? "Высота колонны" : "Column Height";
        case Msg::CUBE_VOLUME:   return ru ? "Объем куба" : "Cube Volume";
        case Msg::PACKING_COEFF: return ru ? "Коэф. насадки" : "Packing Coeff";

        // Rectification
        case Msg::HEADS_PERCENT: return ru ? "Головы %" : "Heads %";
        case Msg::HEADS_SPEED:   return ru ? "Скор. голов" : "Heads Speed";
        case Msg::BODY_SPEED:    return ru ? "Скор. тела" : "Body Speed";
        case Msg::STABILIZATION: return ru ? "Стабилизация" : "Stabilization";
        case Msg::PURGE_TIME:    return ru ? "Время промывки" : "Purge Time";

        // Distillation
        case Msg::DIST_SPEED:    return ru ? "Скор. дистил" : "Dist Speed";
        case Msg::HEADS_VOLUME:  return ru ? "Объем голов" : "Heads Volume";
        case Msg::TARGET_VOLUME: return ru ? "Целевой объем" : "Target Volume";
        case Msg::END_TEMP:      return ru ? "Темп. конца" : "End Temp";

        // Calibration
        case Msg::PUMP_CALIBRATION:  return ru ? "Калибр. насоса" : "Pump Calibr";
        case Msg::TOUCH_CALIBRATION: return ru ? "КАЛИБР. ТАЧА" : "TOUCH CALIBRATION";

        // Manual
        case Msg::MANUAL_MODE:   return ru ? "Ручной Режим" : "Manual Mode";
        case Msg::VALVE_WATER:   return ru ? "ВОДА" : "WATER";
        case Msg::VALVE_HEADS:   return ru ? "ГОЛОВЫ" : "HEADS";
        case Msg::VALVE_UNO:     return ru ? "УНО" : "UNO";

        // Value Edit
        case Msg::SAVE_AND_CLOSE: return ru ? "СОХРАНИТЬ И ВЫЙТИ" : "SAVE AND CLOSE";
        case Msg::TAP_TO_EDIT:    return ru ? "НАЖМИТЕ ДЛЯ ПРАВКИ" : "TAP VALUE TO EDIT";
        case Msg::BTN_BACK:       return ru ? "НАЗАД" : "BACK";

        // Service
        case Msg::VERSION:       return ru ? "Версия" : "Version";
        case Msg::UPTIME:        return ru ? "Аптайм" : "Uptime";
        case Msg::FREE_HEAP:     return ru ? "Свободно" : "FreeHeap";

        // Touch Cal
        case Msg::TOUCH_CAL_TITLE:        return ru ? "КАЛИБРОВКА" : "TOUCH CAL";
        case Msg::TOUCH_CAL_TAP_N:        return ru ? "Нажмите %u раз" : "Tap screen %u times";
        case Msg::TOUCH_CAL_TOUCH_TARGET: return ru ? "Коснитесь мишени" : "Touch the target";

        // Modes
        case Msg::MODE_IDLE:          return ru ? "Ожидание" : "Idle";
        case Msg::MODE_RECTIFICATION: return ru ? "Ректификация" : "Rectification";
        case Msg::MODE_DISTILLATION:  return ru ? "Дистилляция" : "Distillation";
        case Msg::MODE_MANUAL_RECT:   return ru ? "Ручная Рекст." : "Manual Rect.";
        case Msg::MODE_MASHING:       return ru ? "Затирка" : "Mashing";
        case Msg::MODE_HOLD:          return ru ? "Удержание" : "Hold Mode";

        // Phases
        case Msg::PHASE_IDLE:           return ru ? "Ожидание" : "Idle";
        case Msg::PHASE_HEATING:        return ru ? "Нагрев" : "Heating";
        case Msg::PHASE_STABILIZATION:  return ru ? "Стабилизация" : "Stabilization";
        case Msg::PHASE_HEADS:          return ru ? "Отбор голов" : "Heads Selection";
        case Msg::PHASE_POST_HEADS_STAB: return ru ? "Стаб. после голов" : "Post-heads Stab.";
        case Msg::PHASE_BODY:           return ru ? "Отбор тела" : "Body Selection";
        case Msg::PHASE_TAILS:          return ru ? "Отбор хвостов" : "Tails Selection";
        case Msg::PHASE_PURGE:          return ru ? "Продувка" : "Purge";
        case Msg::PHASE_FINISH:         return ru ? "Завершение" : "Finish";
        case Msg::PHASE_COMPLETED:      return ru ? "Завершено" : "Completed";

        // Units
        case Msg::UNIT_W:        return ru ? "Вт" : "W";
        case Msg::UNIT_MM:       return ru ? "мм" : "mm";
        case Msg::UNIT_L:        return ru ? "л" : "L";
        case Msg::UNIT_MIN:      return ru ? "мин" : "min";
        case Msg::UNIT_ML_H:     return ru ? "мл/ч" : "ml/h";
        case Msg::UNIT_ML_H_K:   return ru ? "мл/ч/к" : "ml/h/k";
        case Msg::UNIT_ML_R:     return ru ? "мл/об" : "ml/r";
        case Msg::UNIT_PERCENT:  return ru ? "%" : "%";

        default: return "";
    }
}

#endif // LOCALIZATION_H
