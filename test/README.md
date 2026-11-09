# Test Layout

`native_json_tests` is a lightweight host-side harness for JSON contract checks that
do not need Arduino runtime or ESP32 peripherals.

Run it with:

```powershell
rtk pio test -e native_json_tests
```

Audit direct actuator ownership with:

```powershell
rtk python scripts/direct_output_audit.py
```

The firmware build runs this audit automatically. Direct output writes are
limited to the explicit authoritative control allowlist in the script; API,
Cloud Tunnel, MQTT and TFT/service paths must use `PowerCommandGateway`.

Use this harness for:

- contract checks for JSON payload builders after they are split from hardware code
- stable assertions on required keys, booleans, numbers, and strings
- regression tests for serialization shape without flashing the device
- fraction-program safety gates: route timeout, route settling, level/emergency
  pause and safe resume before collection can restart

Avoid using this harness for:

- code that depends directly on `WiFi`, `ESP`, `AsyncWebServer`, or drivers
- timing-sensitive control logic
- end-to-end HTTP route validation on firmware
