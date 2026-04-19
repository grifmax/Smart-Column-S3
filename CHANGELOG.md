# ������ ���������

��� �������� ��������� � ���� ������� ����� ��������������� � ���� �����.

������ ������� �� [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
� ���� ������ �������������� [Semantic Versioning](https://semver.org/lang/ru/).

---

---

## [2.2.25] - 2026-04-19

### Исправлено

- Восстановлен русский текст на встроенном TFT: display.cpp возвращён из битой mojibake-кодировки в нормальный UTF-8, а рендер LovyanGFX снова работает через штатный UTF-8 pipeline без промежуточного CP1251-адаптера. (codex)

---

## [2.2.24] - 2026-04-19

### ����������

- ��������� ������� `v2.2.23`, ��� ����� ������� ����� �� ���������� TFT ����� `?`: UTF-8 -> CP1251 ������� � `display.cpp` ������ ���������� ��� ������� ����������� CP1251-������ ��� ����, ������ ������ ������ �� `?`. (codex)
- ���������� IPS ������ ��������� ������ ��������� ����� ���������� ������: ������� UTF-8 ������ �� `display.cpp` � legacy CP1251 literals/escapes �� ����������� � ������� TFT-����. (codex)

---

## [2.2.23] - 2026-04-19

### ����������

- � `display.cpp` �������� ��������� UTF-8 -> CP1251 ������� ��� ����������� TFT: ��� `textWidth/drawString` � HMI-���� ������ ����������� ����� ������ encoder ����� ������� � LovyanGFX. (codex)
- ���������� ���������� �� ������� �������� ����������� IPS: status bar, tabs, dashboard, mode monitor, service/settings � calibration overlays ������ ������ ��������� � ��� �������� �������, ������� ������� ������� TFT-������������. (codex)

---

## [2.2.22] - 2026-04-19

### ��������

- ��������� ���������� �������� ����� TFT summary-������ �� `DASHBOARD` ��� `MANUAL_RECT`, `MASHING` � `HOLD`: � ������ ������������ ������� hero ������ ����� �������� ������, � ����/��������/������� �������� � compact-������. (codex)
- ��� `MASHING` � `HOLD` hero-���� ������ ���������� ������� ����������� � ������� ���� � ���������, � ���� � ������ ���� � ������ ����, ����� �������� ����� ������ ������� ��������� �������� ������. (codex)

---

## [2.2.21] - 2026-04-19

### ���������

- �������� HMI-������� `SparklineBuffer` (������������) � `HMIIndicators` (����� ��������) ��� TFT-������� �� ��������� ISA-101. (gemini)
- ����������� ���������������� (Mutex) ������ � ��������� ������� �������� ��� ������������� ��������� ������ ��� ������������ FreeRTOS. (gemini)

### ��������

- ���������� ���������� ����� TFT HMI-�������� � `display.cpp`: ������ ����� ����� �������� � `SparklineBuffer`, `HMIIndicators` � ����������� value-tile helper-��� ��� ��������� ������������� �������. (codex)
- ����� `DASHBOARD` �� ���������� TFT ������ �� �������� ������ �� ������ ��� `DISTILLATION/NBK/FERMENTATION`, �� � ��� `MANUAL_RECT`, `MASHING`, `HOLD`: � ������� ������ ������ ���� hero-���� � ���� support-������ ������ generic `BODY`-������. (codex)

## [2.2.20] - 2026-04-18

### Changed

- Reworked the built-in TFT `DASHBOARD` left summary panel into the same hero-style operator layout already used by the updated process screens: one dominant block plus three compact support rows. (codex)
- Simplified the idle and active dashboard summary cards so the operator lands on the next action or main collected volume first, with mains/pressure/cooling and fraction support data moved below. (codex)

---

## [2.2.19] - 2026-04-18

### Changed

- Reworked the left runtime summary panels on the built-in TFT for `RECTIFICATION`, `DISTILLATION`, and `NBK`: each screen now has one large primary indicator and three smaller support rows instead of five equal-weight rows. (codex)
- Promoted the most important operator values in those left panels to dedicated hero blocks: `BODY` for rectification, `COLLECT / TARGET` for distillation, and `COLLECT / TARGET` for NBK. (codex)

---

## [2.2.18] - 2026-04-18

### Changed

- Rebalanced the built-in TFT runtime hierarchy so the main process numbers on `DASHBOARD`, `MONITOR`, and custom mode-monitor screens render larger, while support metrics stay quieter. (codex)
- Tightened the metric tile chrome for runtime screens by slimming the tile header strip and muting the root footer text, so the eye lands on values first and status copy second. (codex)

---

## [2.2.17] - 2026-04-18

### Changed

- Tightened TFT monitor geometry to the same 3 px gap system: the root monitor, rectification monitor, custom monitor grids, and right-side live metric tiles now sit with denser spacing and a smaller gap between summary and metric panels. (codex)
- Synced monitor touch geometry with the denser layout, including the distillation mode-monitor edit tiles, manual-rect rows, mash/hold step lists, and the rectification monitor tap-zone for `All Temps`. (codex)

---

## [2.2.16] - 2026-04-18

### Changed

- Tightened TFT button geometry to a 3 px gap standard on the built-in IPS panel: `CONTROL`, footer tabs, mode-switch confirmation, `SETTINGS` quick toggles, `MANUAL` valve buttons, and `VALUE_EDIT` step buttons now sit with minimal spacing. (codex)
- Synced the corresponding touch hitboxes for `CONTROL`, `SETTINGS`, `MANUAL`, `VALUE_EDIT`, and the mode-switch overlay so the tighter layout still matches the rendered buttons exactly. (codex)

---

## [2.2.15] - 2026-04-17

### Changed

- Unified the remaining TFT settings/service copy behind a local `tftText()` dictionary so compact labels, headers, and footer hints no longer drift between mixed RU/EN variants. (codex)
- Tightened the secondary TFT screens: `SETTINGS`, `RECT_PARAMS`, `CALIBRATION`, `MANUAL`, `VALUE_EDIT`, and `SERVICE` now use shorter panel titles and more compact operator hints. (codex)
- Fixed the manual-lock overlay text on TFT so it describes access policy in plain operator language instead of raw `IDLE/MANUAL` state names. (codex)

---

## [2.2.14] - 2026-04-17

### Changed

- Fixed TFT SETTINGS hitboxes so the touch zones match the new 2x2 HMI layout for cards and bottom toggles. (codex)
- Resynced release versioning after 2.2.13: firmware/docs/web assets and version stamps now point to the same release again. (codex)
- Removed the temporary local helper _remove_dead.ps1 after the dead-code cleanup in renderRectParams(). (codex)

---

## [2.2.13] - 2026-04-17

### ��������

- �������� ����� `SETTINGS` �� TFT ����������� � ������� ��������� �����: ������ ������ ����� ������������� ������ ������ ������ ������������� �������� (`drawCard` + `drawPanelHeader` + `drawCompactKeyValueRow`), ������ ���������� ���������� �������� ������ ������� (��������/������ �������, ��� �����/������� �/�/�, ��������/���� �����������, ���������� ������/����). ��� ������� ������������� ����/����/���� ���������� ������� ��������� ����� � ����� ������. (antigravity)
- ��������� 7 ����� ������ ����������� � `localization.h`: `FEEDSTOCK`, `FEED_VOLUME`, `FEED_ABV`, `BODY_PERCENT`, `TAILS_PERCENT`, `BODY_TO_TAILS`, `TAILS_FINISH` � ��� RU/EN ��� �� ����� ������. (antigravity)
- � `renderRectParams()` ��� ������� EN-������ (`"FEEDSTOCK"`, `"FEED VOL"`, `"FEED ABV"`, `"BODY %"`, `"TAILS %"`, `"BODY -> TAILS*"`, `"TAILS FINISH*"`, `"PAGE: TECH / CUTS"`, `"PAGE: PROFILE / SPEED"`, `"Tap feedstock to rotate default cuts"`) �������� �� ������ `msg(Msg::...)`. (antigravity)
- ����� ������ ���� ���� (~56 ����� `drawValueRow`-������) � `renderRectParams()`, ������� ������� �� ���������� ��-�� `return;` ����. (antigravity)
- ������ �������� ������� �� `2.2.13`. (antigravity)

---

## [2.2.12] - 2026-04-17

### ��������

- ��������� `EQUIPMENT` � `DIST_PARAMS` �� TFT ���������� �� ������ ������� ����� �� ������� 2x2 ��������� �����: ������ ������������� value-������ � ����������� hitbox ��� ����� layout. (codex)
- ����� `RECT_PARAMS` �� TFT �������� �� ��� �� HMI-����: ������� page-strip ������ ��� `TECH / CUTS` � `PROFILE / SPEED`, ����� ���������� ������ ���������� �� �������� � ��������� derived-tiles ��� pressure-compensated `BODY -> TAILS*` � `TAILS FINISH*`. (codex)
- ������ �������� ������� �� `2.2.12`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.11] - 2026-04-17

### ��������

- ����� `CALIBRATION` �� TFT �������� �� ����� ������� 2-panel layout: ����� ������ ������ ���������� ������, ������ ��������� ������� ������ touch-����������, � ����������� hitbox ��� ����� �����. (codex)
- ����� `VALUE_EDIT` �� TFT ������� � ���� �� HMI-�����: ������� value-������, ���������� ��� ������� ������ � ��������� ������ ������ ���������� ������ ������� ������� editor-card layout. (codex)
- ������ �������� ������� �� `2.2.11`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.10] - 2026-04-17

### ��������

- ����� `MANUAL` �� ���������� TFT ��������� � ����� ������� HMI-���: ��� ������� live-������ `��������/�����` ������ � ��������� ��� ������� ��������� ������ �����, � ����������� hitbox ��� ����� layout. (codex)
- ����� `SERVICE` �� TFT �������� � ������� ������ �� 2x2 diagnostics-������ (`������`, `uptime`, `heap`, `TFT frame`) + ������ diag-����, ����� �������� ��������� �������� �������� ��� ��������� ����������, � �� ��� ������� ������ �����. (codex)
- ������ �������� ������� �� `2.2.10`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.9] - 2026-04-17

### ��������

- ��� ����������� TFT �������� ���������� HMI-�������: ������ � ���� ������ ������������ ��������� ��������� (`������.`, `������.`, `������.`, `����. ����.`), ����� status bar, mode buttons � summary-������ �������� ��� ������ ����������. (codex)
- �� IPS ��������� ����� ������� ������� ������� � ��������� � `CONTROL`, `RECT_PARAMS`, `CALIBRATION`, `SERVICE`, `ALL TEMPS` � fallback-monitor: ����� ���� ����� � ��������� ������, � �� � verbose debug-����. (codex)
- ������ �������� ������� �� `2.2.9`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.8] - 2026-04-17

### ��������

- ��� ����������� TFT �������� ����� UTF-8-safe text-fit ����: ������� ������ ������ ��������� ���������/����������� � `header`, `buttons`, `tabs`, `value rows`, `panel headers`, `value tiles`, `badges`, `footer hints` � root footer ������ ��������� ���� �� �����. (codex)
- �������� ��������� � �������� ������� �� IPS ������ ����� ��������� ������� ������� �������� � ������� ��� ������� ������� ������� ������ �� �����. (codex)
- ������ �������� ������� �� `2.2.8`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.7] - 2026-04-17

### ��������

- �� TFT ����� modal/runtime ����: ������������� ����� ������ ���������� �� ����� panel-overlay, � locked-state ������� ������ ������ �� �������� ������ ��������� ������. (codex)
- ����� `SERVICE` ������ ����� ������������ �������� ������� TFT: ��� slow/watchdog/hard recovery ������ ������ ������ ��� � ������� �� ����-�� �� ���, � ��� ������ ��������. (codex)
- ������ �������� ������� �� `2.2.7`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.6] - 2026-04-17

### ��������

- ��������� TFT-������ ���������� �� ��� �� HMI-����: touch-calibration ������� ��������� ���������� � ������������� �������, boot splash ������ �� �������� ��� ����� debug-�����, � `showMessage`/`showError` ���������� ����� fullscreen-overlay ������ ������ ������ �������. (codex)
- ��������� ��������� � �������� ��������� ��������� �� ���������� IPS ������ �������� ����� ����� overlay/helper-����, ����� ��������� � ��������� ������ �� �������� �� ������ �������������� �����. (codex)
- ������ �������� ������� �� `2.2.6`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.5] - 2026-04-17

### ��������

- ��������� TFT-������ `SETTINGS` � `SERVICE` ���������� �� ��� �� ������������� HMI-����: ��������� ������ ����������, ���������� ��������� ���������, ����� ������������� �������� ������������ � ����� ������� ����� �������������� ��������. (codex)
- ����� `ALL TEMPS` ���� ��� �������� ������ `480x320` � ������ ����������, ����� ������������� ����� ��������� �������� �� footer ����������� IPS. (codex)
- ������ �������� ������� �� `2.2.5`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.4] - 2026-04-17

### ��������

- ������� TFT-����� ��������� � ����� ������ HMI-����: ��������� �������, ������� ������, ����, header, ��������, value-tiles � ����� ������������� ������, ����� ����� �������� ��������� ��� ����������� web-��������. (codex)
- ����� ���������� `CONTROL` � �������� ����� `SETTINGS` ������ ���������� ����� ������������������ �������� ��������� ������� � ������ ������ ������� ������ ��������� ��������. (codex)
- ����� ��������� ����������� IPS ������ �������� ������ ������������ ���� ������/������ � �������� ����� ���������, ����� ������ ������ ������� ��� ������������ �����, � �� ��� ������������ ��������. (codex)
- ������ �������� ������� �� `2.2.4`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.3] - 2026-04-17

### ��������

- ��������� HMI-�������� ����������� IPS UI ��� ���������� custom-�������: `MANUAL_RECT`, `MASHING` � `HOLD` ���������� �� ��� �� ������� root-monitor ������ � ������������� summary-������� � ����� status/footer ������. (codex)
- �� TFT ��� ������ ������������ ��������� ��������� ������������ ������ �� ��������, �������� � ��������, � ����� ���������� �������� � ������ ����� ����������, �������� � ������. (codex)
- ��� ������� � ������������ ������ ������������� step-list ����������� � HMI-���������� � ����� ������� �������� ����, ���� � ������� � ������ ������� �������� � ����������. (codex)
- ������ �������� ������� �� `2.2.3`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.2] - 2026-04-17

### ��������

- ��������� HMI-�������� ����������� IPS UI ��� `custom monitor`: ������ `DISTILLATION`, `NBK` � `FERMENTATION` ���������� �� ��� �� ������������ ������ � ����� summary-������� � ������ 2x3 ������ �������� ����������. (codex)
- ����� root-monitor ������ ��� ������������� ������� ������ ���������� ������ status/footer ������, ����� `dashboard`, `rect monitor` � ��������� runtime-������ �������� ��� ���� HMI-�������. (codex)
- ��� �����������, ��� � ����������� �� TFT ��������� ��������� summary-����� �� �����, ������, ��������, ������� � ������� ������, ��� �������� � ������� ���������� layout. (codex)
- ������ �������� ������� �� `2.2.2`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.1] - 2026-04-17

### ��������

- ��������� HMI-�������� ����������� IPS UI: `dashboard` � �������� `mode monitor` ��� ������������ ���������� � ������� �������� �� ����� ������� ������������ ���������� � ����� summary-������� � ������ 2x3 ������ �������� ����������. (codex)
- �� ������� ������ IPS ��������� ��������� summary-���� ��������� ��������, ��� �������� ������������ �����, �������, ��������/���������� � safety-state ��� ������ ����� ��� �������� �������. (codex)
- ����� �������� ������������ �� TFT ������ ���������� ��� �� ������������� HMI-������, ��� � ������� �����: �������� stage-header, ���������� ������ �� �������/����/������� � ����� ������� ������ ����� ���������� � ��������. (codex)
- ������ �������� ������� �� `2.2.1`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.2.0] - 2026-04-16

### ��������

- ���������� IPS UI ����������� � ������ HMI-����: ������� ������, ������, value-tiles, progress bar � ��������� ������ ���������� �� ������� ������������� ����� ��� ���������� � ������������ ��������. (codex)
- ����� ������ ������� �� TFT ������ ��������� ���� ����������� ����� ������� �������: ������������, �����������, ������ �����, �������, ������������, ��� � �����������; ��������� action-������ ����� � ����� �������� � ������� ������. (codex)
- ��� ����������� ������ ��������� ��������� monitor-layouts ��� `NBK` � `FERMENTATION`, � phase/status ������ ������ ���������� ���������� ����� ��� ��� ���� �������, � �� ������ `rectPhase`. (codex)
- ������ ���������� �� IPS ������ ��������� � ������������� ����������� ��� `IDLE`/`MANUAL_RECT`, ����� �� ���� ������ ������� policy ����� ��������� ���-���������. (codex)
- ������ �������� ������� �� `2.2.0`; frontend bundle ����� ���������� � version-stamps ���������������� � ����� �������. (codex)

## [2.1.21] - 2026-04-16

### ��������

- ��� `POST /api/stirrer/start`, `POST /api/stirrer/set` � `POST /api/stirrer/stop` ������������� ������ policy ������� ����������: ������� �������� ������ � `IDLE`, � ��� �������� ��� ������������ �� ����� �������� backend ���������� `409` � ����� ��������. (codex)
- ������� ������ ������� ������ ������� ���������� ownership FSM � �������� �������, ��������� ������ ������ � ������� ������� ���������� ��� ������� ������� � API. (codex)
- �������� UI smoke-��������, ������� ��������� ���������� ������� ���������� �������� �� ����� ��������� ��������. (codex)
- ������ �������� ������� �� `2.1.21`; frontend bundle ����������, `data/version.json` � HTML-asset stamps ���������������� � ����� �������. (codex)

## [2.1.20] - 2026-04-15

### ���������

- �� ������� �������� �������� live-������ ������� ����: ������� ��������, ����� ������, ������ MCP4725 � ������ ������� `start/set/stop` ����� ������������ REST API. (codex)
- �� ������� `������������ -> ���������` ��������� ��������� �������� �������� ������� � ������ `enabled`, ��������� �� ��������� � auto-start ��� �������, ��� � �����������. (codex)
- � `������������ -> ������������` �������� ��������� �������� ������� � ������ ��������, ������ �������� � ��������� backend endpoint `POST /api/testing/stirrer`. (codex)
- ��������� UI smoke-�������� ��� �������: ������ ������� ������, ���������� �������� � ��������� ���� ������� � workspace ������������. (codex)

### ��������

- `GET /api/testing/status` � ����� `POST /api/testing/stop-all` ������ ��������� ������� � �������� ��������� ������ � live-������� ������������. (codex)
- ������ �������� ������� �� `2.1.20`; frontend bundle ����������, `data/version.json` � HTML-asset stamps ���������������� � ����� �������. (codex)

## [2.1.19] - 2026-04-15

### ���������

- � `src/interface/webserver.cpp` ����������� REST endpoints �������: `POST /api/stirrer/start`, `POST /api/stirrer/stop`, `POST /api/stirrer/set`, � ����� `GET/POST /api/settings/stirrer` ��� ������ � ���������� ������������. (codex)
- ������ `GET /api/status` � WebSocket broadcast ������ �������� ���������� ���� `stirrer`, ������������������ ����� ����� ������������� ������, ����� ������ ������� �� ����� ���������� ���� `loop()`. (codex)

### ��������

- � `src/storage/nvs_manager.cpp` ��������� ���������� �������� ������� � NVS: `enabled`, `defaultSpeedPercent`, `autoMashing`, `autoFermentation`, `autoNbk` ������ ����������� � ����������� ������ � ���������� �����������. (codex)
- ������ API-������� ������� ������ ��������� � � manual override, ��������� `autoMode`, � ���������� ������� � ���������� ����� �������� `Stirrer::stop()` ��� �������������� ��������� ������. (codex)
- ������ �������� ������� �� `2.1.19`, ���������� frontend bundle � ��������� version-stamps � `data/version.json` � ����������� HTML-�������. (codex)

## [2.1.18] - 2026-04-14

### ���������

- ���������� ������� ���������� �������� ���� ����� ���������� ������ 0-10� �� ���� **MCP4725** (12-��� DAC, I2C) � ������������� ��������� **MCP6001** (�������� ?3, 0�3.3� > 0�10�). (gemini)
- ��������� ��������� `StirrerState` � `StirrerSettings` � `types.h` ��� �������� ��������� � �������� �������. (gemini)
- ����� ������� `src/drivers/stirrer.h/.cpp`: ������������� MCP4725 �� I2C, ��������� �������� (0�100%), �����/����, ������������� � `g_state.stirrer`. (gemini)
- ��������� ���������� `adafruit/Adafruit MCP4725@^2.0.2` � ����������� `platformio.ini`. (gemini)
- ��������� NVS-����� ��� �������� ������� (`stir_en`, `stir_spd`, `stir_amash`, `stir_aferm`, `stir_anbk`) � `config.h`. (gemini)
- ��������� ��������� `I2C_ADDR_MCP4725 0x60` � `config.h`. (gemini)
- ����-������ ������� � FSM (`fsm.cpp`) ��� ��������� �������: ��������� (`autoMashing`), ����������� (`autoFermentation`), ��� (`autoNbk`). (gemini)
- ������� ��������������� ����� `Stirrer::stop()` ��� ������ �� ������ ������ (`finalizeModeStop`) � ��� ������������ ��������� ������ (`forceSafeOutputs` � `safety.cpp`). (gemini)
- ���������� ������� (`running`, `speed`, `available`, `autoMode`) ��������� � WebSocket broadcastState. (gemini)

## [2.1.17] - 2026-03-18

### ��������

- �� ������� `���������` ��������� ��������� ������������ ���������� � ��������� ����� sidebar �� desktop: ������ �������� ������ �������� ��� ������ ������� ������� � ������ �� �������� ��� ������� �����. (codex)
- �� ������� `������������ -> ������������` ������ ������� ������ ������������� ���������� ������ ��������� ��������� ����, � ����� ������ ������������ � ������ ��������� �������� ���������� � ����� ������� ������� �� desktop, ����� �� ��������� ������� ������� ������� ��������. (codex)
- Smoke-�������� `equipment-testing` ������� ��� ����� �������� �������� ���� � ������ ��������� ������������ ����� �������, ������������� � ��������� ����� sidebar, � �� ����� ������ ������ ����� ��������. (codex)

## [2.1.16] - 2026-03-17

### ��������

- �� ������� `������������` ������������ ������� ���������� (`��������� / ���������� / ������������`) ������ ����� ���������� ������ ���� ��������� ������� ����, ������ ������� �������� ��������; ��� ����� ��������� ����� ���������� ������ ����������� ���� ������ ����� �������. (codex)
- �� ������� `���������` ������ ��� �� workbench-�������, ��� ��� ������������ � `������������`: �������� ��������� submenu �� �������� `����������� / ���������� / ������ / ��������� / �������`, � ������ ������� ������� �� desktop ������ ������ ���� ���������� ���� � ������� ����������, ������ ������� ����� ���� �������� �����. (codex)
- ��������� `��������` � `������������` ������������� �� ����� UI-�������, ������� ��������� desktop/mobile ������ ����� � `������������`: �� ����� ������� ����� �������� ��� ����������, � �� ������� � ��� service workbench � ���������� ����. (codex)

## [2.1.15] - 2026-03-17

### ��������

- ��������� ����� ��������� � ��������� ��������� � ������� `�������/����`: ��������� `�������` �� ����� ������� ����������, � ��� `charts.html` � `logs.html` �������� ��������� fallback `toggleTopMenu`, ����� ������ ���������� ����� ��������� ��������� ����. (codex)
- �� ��������� ������� �������� UI cleanup: ������ ������ ������� ��������� � ������������ ����� ������� ��� ������� �������� � emoji, `Hold` ������������ � `������������`, � ������� � progress-���� ���� �������� ������ �������, � ��� ����� ������������ ��������� ����� ��� ����������� � ������� ���������� � ���������� backend-������. (codex)
- �� ������ ������������ ������ ������ �������, ���� �������� ������ �������� � �������� ������ ����, � �� ����������� ������� ��������� ���� �������� ������, ����� � �������� ������; HTML � JS ������� ���, ����� ��� �������� ������ �� ��������� ���� �� ������������� ��������. (codex)
- `������������ -> ������������` ��������� � ���������� ��������: ������ ��������� �������� ��������� ������� ���� � accordion, desktop-sidebar ������ �� ����������� �� ��������� ������, � ��������� ����� ������� ����� ������ ���������� ����������. (codex)
- �� �������� `���������`, `WiFi` � `������������` ��������� oversized checkbox � ������ ������������ �������; ������������� ���������� ���������� `Home Assistant Discovery` ����� `/api/settings/mqtt` � NVS, ����� ������� ������ �� ����������� ��� ����� reload/save. (codex)
- �� ������� `������� ���������` ������� ��������� � ��������� ����� �������, ������-������ ����� ��������� � ������, � ����� ������� `���/�����������/������������` �������� �� ���������� UTF-8 �������� � � HTML, � � runtime-�������������. (codex)

## [2.1.14] - 2026-03-17

### ��������

- � ��������� ������� ���� �������� ����� `�������`: ��� `index/charts/logs` �� ������ ������������ ��� ���������� ������ `??`, ����� ��������� �� ������ ��� � ��������� ������� ���������� �� ����� �������. (codex)

## [2.1.13] - 2026-03-17

### ��������

- ��������� �������� �� `������������ -> ������������` ������ ���� �� ������ � ��������� live-������, �� � � ����� event-log, � ��� �������� ������ �������� ������������� ����������� � `history.results.warnings` ��� ������������ ��������� ������� � reason code `RC_OPERATOR_SERVICE_ACTION`. (codex)
- ����� ������ ������� �������� ��������� `equipment_test`-������� � �������� �� ���������� ����� `/api/logs/events` � CSV-�������, � �������� [src/web/core/logs.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\core\logs.js) ������ ���������� �� ��� ��������� ��������� ������ ������ ������ ������������ ������. (codex)
- � ������ ���������� ������ ����� UI-polish: � [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) ��������� �������� ��������, `interlock` ������ �� `����������`, ������� `SIMULATED/RUNNING/IDLE/ON/OFF` ���������� �� ���������� ������� �������, � ������ ��������� �������� ���� ���������� ���������������� ����� �������. (codex)
- � ������� ������� [src/web/history/details.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\history\details.js) ��������� ���������������� ������� ��� `RC_OPERATOR_SERVICE_ACTION`, � `Safety timeline` �������� �� ������� ��� `���������� ������������`. (codex)

## [2.1.12] - 2026-03-17

### ��������

- � ������� �������� cleanup ����� ���������: ������������ ���������� UTF-8 � [CHANGELOG.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\CHANGELOG.md), [GEMINI.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\GEMINI.md), [SPEC.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\SPEC.md), [TODO2.0.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\TODO2.0.md), [docs/HOME_ASSISTANT.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\docs\HOME_ASSISTANT.md), [analysis_smart_column_s3.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\analysis_smart_column_s3.md), [plan_analysis.md](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\plan_analysis.md), [src/drivers/display.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\drivers\display.cpp) � [cloud_proxy/web/app.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\cloud_proxy\web\app.js). (codex)
- ������������� ������� ���������� ���������� ������� ������ ��������� ����������� � ��������, �������� `����`/`HTTP ����` � ������� ��������� � �������������, ����� � ����������� �� ���������� ��������� ���������� ����� ��������������� ��������������. (codex)

## [2.1.11] - 2026-03-17

### ��������

- ��� ������� `���������` �������� ��������� ���������� ���� ������ � [src/web/styles/_settings.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_settings.css), ������������ ����� [src/web/styles/main.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\main.css): ��������, form-group � action-������ ������ ������� � ��������� ����� � `������������` � `������������`. (codex)
- ������ ������ `��������` ������ �� ������������� ��� ������������� �� ��� ������ ��������: ��������� ��������������� ������ �� ������� �������� ������ �� desktop � ���������� ����� �� mobile, ���� ���� ������ HTML �������� inline `width: 100%` ��� `flex: 1`. (codex)
- �������������� ����� �� ������� `���������` ���� ���������: ��������� ������ `margin/padding` � ��������� ������ �� ��������� � ��������, ������� ����� �������������� ��� ���������� ��������� dashboard, � �� ��� ������� ������. (codex)

## [2.1.10] - 2026-03-17

### ��������

- � [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) ������ `������������` �������� �� ����� ������� ��������� ����: ����� ������� ����� �������, ����������� � action-�����, ����� ����� ������ �� �������� ��� ���������� ����� � ������ ��������. (codex)
- �������� `����������` ������ ������������ ������ ��������� input/stepper-��������� � ������ �� ����������� ���� ���������� �� ��� ������: �������� ����, inline-������ � ������ `���������/��������` �������� ������������� ������� ������� � ����� ��������� ����������. (codex)
- ������� � ��������� �������� � `������������` ���� ���������: [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) ������ �� ����������� ��������� KPI-�������� � preset-������ �� ��� ������ ����������, � mobile-������ ����� ���� � ����������. (codex)

## [2.1.9] - 2026-03-17

### ��������

- � [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) ���������� ����������� ��������� ��������� `������������` � ������� ����: ������ ����������� ��������� � `var(--primary)` ������ ������������ ���������� ������ `var(--accent) -> var(--accent-hover)`, ������� ��������� ����� ������ �� ������������ � ����� ����� �� ������� ������. (codex)
- Backend [src/interface/webserver.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\interface\webserver.cpp) ������ ������ ��������� ����� ��������� ��������� �������� ��������� �� ������� `������������ -> ������������`: �����/���� ������, ���, �������, ��������, ������� ������������ � ����� `stop-all` �������� � `recentActions` ������ `/api/testing/status`. (codex)
- �� ��������� [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) �������� ���������� ����� ������ ��������� �������� ����� � ��������� �������� `������������`, � smoke-�������� [tools/ui-smoke/tests/equipment-testing.smoke.spec.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\tools\ui-smoke\tests\equipment-testing.smoke.spec.js) �������� ��������� ����� �������. (codex)

## [2.1.8] - 2026-03-17

### ��������

- ������ `������������` ������ �� ������� ����������� ��������: `���������` � `����������` ������, ��� � `������������`, ����������� ����� workbench-���� � sidebar-���������� �� desktop � ���������� ������������, ��� ������� ���������� ������� �� ��������. (codex)
- �� ��������� [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) `���������` ����������� ������� �� ������ `����� � �����������`, `���, ��� � �������`, `���������� � ���������`, � `����������` �������� ���������� ������������ ����� ������� � ������������ � ����������� �������� ��������. (codex)
- � ������ [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) ��������� ����� ������� ���������� ��� ���� ���������� �������� `������������`: ������ ��������, ��� mobile-����� � ���������� ��������� action-����� �� ���� ��� �����������. (codex)
- Smoke-�������� [tools/ui-smoke/tests/equipment-testing.smoke.spec.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\tools\ui-smoke\tests\equipment-testing.smoke.spec.js) �������� � ������ ��������� �� ������ ��������� �����, �� � ����� ��������� `���������/����������`, ����� ������ workbench �� ������������ ���������. (codex)

## [2.1.7] - 2026-03-17

### ��������

- ����� `������������ -> ������������` ��������� ���������� ��� ��� �� �������, ��� � `�����������`: �������� ���������� sidebar-menu �� desktop � mobile-���������� � ����� ������� ��������� ������ ������� ���������� �������� �� ���� ��������� ������ �����. (codex)
- �� ��������� [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) �������� workbench-���� ��� ��������� ����� `����� / ������� / ����������� / ��� / ���������� / �������� / �������� / �������`, � ����������� �������� �������� � ������ ���� ���������. (codex)
- � ������ [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) ��������� �������, ����� action-������ � ��������� ������, � �������� ������������ ���������� �� ����� ������� ���������� ��� ������� ������������� ������������ ������. (codex)

## [2.1.6] - 2026-03-17

### ��������

- � ��������� ������ `������������ -> ������������` ��� �������� ��������� ���������� ����� � ������������� �������������: �������� ����� ������ �������� ������ �� `����`, `������` � `���`, �� �������� ������ � ���������� �������� ���������. (codex)
- ������� [src/drivers/valves.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\drivers\valves.cpp) ������� ������������� timer ��� ��������� ��������, � backend [src/interface/webserver.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\interface\webserver.cpp) ������ ������������ ����� ������ `waterPulse/headsPulse/unoPulse` � ��������� `POST /api/testing/valves` � `action: "pulse"`. (codex)
- �� ��������� [src/web/settings/equipment-testing.js](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\settings\equipment-testing.js) � [src/web/styles/_equipment.css](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\web\styles\_equipment.css) ��������� �������� �������� ���� ������������ ��������, ��������� ������ `�������` � ����� ��������� � ���������� ��������, � smoke-�������� `tools/ui-smoke/tests/equipment-testing.smoke.spec.js` ������ ��������� � ���� ��������� ����. (codex)

## [2.1.5] - 2026-03-17

### ��������

- ��� ������ workspace `������������ -> ������������` �������� ��������� UI smoke-�������� `tools/ui-smoke/tests/equipment-testing.smoke.spec.js`, ������� ��������� ��������� ���������, �������� `/api/testing/status`, ������ ����� ������, �������� ������������, ����� `stop-all` � ������� � ����������. (codex)
- ��������� `tools/ui-smoke/tests/helpers/smoke-helpers.js`: ��������� fixtures � ����������� ��� `/api/testing/*` � `/api/calibration`, ����� ����� ��������� ����� ��������� ������������ � headless-����� ��� ������ ����� � ������ ��������. (codex)

## [2.1.4] - 2026-03-16

### ��������

- � ������ `������������` ��������� ���������� ��������� ��������� `��������� / ���������� / ������������`, � ����� ����� `������������` ������ ��� ��������� ��������������� ������� ���� � �������� ��� ������, ��������, ������������ ������������, ����, �����������, ������� ��������, ���������-�������� � ������� ����������. (codex)
- �������� backend API `GET /api/testing/status`, `POST /api/testing/stop-all`, `POST /api/testing/pump`, `POST /api/testing/heater`, `POST /api/testing/valves`, `POST /api/testing/servo`: �� ���������� live-������ ������, interlock-�����������, ��������� �����/��������/������������/�������� � ��������� ��������� �������� ������ � ���������� ��������. (codex)
- ������������ ������������ ������ ����������� � ����� ���������� ����� NVS: ��������� �������� � ���������� `enabled`, ����� � ������ �������� �������, � ������� �������� ������� live API ��� �������� ����, ����� �������� � ����������� ������������. (codex)
- �� ��������� �������� ����� ������ `src/web/settings/equipment-testing.js` � ��������� ����� `src/web/styles/_equipment.css`: �������� ����� �������� ������ ����������, ������ �������� ������, �������� ��������� ������������� ������� ����, ������� pump-�������, ������ ���������� ������������� � ���������� �������� � ����������. (codex)

## [2.1.3] - 2026-03-16

### ��������

- � demo-������ ��������� ��������� ���������� ���������� ��������������� ������������: �������� ������, ���� � �������� ������ �� ������ �������� ������� �� ������ ��� `demoMode=true`, �������� ������ ���������� ��������� ��� ���������� � ���������. (codex)
- [src/main.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\main.cpp) ������ �� ���������� demo-��������� ����� ������� ����������� ��������, ������� UI ���������� ��������������� �������� ��� ������� ������� ������� �����. (codex)

## [2.1.2] - 2026-03-16

### ��������

- � [src/control/demo_simulator.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\control\demo_simulator.cpp) ����-����� �������� ������ ����� �� ������ ����� � ��������� `setSpeed()`: ������ ������ ��������� ������� �������� ���� � ���������� �. (codex)
- ��������� ��������� ������������� ����-����� ����� FSM ����� [src/main.cpp](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\main.cpp) � [src/control/demo_simulator.h](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\src\control\demo_simulator.h), ����� � demo ����� ������ �������� ��������������� ��������, �� �� ����� ������� �� ������ ������. (codex)

## [2.1.1] - 2026-03-16

### ��������

- ��������� ������ [scripts/build_web.py](C:\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\Smart-Column-S3-claude-smart-column-s3-01BtHoqGVyMaVAPXERRPFJq7\scripts\build_web.py): �� ����� ������ Unicode-�������, ��-�� ������� `uploadfs` ����� � Windows-������� � �� ������� LittleFS �� ����������. (codex)
- ����� ����� LittleFS ���������� �� ����������, ��� ��� Web UI �� ���������� ������ ������������� ������ ��� Telegram-��������. (codex)

## [2.1.0] - 2026-03-16

### ��������

- �� �������� ��������� ����� Telegram-������: �������� `src/interface/telegram.cpp`, `src/interface/telegram.h`, backend API `/api/settings/telegram*`, runtime ������ �� `src/main.cpp`, ��������� ���� �������� � NVS-�����. (codex)
- �� Web UI � ������ ������� ��� Telegram-��������� � ��������: ������ `src/web/settings/telegram.js`, ���� Telegram �� `data/index.html`, ������� �� `src/web/main.js` � `src/web/_main-init.js`, � ����� ������ � `scripts/wire-modules.mjs` � `scripts/split-app.mjs`. (codex)
- �� ������� ������� ����������� `FastBot2`, vendored ������� `lib/FastBot2`, ����������� ���������� � `README.md`, `SPEC.md`, `docs/API.md`, `docs/HOME_ASSISTANT.md`, `data/landing/index.html` � ��������� ������� markdown-������, ����� Telegram ������ �� ����������� �� ��� �������, �� ��� ����������. (codex)
- ������ �������� ������� �� `2.1.0` ��� ������ ����� ����� ������� �������� Telegram �� firmware, Web UI, ������������ � ������������ �������. (codex)

## [2.0.44] - 2026-03-15

### ��������

- � `src/drivers/pump.cpp` ��������� ����������� �������� ������: ������� ������� �� `PIN_PUMP_DIR` � ����������� ��� `start()` ������ ������������ � ��������������� ���������, ����� ����� �������� � �������� ������� ������������ �������� `2.0.43`. (codex)
- ������ �������� ������� �� `2.0.44` ��� ��������� hotfix �� ����������� �������� ������, ����� ��� ��������� ���� ������������� �������� �� ����������� �������� STEP �� ���������� ���������. (codex)

## [2.0.43] - 2026-03-15

### ��������

- � `src/drivers/pump.cpp` ��������� STEP-��������� ��� ������ ���������� � ��������� `AccelStepper::runSpeed()` �� ���������� PWM-��������� ESP32 (`LEDC`), ����� ������� ����� ������ �� �������� �� �������� ����� � �� �������� ������������� ������� � ������� ������ �� ������� ���������. (codex)
- � `src/drivers/pump.cpp` pump-task ��������� � ����� supervisory-����: �� ������ ������ ������ �������� ������� � �������, �������������� ������� ����� � ������ �� ������� � ������ �� �������� �� ���� ������ STEP-�������. (codex)
- � `src/interface/webserver.cpp` ��������� ����������� ����������� `GET /api/pump/diag`, ������� `appliedSpeedMlH`, ����� �� ����� ���������� ����� ���� ���������� ��������� � ����������� ���������-���������� �������� ������. (codex)
- ������ �������� ������� �� `2.0.43` ��� ��������� hotfix �� �������� �������� ������ � ������� ��������� ������ �� ������� ���������. (codex)

## [2.0.42] - 2026-03-15

### ��������

- � `src/drivers/pump.cpp` ������ ���������� `vTaskDelay(1 ms)` ����� �� ����� ������ ������: pump-task ������ ������ �� ������ ������������� 10-�� "����" � ����� �����, ������� �� ������� ��������� ������ ������������� ������� � ��������� stepper-������. (codex)
- � `src/drivers/pump.cpp` �������� ������ ramp ������� �������� ��� ������ `runSpeed()`, ������ ��� `AccelStepper` �� ���� ���� �� ���������� acceleration ��� �� ����: ����� ������ ������� �� ������� �������� ������� � ��� ����������� ������ step-rate ��� `start/setSpeed`. (codex)
- � `src/drivers/pump.cpp` ��� STEP-������� ����� `setMinPulseWidth(4)`, � � `src/interface/webserver.cpp` ����������� `GET /api/pump/diag` ��������� ����� `appliedSpeedMlH`, ����� �������� ������ ������� � ���������� ���������� �������� ������. (codex)
- ������ �������� ������� �� `2.0.42` ��� ��������� ��� live-fix �� ��������� � ������������ ������ �� ������� ��������� ����� ������ �������� �� ����������. (codex)

## [2.0.41] - 2026-03-15

### ��������

- � `src/interface/webserver.cpp` ��������� `GET /api/status` ������ ������������� ��������� `ControlV2` ����� ������������� ������, ����� `v2.lifecycle` � `v2.paused` �� ��������� �� ������� ����� `mode/paused` ����� ������ FSM. (codex)
- � `src/interface/webserver.cpp` � `src/interface/cloud_tunnel.cpp` `POST /api/process/pause` � `POST /api/process/resume` ������ ����� �������� `ControlV2::updateRuntime(...)`, ������� ����� ������������ ����� ��� ������������� `v2`-������ ����������� � ��� �� API-����������, � �� ��� ���������� �����. (codex)
- � `src/interface/cloud_tunnel.cpp` `GET /api/status` �������� � ��������� �������� � ���� ������������ ������ `v2` runtime ����� ����� �������, ����� ��������� � �������� status path �� ����������� �� pause/resume ���������. (codex)
- ������ �������� ������� �� `2.0.41` ��� ��������� ��� live runtime consistency fix ����� ������� API-smoke �� ������� � ����������� ������. (codex)

## [2.0.40] - 2026-03-15

### ��������

- � `src/drivers/pump.h` � `src/drivers/pump.cpp` ��������� ����� runtime-���������� worker-task ������: ������ ����� ����� `taskAlive`, `taskLoopCount`, `cooperativeSleepCount`, `fastYieldCount`, `lockTimeoutCount`, `lastLoopAtMs` � ������� step/volume ������� ��� ����������� ���������. (codex)
- � `src/interface/webserver.cpp` �������� ����� ��������������� endpoint `GET /api/pump/diag`, ����� ��������� ��������� pump-task � �������� starvation/lock-problem ����� �� ���� � ����������. (codex)
- � `src/main.cpp` �������� ���������� `RebootTracker` �� ������� boot: `totalReboots`, `wdtReboots`, `crashReboots` � `userReboots` ������ ���� �� �������� ��� ���������� ��������, � �� ���������� ������ ��� ����� �������� ������������. (codex)
- ������ �������� ������� �� `2.0.40` ��� ��������� ��� ��������������� ������������ � post-fix observability ����� hotfix ������. (codex)

## [2.0.39] - 2026-03-15

### ��������

- `src/drivers/pump.cpp` ����������� ���, ����� pump-task ������ �� ������� `core 1` � ����������� spin-loop �� ������������ ����������: ������ ����� �������� ������������� yield-slice � `vTaskDelay(...)`, ����� �� ������ `idle task` � �� ������������� `Task WDT` reset ��� ���������� ������. (codex)
- � ��� �� `src/drivers/pump.cpp` ��������� mutex-������ ������ `AccelStepper`, ������ ��� ������ `runSpeed()` � ��������� ������ � ������ `start/stop/setSpeed/currentPosition` �� ��������� ������ ���������� � ������ ������� ��� �������������, ��� ��������� race condition �� ����� ������ ������. (codex)
- ����� runtime-check �� ���������� ������� ������� �� �����: `http://192.168.3.138/api/status` ������� ����� ��������� `uptime`, � `http://192.168.3.138/api/reboot/status` ��������� `Task WDT`, ��� ������ ������� � ���������� ������ pump-task � ����� ���������� ��� ��������� ����������� �����������. (codex)
- ������ �������� ������� �� `2.0.39` ��� ��������� hotfix-��� �� ������������ ������ ����� post-migration �����. (codex)

## [2.0.38] - 2026-03-15

### ��������

- `tools/ui-smoke/tests/helpers/smoke-helpers.js` �������� ���������� `GET /api/history` � `GET /api/history/{id}`: ����� smoke harness ������ ����� ������ ������ ������� � ������ �������� ��� �� ������, ��� ��� ����� `status` � `logs`. (codex)
- �������� ����� smoke-���� `tools/ui-smoke/tests/history-v2.smoke.spec.js`, ������� �������� ���� `history list -> details modal` � ��������� `v2` safety summary, completion badge, outcome summary, phase `reasonCode/operatorMessage` � `Safety timeline`. (codex)
- ��� ��������� ������ post-migration verification gap: ����� ���������� v2 migration ������� ��������� ������ ���������� �� ������ ����� � manual UI-����������, �� � �������������� smoke-��������� �� �������� `v2`-����. (codex)
- ������ �������� ������� �� `2.0.38` ��� ��������� ��� ��������� verification � post-migration polish ����� ��������� �� ����������. (codex)

## [2.0.37] - 2026-03-15

### ��������

- `src/types.h` � `src/drivers/sensors.cpp` ������� �� ������� `inline static constexpr` warning: `SystemHealth::healthWeights` ������� � ������� `static const` �����������, ����������� � ������� ���������� ������ Arduino/PlatformIO. (codex)
- ��� ������� �������� ��� �� firmware build ����� ���������� v2 migration � ������ post-migration ������ ������� ���� ��� ��������� ��������� health scoring. (codex)
- ������������� ������� `npm run test:ui-smoke`: ��� 5 smoke-������ Web UI ������ ������� �� ������� post-migration ��������� �������. (codex)
- ������ �������� ������� �� `2.0.37` ��� ��������� ��� post-migration cleanup � verification. (codex)

## [2.0.36] - 2026-03-15

### ��������

- `docs/v2/migration_preparation.md` ������� �� ������ ���������� `Wave 4` audit: ������ � �������� migration-���� ���� �������������, ��� runtime/contracts/history/API/UI ���� ����������� �������� � ������ ��������� �� ������ ��������� �����������, � �� �������� ������������� �����������. (codex)
- � �������� ��������� ������� ������ ��������� (`98-99%`), ���������� ���������� ������ (`RC_PHASE_TRANSITION_INFERRED` ��� ������� adapter fallback, `RC_UNSPECIFIED` ��� ������ �������������) � ������������ definition of done ��� �������� ��������. (codex)
- ��� ������� ���� �������� �� ��������� �����: ������ �������� migration-��� �������� �� ��������� ����, � ����������� ��������� ����� ���� ������������� ����� `2.0.x`. (codex)
- ������ �������� ������� �� `2.0.36` ��� ��������� ��� �������� ���������� migration-review � ���������� verification goals. (codex)

## [2.0.35] - 2026-03-15

### ��������

- `src/control/v2/status_adapter.cpp` ������ ����� ���������� explicit terminal transition ���� ���� handler � ��� �� �������� ��� ������ ��������������� `mode` � `IDLE`. (codex)
- ��� ��������� ������ semantic gap ��� `NBK`, `FERMENTATION`, `HOLD` � `MASHING`: �� ��������� `notePhaseTransition(...)` ������ �� ������ �������� � ����������� � adapter fallback ������ ��-�� ������� ���������� state ������ ������ loop-pass. (codex)
- � ���������� `lastReasonCode`, transition log, history completion � live `v2` status �������� ������ explicit ��������� ������� ��������, � �� post-factum inferred mode-exit reason. (codex)
- ������ �������� ������� �� `2.0.35` ��� ��������� ��� ���������� runtime-audit �� ���������� terminal transitions �� happy-path completion ���������. (codex)

## [2.0.34] - 2026-03-15

### ��������

- `src/control/modes/distillation_handler.cpp` ������ �� ���������� `RC_UNSPECIFIED` �� ���������� helper ��� ���������� body-phase: ���� �������� fallback ������ ���������� ����������� `RC_BODY_END_DETECTED`. (codex)
- Fallback message ��� ���������� body-phase � `DISTILLATION` ���� �������� �� ������ `"Distillation body end detected"`, ����� history, transition log � live status �� ������ ����� ���� � �������� �����. (codex)
- ��� ��������� ��� ���� ���������� �������� runtime `RC_UNSPECIFIED` � ������ `v2` reason contracts ����������� ������ ���������� ������ �� �������� mode paths. (codex)
- ������ �������� ������� �� `2.0.34` ��� ��������� ��� ��������� �������� legacy fallback reasons ����� runtime-audit inferred paths. (codex)

## [2.0.33] - 2026-03-15

### ��������

- `src/control/modes/distillation_handler.cpp` ������ �� ���������� `RC_PHASE_TRANSITION_INFERRED` � ����������� recovery-�����: �������������� ������������ distillation phase � `BODY` ������ ��� ����� ��������� explicit reason `RC_PHASE_RECOVERY_APPLIED`. (codex)
- `src/control/v2/reason_codes.h` �������� ����� ��������� reason code ��� mode-level phase recovery, ����� `RC_PHASE_TRANSITION_INFERRED` ��������� �������� ������ adapter fallback, � �� ������� recovery-������� ������ handler. (codex)
- `src/web/runtime/process-notifications.js` ������ ���������� ����� reason code ��-�����������, ������� live ����������� ������ �� ��������� ��������� ��������������� phase recovery � adapter inference. (codex)
- ������ �������� ������� �� `2.0.33` ��� ��������� ��� ��������� �������� ���������� `RC_PHASE_TRANSITION_INFERRED` ����� �������� ������� ��������� missing contracts. (codex)

## [2.0.32] - 2026-03-15

### ��������

- `src/control/v2/status_adapter.cpp` �������� �������� `v2.operatorMessage` ������� safety message �� ������ �����: ������ `operatorMessage` ������� ����������, ��������� ������ � `lastReasonCode`, ��� � ������������ ��� live status, API � �����������. (codex)
- Fallback safety message ������ ������������� ������ �����, ����� � ������� ��� ��� ������������ `lastReasonCode`, ������� `v2` contract ����� ��������� ��������� "��������� ������� ��������" � "������� safety-���������". (codex)
- ��� ������� mode-change fallback ����� `status_adapter` ������ ���������� ����� `setStatusReason(...)`, ����� `lastReasonCode` � `operatorMessage` ����������� ������������ ���� ���, ��� explicit transition ��� �� ������. (codex)
- ������ �������� ������� �� `2.0.32` ��� ��������� ��� ���������� runtime-audit �� ������������ semantics � `status/status-history/API` ����� �������� ��������� inferred contracts. (codex)

## [2.0.31] - 2026-03-15

### ��������

- `src/control/fsm.cpp` �������� � `src/control/v2/safety_supervisor.cpp`: `abortMode()` ������ ������ `POWER_FAILURE` � ����� `RC_SAFETY_TRIP_POWER`, � �� ������ ��� ������� � ������ fallback-��������. (codex)
- Generic safety abort � `FSM` ������ �� ������ � `RC_UNSPECIFIED`: ��� �������������� alarm ����� ������ ������������ `RC_SAFETY_TRIP_GENERIC`, ����� history, transition log � live status ��������� ����������� `v2` reason code. (codex)
- ��� ��������� ��� ���� ����������� ����� runtime stop path � ����� `SafetySupervisorV2`, �������� ���������� legacy-semantics � `Wave 4` audit. (codex)
- ������ �������� ������� �� `2.0.31` ��� ��������� ��� �������� �������� safety reason mappings ����� �������� phase fallback � explicit inferred transitions. (codex)

## [2.0.30] - 2026-03-15

### ��������

- `src/control/v2/status_adapter.cpp` ������ �� �������� ��������� �������� ������� phase changes �� ��������� ���������, ���� explicit `notePhaseTransition(...)` �� ��� �������: ����� ������ ������ ������ ����������� ��� `RC_PHASE_TRANSITION_INFERRED`. (codex)
- ��� inferred phase fallback ��������� ����� ������������ ��������� `from -> to`, ������� history, transition log � live status ������ ����������, ��� ������� ��� ������������ ���������, � �� ������ �� ���������� mode contract. (codex)
- ��� ������������� ��������� ���� `inferPhaseReason(...)` ��� ��������� "����������" ������ � ������ ���������� ���� � explicit contracts ������������ ������ ���������� ������� ��������������� reason codes. (codex)
- ������ �������� ������� �� `2.0.30` ��� ��������� ��� ��������� �������� semantic fallback-������ � `status_adapter`. (codex)

## [2.0.29] - 2026-03-15

### ��������

- `src/control/v2/safety_supervisor.cpp` ������� ����� `v2` reason mappings ��� `POWER_FAILURE` � generic fallback safety trip, ������� latched safety state ������ �� ����������� � `RC_UNSPECIFIED` ��� �������� alarm mapping. (codex)
- `src/control/v2/reason_codes.h` �������� ������ `RC_SAFETY_TRIP_POWER` � `RC_SAFETY_TRIP_GENERIC`, � ����� ����� `power_failure` safety event token ��� ������� � API. (codex)
- `src/history.cpp`, `src/web/history/details.js` � `src/web/runtime/process-notifications.js` ������� ������������ � ��������������� ���������� ����� safety trip reasons, ����� post-mortem � live UI �� ������ ����� ��� power/generic �������. (codex)
- ������ �������� ������� �� `2.0.29` ��� ��������� ��� ����� ��������� �������� fallback reason codes � safety-v2 ����. (codex)

## [2.0.28] - 2026-03-15

### ��������

- `src/control/v2/reason_codes.h` �������� ����������� `RC_PHASE_TRANSITION_INFERRED`, ����� ���������� fallback-�������� ������ �� �������� � runtime/history ��� �������� `RC_UNSPECIFIED`. (codex)
- `src/control/v2/status_adapter.cpp` �������� �� ����� fallback reason ��� ������� phase changes, ��� ��� ���� ���� �������� �� ��� ���������� ��������������� ������� ��������, �� ������ ������ ��� ���� � �����������. (codex)
- `src/control/modes/distillation_handler.cpp` ������� explicit recovery transition � ��������� `default`-�����: �������������� distillation phase � `BODY` ������ ���� ��� ����� `notePhaseTransition(...)`, � �� ����� ����� phase reset. (codex)
- `src/web/runtime/process-notifications.js` ������ ��������������� ���������� `RC_PHASE_TRANSITION_INFERRED`, � ������ �������� ������� �� `2.0.28` ��� ��������� ��� �������� ���������� fallback semantics. (codex)

## [2.0.27] - 2026-03-15

### ��������

- `src/control/v2/status_adapter.cpp` ������ �� `mode -> IDLE` ������� �������� ��� ���������������� terminal `v2` reason code, � �� �������� ������ �������� ���������� ������ �� `previousPhaseId` � legacy mode-change ����������. (codex)
- ��� normal completion ������������� success �������� � `isSuccessfulCompletionReason(...)`, ������� `mashing`, `hold` � `fermentation` ��������� ��������� ���� explicit completion reason ��� ������ � `IDLE` ��� ������� ���������. (codex)
- Fallback ��� mode exit ���� � ��������� `inferModeExitReason(...)`: safety stop ��-�������� ������ �� latched alarm, � ������� ��������� �������� ������ ��� �������� ���� ��� legacy ���������� `RECTIFICATION/DISTILLATION/NBK`. (codex)
- ������ �������� ������� �� `2.0.27` ��� ��������� ��� ���������� inference-layer � `status_adapter` ����� ���������� ��������� ���������� `RC_UNSPECIFIED` fallback paths. (codex)

## [2.0.26] - 2026-03-15

### ��������

- `src/control/fsm.cpp` ������� ����� `noteModeExitTransition(...)` ��� ���� �������, ������� explicit `v2` exit contracts ������ ������������ ��������������� � ������ �� ��������� �������� �� `stopMode()` fallback-������. (codex)
- ��� `RECTIFICATION` � `NBK` ������ ���� �������� ����� stop transitions ��� ������������ ���������, � `abortMode()` ������ �� �������� ����� user-stop path � ��������� safety reason code �� ������� ������. (codex)
- ����� helper `finalizeModeStop(...)` �������� shutdown/reset mode state ��� ������������, � `status_adapter` ����� ����� �������� ������ mode-change inference � ������ �������� ������ ����������. (codex)
- ������ �������� ������� �� `2.0.26` ��� ��������� ��� �������� stop/abort semantics � ������� `v2` lifecycle contract. (codex)

## [2.0.25] - 2026-03-15

### ��������

- `src/control/v2/mode_contracts.h` �������� ����� `kNoPhaseIdV2`, ����� `v2`-��������� ����� ���� ���������� ���������� �������� ���� � �� ��������� ���� ������ ���������� ����������� ������ �������. (codex)
- `src/control/v2/status_adapter.cpp` ������ �������� `kNoPhaseIdV2` ��� `idle` phase token, ������� ��� idle-like ��� history � ��������� explicit start transition �� `IDLE`, ���� handler ������� sentinel ������ legacy phase id. (codex)
- `src/control/modes/hold_handler.cpp` � `src/control/fsm.cpp` ���������� �� ����� �������� ��� `HOLD`: ����� ���������� ��� `idle -> hold_step`, � ������������ ��������� ��� `hold_step -> idle`, ��� �������������� `hold_step -> hold_step`. (codex)
- ������ �������� ������� �� `2.0.25` ��� ��������� ��� ������������ phase semantics ��� `HOLD` ����� ���������� ��������� ���������� fallback-�������� `Wave 4`. (codex)

## [2.0.24] - 2026-03-15

### ��������

- `src/control/v2/status_adapter.cpp` �������� ���������� explicit start transitions: `notePhaseTransition(...)` ������ ��������� �������������� �� ������ ��� ��� ������ ������ � ���������, �� � ��� ������ ������ �� `IDLE`, ���� handler ���� ����� ��������� ��������. (codex)
- `src/control/fsm.cpp` ������ ������ ����� `RC_MODE_START_REQUEST` ��� ������ `RECTIFICATION`, `DISTILLATION`, `NBK` � `FERMENTATION`, � `src/control/modes/mashing_handler.cpp` � `src/control/modes/hold_handler.cpp` ������ �� �� ��� ������ `Mashing::start(...)` � `Hold::start(...)`. (codex)
- ��� ������� ��� ���� ���� fallback-��������� �� `Wave 4`: history, transition log � live `lastReasonCode` �������� �������� ��������� reason contract ������ ����������� �������������� �� mode-change fallback. (codex)
- ������ �������� ������� �� `2.0.24` ��� ��������� ��� �������� ��������� ���������� � ������������ v2 mode lifecycle. (codex)

## [2.0.23] - 2026-03-15

### ��������

- `src/control/modes/fermentation_handler.cpp` ������ ��������� ����������� �� `settings.fermentation.durationHours`, ������ ����� `v2` transition `RUNNING -> COMPLETED` � `RC_FERM_TARGET_REACHED` � ��������� ��������� ����������� ��� ������ ������. (codex)
- ��� `FERMENTATION` � `src/control/fsm.cpp` ��������� ����� ������������ ��������� ����� `RC_MODE_STOP_REQUEST`, ����� ������ stop ����� ������ �� ������� � history � transition log. (codex)
- `src/control/v2/status_adapter.cpp` ������ ������� `FERMENTATION` �������� ����������� ��� `RC_FERM_TARGET_REACHED`, ������� completion summary/history ������ �� �������� ��� ������� `stopped`, ���� ����� �������� ������ ������ �� �������. (codex)
- ������ �������� ������� �� `2.0.23` ��� ��������� ��� `Wave 4` ��� �������� `fermentation` �� ����� v2 completion transitions. (codex)

## [2.0.22] - 2026-03-15

### ��������

- `src/control/modes/hold_handler.cpp` � `src/control/modes/mashing_handler.cpp` ���������� �� ����� `v2` phase contracts ��� ������������� �����: ���������� ���� � ����� ��������� ������ ������ `ReasonCodeV2::RC_TEMP_STEP_HOLD_COMPLETE` � ������������ ���������� ����� � ����� ��������. (codex)
- ��� `MASHING` � `HOLD` � `src/control/fsm.cpp` ��������� ����� ������������ ��������� ����� `RC_MODE_STOP_REQUEST`, ����� manual stop ���� ������� �� ������� � history � transition log. (codex)
- `src/control/v2/status_adapter.cpp` ������ ������� `MASHING` � `HOLD` �������� ����������� ��� `RC_TEMP_STEP_HOLD_COMPLETE`, ��� ��� history � completion summary ��� ���� ������������� �������� ������ �� �������� ��� ������� `stopped`. (codex)
- ������ �������� ������� �� `2.0.22` ��� ��������� ��� `Wave 4` ��� �������� `hold/mashing` �� ����� v2 phase transitions � ���������� completion outcome. (codex)

## [2.0.21] - 2026-03-15

### ��������

- `src/control/modes/distillation_handler.cpp` �������� �� ����� `v2` phase contracts: �������� `HEATING -> HEADS/BODY`, `HEADS -> BODY`, `BODY -> FINISH` � `FINISH -> IDLE` ������ ������ `ReasonCodeV2` � ������������ ��������� ����� � ����� ����� ����. (codex)
- ��� `DISTILLATION` � `src/control/fsm.cpp` ��������� ����� �������� ������������ ��������� ����� `RC_MODE_STOP_REQUEST`, ����� history � transition log �� ������ ������� � fallback-��������� ��� ������ stop ������. (codex)
- ��� ��������� ��������� ����� `Wave 4` �� `docs/v2/migration_preparation.md`: distillation ���� ��� ����� �������, ������� ��� ���� � ��������� v2 transition reasons, � �� ������ �������������� ����� v2 status/read-only ����. (codex)
- ������ �������� ������� �� `2.0.21` ��� ��������� ��� �������� `DISTILLATION` � ����� v2 phase transitions. (codex)

## [2.0.20] - 2026-03-15

### ��������

- `src/history.h`, `src/history.cpp` � `src/interface/webserver.cpp` ��������� compact ������ ���������� ��������: history list ������ �������� `completionState`, `completionReasonCode` � `completionOperatorMessage`, ����� ��������� normal finish, operator stop � safety stop ��� ������ �������� details. (codex)
- `src/web/history/list.js` ������ ���������� ��������� completion badge � �������� history � ���������� ����� v2-aware �������� state ������ legacy `status`, �� ����� ������ �������� ����� `FINISH`, `OPERATOR STOP` � `SAFETY STOP`. (codex)
- `src/web/styles/_history.css` �������� ������� completion badge, � ��������� `title` ��������� ������ ��� ��������������� detail-����������, �� ��� ������������ ������ ������ ����� ��������. (codex)
- ������ �������� ������� �� `2.0.20` ��� ��������� ��� �������� history list � v2 completion state summary. (codex)

## [2.0.19] - 2026-03-15

### ��������

- `src/history.h`, `src/history.cpp` � `src/interface/webserver.cpp` ��������� compact summary ���������� �������� ������: history list ������ ��� ������ �������� details �������� `lastPhaseName`, `lastReasonCode` � `lastOperatorMessage` �� ��������� ���������� ���� ��������. (codex)
- `src/web/history/list.js` ������ ���������� � �������� �������� �������� v2-aware ������ ��������� �������� �� ��������� ����, ������� � ������������� ���������, ����� ����� � safety badge ���� ����� �����, ��� ���������� ��������� �������� ����. (codex)
- ��� ����� summary-������ � `src/web/history/list.js` ��������� HTML-�������������, � `src/web/styles/_history.css` ������� ���������� ellipsis-�����, ����� `operatorMessage` ��������� � ��������� ����������� � ������ history. (codex)
- ������ �������� ������� �� `2.0.19` ��� ��������� ��� �������� history list � v2 phase outcome summary. (codex)

## [2.0.18] - 2026-03-15

### ��������

- `src/history.h` � `src/history.cpp` ��������� compact safety summary ��� `ProcessListItem`: history list ������ ��� ������� �������� �������� ��������� ����� `trip/ack/recovery/reset/limited` � �������� �������� �������� `safetySummary` ��� ������ �������� details JSON. (codex)
- `/api/history` � `src/interface/webserver.cpp` ������ ����� `safetyState`, `safetySummary` � ��������� safety-�����, ����� ������ ��������� ��� ����� ��������, ���������� �� safety-�������� ������ `ACK + RESET`. (codex)
- `src/web/history/list.js` � `src/web/styles/_history.css` ������ ������ safety badge ����� � �������� ��������, ������� � history list ����� ����� `TRIP`, `ACK`, `RECOVERY` ��� `ACK + RESET` ��� �������� details modal. (codex)
- ������ �������� ������� �� `2.0.18` ��� ��������� ��� �������� history list � v2-aware safety summary. (codex)

## [2.0.17] - 2026-03-15

### ��������

- `src/control/safety.cpp` ������ ������ ���������������� v2 operator actions ��� `ack` � ��������� `reset`, � `src/control/v2/status_adapter.cpp` ���������� �� � persistent history ��� ��������� ������� `RC_SAFETY_ACKNOWLEDGED` � `RC_SAFETY_RESET_COMPLETED`, �� ���������� ������ �� `webserver` � `cloud_tunnel`. (codex)
- ������ operator actions �������� ����� pending hook ������ `status_adapter`, ������� ������� safety timeline ������� ������������� � runtime-���������� `recovery/trip`, � `ack/reset` �� ������ � `RC_SAFETY_RECOVERY_EXITED` � ��� �� ���� ����������. (codex)
- Frontend history � runtime labels ��������� ��� ����� reason codes: `src/web/history/details.js`, `src/web/styles/_modal.css` � `src/web/runtime/process-notifications.js` ������ ���������� ������������� � ����� ������ ��� ��������� info-�������. (codex)
- ������ �������� ������� �� `2.0.17` ��� ��������� ��� �������� safety history � ������� �������� `trip -> ack -> recovery -> reset`. (codex)

## [2.0.16] - 2026-03-15

### ��������

- `src/control/v2/status_adapter.cpp` ������ ����� � persistent history �� ������ ��������� `safety stop`, �� � live safety transitions `limited`, `recovery` � `latched trip`, ����� �������� v2 `severity/reasonCode`, ��� ��� history �������� ����� ������ safety-���������� ��������. (codex)
- ��� recovery �������� ��������� history-event `RC_SAFETY_RECOVERY_EXITED`, � ��������� ������ safety stop ������ ��������������� ������������ ��� ����������� transition event, ����� post-mortem timeline �� ��������� ����������� ���������� �����������. (codex)
- `ProcessRecorder::addWarning()` � `src/history.cpp` ������ ������� ��������� `recording`, ����� warning/error ������� �� ����� ������� � history ��� �������� process-������. (codex)
- ������ �������� ������� �� `2.0.16` ��� ��������� ��� �������� persistent history � ����������� v2 safety timeline. (codex)

## [2.0.15] - 2026-03-15

### ��������

- History details modal � `src/web/history/details.js` ������ �������� ��������� `Safety timeline` �� ���������� `results.errors` � `results.warnings`, ��������� ������� �� ������� � ���������� � post-mortem ���� ������, recovery � safety-����������� ��������. (codex)
- Safety timeline ���������� ������������ v2 `reasonCode` (`RC_SAFETY_TRIP_*`, `RC_SAFETY_RECOVERY_*`, `RC_SAFETY_LIMIT_*`) ��� ���������������� ���������� �������, ��� ��� �������� ����� �� ������ raw message, � ��������� safety-�������� ��������. (codex)
- � `src/web/styles/_modal.css` ��������� ���������� ��������� ��� safety timeline card/badge (`error`, `limited`, `recovery`), ����� ����������� trip-�������, ����������� � �������������� ������� ������������ ��������� ����������� � �������. (codex)
- ������ �������� ������� �� `2.0.15` ��� ��������� ��� �������� history UI � ������� ������������ safety post-mortem ������. (codex)

## [2.0.14] - 2026-03-15

### ��������

- History details modal � `src/web/history/details.js` �������� ������� `������ � ������`, `��������������` � `�������`, ������� ���������� ���������� `results.errors`, `results.warnings` � `notes`, ������� `reasonCode` � `operatorMessage` ��� post-mortem ������� ��������. (codex)
- ��������� ��� � ����� history event-������ ���������� �� ���������� �������� DOM-����� ����� `textContent` ������ ������ ����������� ������������ ����� � `innerHTML`, ����� `operatorMessage` � ������� �� ��������� XSS-����������� UI. (codex)
- � `src/web/styles/_modal.css` ��������� ����� ����� ��� history events � full-width history sections, ����� safety warnings/errors �������� ��� ��������� �������� ������ ���������� ����. (codex)
- ������ �������� ������� �� `2.0.14` ��� ��������� ��� ��������, ������� ������ history UI �������� ��� ������� ������ � safety-��������� ����� ���������� ��������. (codex)

## [2.0.13] - 2026-03-15

### ��������

- � ���������� `src/interface/webserver.cpp` ���������� ������� history-endpoints: `/api/history`, `/api/history/{id}`, `/api/history/{id}/export`, � ����� �������� ������ �������� � ������� ���� �������, ����� Web UI ����� ������� � ������� ��������, � �� ������� �� ����������� `src/old/web.cpp`. (codex)
- `exportProcessToJSON()` � `src/history.cpp` ������ �� �������� ���������: JSON-export ������ �������� ������ ������� ��������, ������� `timeseries`, `results`, `phases` � ����� v2-���� `reasonCode/operatorMessage`. (codex)
- History modal �� ��������� (`src/web/history/details.js`) ������ ���������� ������� ���������� ���� � ������������ �����������, ���� ��� ���� ��������� � history, ��� ��� v2 ��������� ������ ����� ����� �� ������ � live runtime, �� � � UI �������. (codex)
- ������ �������� ������� �� `2.0.13` ��� ��������� ��� ��������, ������� ���������� history API � ���������� runtime � ��������� ������ � ����� v2 history-������ ����� Web UI. (codex)

## [2.0.12] - 2026-03-15

### ��������

- ������� ��������� � `src/history.*` ��������� v2-������ `reasonCode` � `operatorMessage` ��� ��� � ��������������, � �������� ������ JSON-������ ������� ����������������� �� ���� optional-deserialization ����� �����. (codex)
- `ProcessRecorder` ������ �� ����� ���������� ������ ����� `1.3.0`: ����� history-������ ������ ���������� ������� ������� ��������, � warning-������ ���� ����� ������� v2 reason context. (codex)
- `src/control/v2/status_adapter.cpp` ��������� � persistent history recorder: ��� ������ ������ ������������� ����������� history-������, ��� ������� ��������� ����������� ����������� ���� � `reasonCode/operatorMessage`, � ��� ��������� �������� history ����������� � ������ natural finish vs stop/safety stop. (codex)
- ������ �������� ������� �� `2.0.12` ��� ��������� ��� ��������, ������� ��������� v2 ��������� ������ �� live/event log ���� � ���������� process history. (codex)

## [2.0.11] - 2026-03-15

### ��������

- ������� ��������� ������� � `src/storage/logger.cpp` �������� structured-������ ��� v2 phase transition log: JSON `/api/logs/events` ������, ������ ��������� `message`, ����� `kind`, `mode`, `fromPhase`, `toPhase`, `reasonCode` � `operatorMessage`, ���� ������ ���������� ��� `phase_transition`. (codex)
- CSV-�������� recent events ��������� ��������� `kind`, `mode`, `from_phase`, `to_phase`, `reason_code` � `operator_message`, ������� transition log ����� ������������� ��� ����������������� ������ ������ ���������, � �� ������ ��� ������� �����. (codex)
- ������ �������� ������� �� `2.0.11` ��� ��������� ��� ��������, ������� ����������� v2 ��������� ������ �� live runtime � event log export ����. (codex)

## [2.0.10] - 2026-03-15

### ��������

- Browser notifications ���������� �� ��������� v2-aware helper `runtime/process-notifications.js`, ������� ���������� ������� runtime-��������� � ������������� �������, ������� ����������� � �������, ���������� � ����� ����� ������ ��������� �������� � ��� polling, � ��� WebSocket-�����������. (codex)
- ����������� ������ �� �������������� ������ mode/phase: � body ������ ������������� `v2.lastReasonCode` � `v2.operatorMessage`, ��� ��� �������� ����� �� ������ ���� ��������, �� � ��� ������� � �������� ����� v2 ������. (codex)
- `status.js` � `update-ui.js` ������� �� ������������ ad-hoc ������ ����������� � ���������� �� ����� runtime helper, � ������ �������� ������� �� `2.0.10` ��� ��������� ��� �������� process notifications �� v2 contracts. (codex)

## [2.0.9] - 2026-03-15

### ��������

- WebSocket live-status � `webserver.cpp` �������� ������ `v2` safety summary, � cloud tunnel `/api/status` � `cloud_tunnel.cpp` ������ ���� ���������� `v2`-������, ����� frontend ������� ���� � �� �� safety-������ � ��� ��������� streaming, � ��� cloud/polling ������. (codex)
- �� frontend �������� ������ helper `runtime/safety-state.js`, ������� ����������� `alarm + v2.safety` � ����� UI-������; �� ���� ���������� `safety-modal.js` � landing safety chip, ������� ��������� ������ ��������� �������������� `alert`, `acknowledged`, `limited` � `recovery/reset ready`. (codex)
- Landing screen ������ ���������� ����� ������ safety status (`SAFETY OK`, `SAFETY WARN`, `SAFETY LIMITED`, `SAFETY ACKED`, `RESET READY`) ������ ������ ������ `safetyOk`, � ������ �������� ������� �� `2.0.9` ��� ��������� ��� ���������� live safety contract. (codex)

## [2.0.8] - 2026-03-15

### ��������

- Frontend runtime � `globals.js` � `state.js` �������� ��������� `v2` safety-���������, ����� UI ��� ��������� ������� �� `severity`, `reasonCode`, `resetAvailable` � `resetBlockedReason` �������� �� ����� v2 API-�������. (codex)
- `safety-modal.js` ������ �� ��� ��������� ����� status tick ����� `ack/reset`: ������� ����� ��������� JSON-����� action endpoint, ��������� ��������� ������������� � �������� reset � �� ������������� ���� ��� ��� acknowledged alarm ��� ���������� ������. (codex)
- �������� ������ safety modal ������ ������������� ����� ���������� `ack` � `reset` �� �������� recovery/reset ���������, � ������ �������� ������� �� `2.0.8` ��� ��������� ��� ���������� ������ v2 frontend contract. (codex)

## [2.0.7] - 2026-03-15

### ��������

- `/api/safety/ack` � `/api/safety/reset` � `webserver.cpp` � `cloud_tunnel.cpp` ������ ���������� �� ������ legacy `success/alarm`, �� � ��������� ���� `v2` � `severity`, `reasonCode`, `requiresAcknowledge`, `resetAvailable`, `resetBlockedReason`, `safetyLatched` � `lastReasonCode`. (codex)
- ����� `ack/reset` runtime v2 ������������� ����������� ����� `ControlV2::updateRuntime(...)`, ����� ����� API ������� ��� ���������� safety/recovery ���������, � �� ���������� snapshot. (codex)
- ������ �������� ������� �� `2.0.7` ��� ��������� ��� �������� safety action endpoints �� ����� ����� v2 response contract. (codex)

## [2.0.6] - 2026-03-15

### ��������

- `alarm` JSON � `webserver.cpp` � `cloud_tunnel.cpp` �������� ������ `resetAvailable` � `resetBlockedReason`, ����� UI � ������� ������� ����� ��������, ����� �� ��� ��������� `safety reset` ��� ���������� ���������� ��������. (codex)
- `v2.safety` � `/api/status` ������ ���� ��������� ����������� ������ ������ � ������� ���������� reset, ��� ��������� ����� `recovery`-������ � ������������ ��������� ���������. (codex)
- Frontend runtime ������� � ������� ������� safety alarm: `state.js` ������ ����������� `alarm/currentAlarm`, `globals.js` ������ �� � ����� runtime state, � `safety-modal.js` ����������, �������������� �� ������� ������������ ��� ������ reset ��� ����������. (codex)
- ������ �������� ������� �� `2.0.6` ��� ��������� ��� ���������� recovery/reset readiness � API � UI ����. (codex)

## [2.0.5] - 2026-03-15

### ���������

- �������� ��������� read-only `SafetySupervisorV2` � `src/control/v2/safety_supervisor.*`, ������� ���� ������ ������ ������� `activeLimits` � `SafetyDecisionV2` ��� v2 runtime/export ����. (codex)
- � `Safety` ������ helper `canResetNow(...)`, ������������ �� �� ���������� ��������, ��� � �������� `reset`, ����� recovery-������ � v2 �������� �� �� �� safety-������, � �� �� ������������� ���������. (codex)

### ��������

- `status_adapter` ������ �� ������ ��������� safety-���������: ������ `severity`, `reasonCode`, `message`, `requiresAcknowledge` � `activeLimits` ����������� � `SafetySupervisorV2`. (codex)
- `v2` ������ ��������� `latched_trip` � `recovery`: ����� ������ ��� ����� ���� �����, `metrics.safety.severity` ��������� � `recovery`, � `indicators.recoveryActive` ����������� � `true`. (codex)
- ������ �������� ������� �� `2.0.5` ��� ��������� ��� �������� � ������� v2 safety supervisor ����. (codex)

## [2.0.4] - 2026-03-15

### ��������

- `MANUAL_RECT` �������� �� explicit `ReasonCodeV2` ��� ������ ������� ������������ � ���������������� ���������: `manual_rect_handler.cpp` ������ ��� ���������� `notePhaseTransition(...)` � ��������� ��������� � operator message ������ ����� ����� ��������� � ��������. (codex)
- `FSM::startMode()` � `FSM::stopMode()` ��������������� ���, ����� ����� ������ ������������ � � ������ ��������� �������� � v2 transition log ��� ����� �������, � �� ������ ��� ����������-��������� `mode change`. (codex)
- Fallback-������ `status_adapter` ��� `MANUAL_RECT` ��������: ���� explicit transition �� �����-�� ������� �� ������, ������� ������ ��������� ����� ������ � ������������ ��������� ������ ������ �������������� `RC_MANUAL_OPERATOR_SWITCH`. (codex)
- ������ �������� ������� �� `2.0.4` ��� ��������� ��� �������� ������ ������������ �� ����� v2 transition contracts. (codex)

## [2.0.3] - 2026-03-15

### ���������

- `SafetyPolicyV2` �������� ����� policy-������ ��� `MANUAL_RECT`: �������� �������� ������, ������ ������������ ������, cooldown ����� step-down ��������� � ������ ����� �������� ���� � `src/control/v2/safety_policy.*`. (codex)

### ��������

- `manual_rect_handler.cpp` ������ �� �������� ����������� anti-flood �������: handler ������ �������� ������� policy-������� �� `SafetyPolicyV2`, ��������� ��� � ��������� ������� ��������� �� ����������� � ���������� �������� ��������. (codex)
- `ProcessIndicatorsV2` � `status_adapter` ���������� � ��� �� manual rect policy, ������� `powerLimited` � `activeLimits.maxHeaterPowerPercent` ��� `MANUAL_RECT` ������ �������������� �� ��� �� ������, ��� � �������� anti-flood ����������� � runtime. (codex)
- ������ �������� ������� �� `2.0.3` ��� ��������� ��� ��������, ������� ��������� manual rect anti-flood / derating �� mode handler � ����� v2 policy ����. (codex)

## [2.0.2] - 2026-03-15

### ���������

- �������� ����� v2 helper `SafetyPolicyV2` � `src/control/v2/safety_policy.*`, ������� ������������ ������� NBK pressure-derating � ������ � ���������������� ��� handler/runtime/status ����. (codex)

### ��������

- `NBK` ������ �� ������ ����������� ������� �������� �������� � `nbk_handler.cpp`: ������ handler ����������� ������� � ������ policy helper � ��������� ��� ������� `appliedPowerPercent`. (codex)
- `ProcessIndicatorsV2` � `status_adapter` ���������� �� �� �� ����� ��������, ����� ���� `powerLimited`, `activeLimits.maxHeaterPowerPercent` � �������� ����������� �������� ��������� �� ����� � ��� �� ������. (codex)
- ����� v2 ���������� �� ������ `2.0.2` ��� ��������� ��� �������� ������������ �� mode handlers � ����� policy/runtime ����. (codex)

## [2.0.1] - 2026-03-15

### ���������

- � read-only v2 runtime �������� explicit transition bridge `ControlV2::notePhaseTransition(...)`, ����� handlers ����� ���������� ������ ������� ������� ��������� ��� ���������� �� � �������� ����������. (codex)

### ��������

- `RECTIFICATION` �������� �� ����� `ReasonCodeV2` � ������ ��������� `HEATING -> STABILIZATION`, `STABILIZATION -> HEADS`, `HEADS -> POST_HEADS_STABILIZATION`, `POST_HEADS_STABILIZATION -> PURGE`, `PURGE -> BODY`, `BODY -> TAILS`, `TAILS -> FINISH` � `FINISH -> IDLE`. (codex)
- `NBK` �������� �� ����� `ReasonCodeV2` ��� ��������� `HEATING -> STABILIZATION`, `STABILIZATION -> WORKING` � `FINISH -> COMPLETED`, ����� v2 status/logging ������ �� �������� �� ����� ����� ���������. (codex)
- `status_adapter` ������� ���, ����� ����������� ������������ explicit phase transition reason codes, ��������� ���������� ��������� �������� ��� ������ ������ � `IDLE` � ������ ����� ������ ������� �� ������ ��������� ��� fallback. (codex)

## [2.0.0] - 2026-03-15

### ���������

- �������� ������������� v2 groundwork: ��������� �������� `docs/v2/*` � ����� ������ `src/control/v2/*` ��� `reason codes`, `mode contracts`, `process indicators` � `transition logger`. (codex)
- ����� read-only runtime adapter v2, ������� �������� `ProcessIndicatorsV2`, `MetricsSnapshotV2` � `ModeStatusV2` ������ �������� `SystemState` ��� ��������� ��������� �������. (codex)

### ��������

- `/api/status` �������� ����� ������ `v2` � lifecycle, phase token, active limits, command targets, safety state � process indicators ��� ����������� ������ �������� �� ����������� 2.x. (codex)
- ������ �������� ���������� �� ����� `2.0.0`, ����� ������ ��� v2-��������� ��� ��� � ����� �������� �����. (codex)

## [1.13.11] - 2026-03-14

### ����������

- ���������� �������� ���������� ������ ("��������� ���� ����� ��� ���������� � ������ ������� �� ������ ��������"). ��������� ��������� ����� � �������� ������� � API, ���������� ����� UI; ������ �������� ������ ���������� �������� � `/api/pump/calibrate/start` � ��������� `speed`. (claude)

---

## [1.13.10] - 2026-03-14

### ����������

- ���������� ���������� ���� ����� ������������ ������ ��� �������������� ���������� ���������� ������. (gemini)
- ���������� ������ "Calibration already active" ��� ������� ���������� ������� ���������� ����� ������ (�������� �������� `/api/pump/calibrate/cancel`). (gemini)
- ������� ����� ���������� ���������� (������� ��������� � ������� �����). (gemini)

## [1.13.9] - 2026-03-14

...

---

## [1.13.8] - 2026-03-14

### ���������

- ���������� ������� �������� � ��������� ������������������ ������ FreeRTOS (Core 1, Priority: Max-1) ��� ���������� �����-�������� � ������������ (gemini).
- ����������� ����������� ��������� ���������� ������ ��� ���������� �������� ����������� (gemini).

### ����������

- ���������� "������������" ������, ��������� ������������ ���������� � �������� ����� `loop()` (gemini).
- ��������� �������� DemoSimulator � �������� ��������� ������: ������ ��������� ��������� ���������� ����������� ����� `setSpeed` (gemini).
- �������� ������������ UI ��� ������ ������ �� ���� ��������� ��������� ����� (gemini).

---

## [1.13.7] - 2026-03-14

### ����������

- ���������� ����������� ������ ���������� � `src/types.h` (�������������� ������ ������������� ������� `healthScores`) (gemini).
- ���������� ������ �������� (undefined reference) ��� ����������� ������ `SystemHealth` (gemini).
- ��������� ������������ ������ ��������������� � `src/main.cpp` (gemini).

### ���������

- ����������� ����������� ���������� ������� �������� (Health Matrix) � 6 ������������: SENSORS (40%), SAFETY (20%), POWER (20%), WIFI (10%), STORAGE (5%), OTA (5%) (gemini).
- �������� ������� `RebootTracker` ��� ��������� ������� ������ ������������ ESP32-S3 (WDT, Brownout, Exception, SW Reset) (gemini).
- �������� REST API �������� `/api/reboot/status` ��� ���������� �� ���������� ��������� ����������� (gemini).
- ���������� � ������� ������������ ������������� � API `/api/health` ��� ����������� � Web UI (gemini).

---

## [1.13.6] - 2026-03-15

### ����������

- **CRITICAL-FIX** (`watt_control.cpp`, `watt_control.h`): ��������� ������� ������������� ������������ (WDT Reset) ��� ������������� ������������ ����������. ������ ���������� ���������, ������� ������� � �����������, �������� �� ���������� (ISR) � ���������� FreeRTOS ������ � ������� �����������. ��� ������������� ���������� ������� � ������������ ���������� ������. (gemini)
- **�����������**: ���������� ����������������� ����� `WattControl` ��� ���������� ����������, ��������� �������� "ISR -> FreeRTOS Task", ��� ������������� ������ ��������� ��� real-time ������. (gemini)

### ���������

- **Health Matrix**: �������� `SystemHealth` ���������� � ���������� ��������� �� ����������� (�������, ������, �����, �������, �����������, ������������) ��� ����� ������ ������ ��������� ������� (gemini)

---

## [1.13.5] - 2026-03-14

### ���������

- ������������� ��������� **������������ ���������� ��������** (Phase Control) � ���������� �������� ����� ���� (Zero-Cross). (gemini)
- � `config.h` �������� �������� `HEATER_MODE_TRIAC` ��� ����������� �������� ����������, ������������ ���������� `gptimer` �� ESP32-S3. (gemini)
- � `watt_control.cpp` ���������� ���������� ������ ���� ������� (�������� ��������� ���������) �� ������ ��������� �������� � �������� Vrms ���������� ���� ��� ����������� �������� (����� ������-�����). (gemini)

## [1.13.4] - 2026-03-14

### ���������

- ��������� ��������� ����������� ����� ��� � ������ ������ ������������ � ��������� ��� �������. (gemini)

### ����������

- ���������� ������ ������ Demo Mode: ������ ��� ��������� ��������� ������ ������������� ���������� �������� ������������ �������������, �������� FSM ��������� ��������. (gemini)

## [1.13.2] - 2026-03-14

### ���������

- ����������� ������������� ��������� ���� ������������ � Web UI. (gemini)
- ��� ������������� Soft Failure (����� ����������� ��������) ������������ ������ ����� �������: ���������� ������� ��� ���������� ������, ������ �����. (gemini)
- ��������� ���������� ������ ������ `safety-modal.js` � �������� ���� ���������� ����������. (gemini)

## [1.13.1] - 2026-03-14

### ���������

- ����������� ������� �������������� Self-Check: ��� � 30 ����� ����������� ����������� ������, ������� � ������������ ��������. (gemini)
- �������� ���������� ������ �������� (Weighted Health Score): ����������� ������� (���, �����) ������ ����� ��������� 80% ��� ������� �������. (gemini)
- ���������� ��������� ���� CRC-������ ��� ������� �� 7 ������������� �������� �������������. (gemini)

### ��������

- ����� ������ ������������: ����� ��������������� �������� (���, ����������, ��������) ������ �� �������� ����������� ��������� ��������, � ������� ������������� ������������ � UI (Soft Failure). (gemini)

## [1.13.0] - 2026-03-14

### ���������

- **#4** (`fsm.cpp`): `getPhaseProgressPercent()` � `getPhaseTargetSec()` ������ ���������� �������� �������� ��� ���� ������� � Mashing (�� �������� ����), Hold (�� ������������ ����), NBK (�� ����./�������/������), Fermentation (�� �������). ����� ���������� 0.
- **#4** (`types.h`): � `NbkSettings` ��������� ���� `targetVolumeMl` (0=����������), � `FermentationSettings` � `durationHours` (0=���������). ����� Web UI ��� ��������� ����� ������ ��� ����������� ���������.
- **#14** (`main.cpp`): Self-check ��� ������ 30 ����� � ���������� ��������� heap, uptime, ������� ������������ � �������� ������ �������� � `Logger::logf()` � Serial.

### ��� ��������� (������������)

- **#7** `buzzerTask`: WDT �� ��������� � ������ ���� �� `portMAX_DELAY` (���������� ������� FreeRTOS).
- **#9** ���������� Health Score: ��� ���������� � `sensors.cpp::updateHealth()`.

## [1.12.0] - 2026-03-14

### ����������

- **BUG-1** (`sensors.cpp`, `types.h`): `SystemHealth::tempSensorsOk` ��������� � `bool` �� `uint8_t` � Health Score ������ ��������� ������� ���������� ������� ��������, � �� ������ `true/false`.
- **BUG-2** (`heater.cpp`): ������� ������ ���� (ramp) ������ �������� � ��������� ���������� `rampStartPower` ��� ���������� ������������. ����� `currentPower` ��������������� �� ������ ����, ����� ���������������� ������.
- **BUG-3** (`webserver.cpp`): ����� �������� `return "finish"` � `getMashPhaseString()` (dead code).
- **BUG-4** (`sensors.cpp`): ���������� race condition ��� ������ `flowPulseCount` �� ISR � ������� ������ �������� ���������� �� ���� ����������� ������ (`noInterrupts`), � �� �������� 4 ���� � �������������� ����������� �� ����������.
- **BUG-5** (`pump.cpp`): `totalSteps` ������ � `uint32_t` �� `int32_t` � ��������� ������������� ������������ ������ ��� ������� �������������� signed > unsigned.
- **ARCH-2** (`cloud_tunnel.cpp`): � `getModeToken()` ��������� case'� ��� `Mode::NBK` � `Mode::FERMENTATION` � ����� ��� ���������� `"unknown"` �� ��������� �������.
- **ARCH-3** (`valves.cpp`, `valves.h`): ������ ����������� `delay(15)?N + delay(2000)` � `Valves::setFraction()` � `initFractionator()`. ������� �������� ������������ ���������� �� ������������� ������� ����� `millis()` � ������� `Valves::update()` �� loop.
- **PERF-3** (`main.cpp`): `Heater::update()` � `Valves::update()` ������ ���������� � �������� loop � ����� ������� �������� ������� ���� (`rampTo`) ���� �����������, �� ������� �� ����������.

## [1.11.23] - 2026-03-14

### ���������

- ����������� ��������� �������� ���� ����� `BOARD_REV` � ����� ���� `src/pins_config.h`. (gemini)
- �������� ����� ����� TFT "��� �����������" ��� ����������� ���� 7 �������� ������������. (gemini)
- ���������� ����� ������� ���������� ������� (Safe/Normal/Fast). (gemini)
- ��������� �������������� ������� ����� (����� 10 ������). (gemini)

## [1.11.20] - 2026-03-14

### ���������

- ���������� ������������� ������ ����� FreeRTOS ������. (gemini)
- ��������� ��������� `secrets.h` ��� �������� Wi?Fi �������. (gemini)

## [1.11.18] - 2026-03-13

### ��������

- ��������� ������ �������� �� **ArduinoJson 7**. (gemini)

## [1.11.15] - 2026-03-13

### ���������

- ����������� ��������� ����������� FSM (����� ������� � `src/control/modes/`). (gemini)
- ��������� ������� **Reboot Reason Tracking**. (gemini)

[1.13.1]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.13.1
[1.13.0]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.13.0
[1.12.0]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.12.0
[1.11.23]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.23
[1.11.20]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.20
[1.11.18]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.18
[1.11.15]: https://github.com/grifmax/Smart-Column-S3/releases/tag/v1.11.15
