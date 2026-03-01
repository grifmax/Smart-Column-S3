# TODO-anime: Integration Report

## Scope

- Runtime modals with quick-step controls:
  - Manual heater power (`Вт` and `%`)
  - Manual pump speed
  - Water autostart threshold by `T_cube`
  - Safety thresholds (`pressure`, `TSA`, `water_out`) from live indicators
- Runtime state synchronization for equipment/safety fields from both nested and flat payload variants.
- Backend payload aliases for `/api/status` and WebSocket packets.

## Validation

- Frontend lint:
  - `npx eslint src/web/runtime/edit-modal.js src/web/runtime/bars.js src/web/runtime/state.js`
  - Result: `OK`

- Firmware build:
  - `pio run -e esp32s3`
  - Result: `SUCCESS`
  - Memory summary:
    - RAM: `18.2%` (`59656 / 327680`)
    - Flash: `46.3%` (`1820965 / 3932160`)

## Notes

- Build completed with existing non-blocking ArduinoJson deprecation warnings in `webserver.cpp`.
- Functional UI verification in a live browser target is pending until final FS upload session.
