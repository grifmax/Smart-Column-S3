"""Source-level characterization of equipment status JSON before C-013.

Run: python scripts/test_status_settings_contract.py
This compares assignments in the actual REST, WebSocket and cloud builders.
It is not a runtime/device integration test.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
ASSIGNMENT = re.compile(r'(\w+)\["([A-Za-z0-9_]+)"\]\s*=\s*([^;]+);')

EQUIPMENT = {
    'heaterPowerW', 'columnHeightMm', 'packingCoeff', 'cubeVolumeL',
    'minHeaterSubmergeL', 'waterAutoStartCubeTempC', 'boosterHeaterEnabled',
    'boosterHeaterPowerW', 'boosterHeaterStopCubeTempC', 'coolingPwmEnabled',
    'coolingPwmHoldPercent', 'coolingPwmPullMs', 'coolingPwmCurrentDuty',
    'useDs2482ForTemps', 'ds2482Address', 'tempBusGpioPin',
    'temperatureBusSource', 'temperatureBusSourceLabel',
}
CLOUD = {
    'heaterPowerW', 'cubeVolumeL', 'minHeaterSubmergeL',
    'waterAutoStartCubeTempC', 'boosterHeaterEnabled', 'boosterHeaterPowerW',
    'boosterHeaterStopCubeTempC', 'coolingPwmEnabled',
    'coolingPwmHoldPercent', 'coolingPwmPullMs', 'coolingPwmCurrentDuty',
}
SAFETY = {'pressureMaxMmHg', 'tsaMaxC', 'waterOutMaxC',
          'waterOutRiseRateCMin', 'pressureRiseRateMmHgMin'}
ALIASES = {
    'min_heater_submerge_l', 'water_auto_start_cube_temp_c',
    'booster_heater_enabled', 'booster_heater_power_w',
    'booster_heater_stop_cube_temp_c', 'cooling_pwm_enabled',
    'cooling_pwm_hold_percent', 'cooling_pwm_pull_ms',
    'cooling_pwm_current_duty', 'safety_pressure_max_mmhg',
    'safety_tsa_max_c', 'safety_water_out_max_c',
    'safety_water_out_rise_rate_c_min', 'safety_pressure_rise_rate_mmhg_min',
}


def block(source, start, end):
    if source.count(start) != 1:
        raise AssertionError(f'ambiguous start: {start}')
    pos = source.index(start)
    stop = source.find(end, pos)
    if stop < 0:
        raise AssertionError(f'missing end: {end}')
    return source[pos:stop]


def assignment_map(text, names):
    result = {}
    for match in ASSIGNMENT.finditer(text):
        variable, key, expr = match.groups()
        if variable not in names:
            continue
        full_key = (names[variable], key)
        if full_key in result:
            raise AssertionError(f'duplicate assignment: {full_key}')
        result[full_key] = re.sub(r'\s+', '', expr)
    return result


def status_block(source, doc, equipment, safety):
    start = f'JsonObject {equipment} = {doc}["equipment"].to<JsonObject>();'
    end_marker = f'StateSafetySettingsJson::fillAliases({doc}.as<JsonObject>(), g_settings.safety);'
    if source.count(start) != 1:
        raise AssertionError(f'ambiguous start: {start}')
    begin = source.index(start)
    end = source.index(end_marker, begin) + len(end_marker)
    text = source[begin:end]
    result = assignment_map(text, {equipment: 'equipment', safety: 'safety', doc: 'alias'})
    call = f'StateSafetySettingsJson::fill({safety}, g_settings.safety);'
    if text.count(call) != 1:
        raise AssertionError(f'missing/duplicate shared safety builder: {call}')
    helper = (ROOT / 'src/interface/state_safety_settings_json.h').read_text(encoding='utf8')
    nested = block(helper, 'inline void fill(JsonObject obj, const Safety& safety)',
                   'inline void fillAliases(')
    flat = helper[helper.index('inline void fillAliases('):]
    alias_call = f'StateSafetySettingsJson::fillAliases({doc}.as<JsonObject>(), g_settings.safety);'
    if text.count(alias_call) != 1:
        raise AssertionError(f'missing/duplicate safety aliases builder: {alias_call}')
    for section, source_part in (('safety', nested), ('alias', flat)):
        for key, expression in assignment_map(source_part, {'obj': section}).items():
            if key in result:
                raise AssertionError(f'duplicate assignment: {key}')
            result[key] = expression.replace('safety.', 'g_settings.safety.', 1)
    return result


class StatusSettingsContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rest = (ROOT / 'src/interface/api/api_status.cpp').read_text(encoding='utf8')
        ws = (ROOT / 'src/interface/web_live.cpp').read_text(encoding='utf8')
        cloud = (ROOT / 'src/interface/cloud_tunnel.cpp').read_text(encoding='utf8')
        cls.rest = status_block(rest, 'doc', 'equipment', 'safetySettings')
        cls.fast = status_block(ws, 'fastDoc', 'fastEquipment', 'fastSafetySettings')
        cls.full = status_block(ws, 'doc', 'equipment', 'safetySettings')
        cloud_block = block(cloud, 'static void fillEquipmentJson(JsonObject equipment) {',
                            'static String base64Encode(')
        cls.cloud = assignment_map(cloud_block, {'equipment': 'equipment'})

    def test_rest_ws_key_sets_and_expressions(self):
        expected = ({('equipment', x) for x in EQUIPMENT} |
                    {('safety', x) for x in SAFETY} |
                    {('alias', x) for x in ALIASES})
        self.assertEqual(expected, set(self.rest))
        self.assertEqual(self.rest, self.fast)
        self.assertEqual(self.rest, self.full)

    def test_cloud_is_exact_equipment_subset_with_same_expressions(self):
        self.assertEqual({('equipment', x) for x in CLOUD}, set(self.cloud))
        self.assertEqual(self.cloud, {k: self.rest[k] for k in self.cloud})

    def test_aliases_are_paired_with_nested_fields(self):
        pairs = {
            'min_heater_submerge_l': ('equipment', 'minHeaterSubmergeL'),
            'water_auto_start_cube_temp_c': ('equipment', 'waterAutoStartCubeTempC'),
            'booster_heater_enabled': ('equipment', 'boosterHeaterEnabled'),
            'booster_heater_power_w': ('equipment', 'boosterHeaterPowerW'),
            'booster_heater_stop_cube_temp_c': ('equipment', 'boosterHeaterStopCubeTempC'),
            'cooling_pwm_enabled': ('equipment', 'coolingPwmEnabled'),
            'cooling_pwm_hold_percent': ('equipment', 'coolingPwmHoldPercent'),
            'cooling_pwm_pull_ms': ('equipment', 'coolingPwmPullMs'),
            'cooling_pwm_current_duty': ('equipment', 'coolingPwmCurrentDuty'),
            'safety_pressure_max_mmhg': ('safety', 'pressureMaxMmHg'),
            'safety_tsa_max_c': ('safety', 'tsaMaxC'),
            'safety_water_out_max_c': ('safety', 'waterOutMaxC'),
            'safety_water_out_rise_rate_c_min': ('safety', 'waterOutRiseRateCMin'),
            'safety_pressure_rise_rate_mmhg_min': ('safety', 'pressureRiseRateMmHgMin'),
        }
        self.assertEqual(ALIASES, set(pairs))
        for alias, nested in pairs.items():
            with self.subTest(alias=alias):
                self.assertEqual(self.rest[('alias', alias)], self.rest[nested])


if __name__ == '__main__':
    unittest.main(verbosity=2)
