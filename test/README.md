# Test Layout

`native_json_tests` is a lightweight host-side harness for JSON contract checks that
do not need Arduino runtime or ESP32 peripherals.

Run it with:

```powershell
rtk pio test -e native_json_tests
```

Use this harness for:

- contract checks for JSON payload builders after they are split from hardware code
- stable assertions on required keys, booleans, numbers, and strings
- regression tests for serialization shape without flashing the device

Avoid using this harness for:

- code that depends directly on `WiFi`, `ESP`, `AsyncWebServer`, or drivers
- timing-sensitive control logic
- end-to-end HTTP route validation on firmware
