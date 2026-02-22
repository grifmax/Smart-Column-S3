# TODO 2.0 - Smart-Column S3

## Phase 1. UI reliability and safe control flow (in progress)
- [x] Add WebSocket->HTTP status fallback polling (auto start/stop by WS state).
- [x] Add safe fallback when chart library is unavailable (do not break UI).
- [x] Keep mode/pause state synced from WS and `/api/status`.
- [x] Add explicit active-mode highlighting in Control menu.
- [x] Disable non-active mode buttons while a process is running.
- [x] Add confirmation dialog before switching to another mode.
- [x] Include `paused` in WS fast/full state packets.
- [x] Add E2E smoke test for mode switch and button states (`tools/ui-smoke`).

## Phase 2. Display stability and observability
- [x] Audit display update loop frequency and contention with sensor/IO tasks.
- [x] Add display soft-watchdog (force full redraw after slow-frame burst).
- [x] Add display hard-watchdog (re-init bus/panel on persistent timeout/error burst).
- [x] Reduce redraw scope (partial updates, dirty regions).
- [x] Add frame-time metrics and recovery counters to diagnostics.
- [ ] Add configurable display refresh profile (`normal` / `safe`).

## Phase 3. Telegram channel hardening (FastBot2)
- [ ] Verify reconnect/backoff behavior on unstable Wi-Fi.
- [ ] Add command rate limiting and duplicate suppression.
- [ ] Add telemetry for bot send/poll errors.
- [ ] Add health command with core diagnostics snapshot.

## Phase 4. System stability baseline
- [ ] Introduce unified health matrix (sensors, comms, storage, heap).
- [ ] Add periodic self-check task with event log.
- [ ] Add reboot reason tracking + crash-safe last-state snapshot.
- [ ] Add long-run test scenario (8h+ soak) and acceptance criteria.

## Phase 5. Hardware abstraction readiness
- [ ] Finalize UART/pin mapping profiles (including PZEM on dedicated port).
- [ ] Add compile-time pin profiles for board revisions.
- [ ] Add boot-time pin sanity checks and conflict warnings.

## Phase 6. Distiller parity: cockpit UI + process ergonomics (in progress)
- [ ] Add process phase telemetry API in FSM (elapsed/target/progress) for TFT and Web UI.
- [ ] Upgrade main TFT dashboard to a cockpit layout (mode+phase timer, progress bar, I/O statuses).
- [ ] Add at-a-glance operation strip (voltage, pressure, pump, fractions, uptime) with partial redraws only.
- [ ] Add dedicated mode placeholders and API contracts for `NBK` and `FERMENTATION`.
- [ ] Implement NBK mode state machine (feed/steam control, safety interlocks).
- [ ] Implement fermentation mode (setpoint, hysteresis, scheduling, alarms).

## Phase 7. Mode-specific TFT screens (in progress)
- [x] Stage A: profile-based dashboard for `IDLE` and `RECTIFICATION` (only relevant widgets per mode).
- [ ] Stage B: profile-based dashboard for `DISTILLATION` and `MANUAL_RECT`.
- [ ] Stage C: profile-based dashboard for `MASHING` and `HOLD`.
- [ ] Unify action rows/buttons by mode (show only valid controls).
- [ ] Add a dedicated "Temperatures" detail page with all connected probes.
