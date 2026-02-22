# TODO 2.0 - Smart-Column S3

## Phase 1. UI reliability and safe control flow (in progress)
- [x] Add WebSocket->HTTP status fallback polling (auto start/stop by WS state).
- [x] Add safe fallback when chart library is unavailable (do not break UI).
- [x] Keep mode/pause state synced from WS and `/api/status`.
- [x] Add explicit active-mode highlighting in Control menu.
- [x] Disable non-active mode buttons while a process is running.
- [x] Add confirmation dialog before switching to another mode.
- [x] Include `paused` in WS fast/full state packets.
- [ ] Add E2E smoke test for mode switch and button states.

## Phase 2. Display stability and observability
- [ ] Audit display update loop frequency and contention with sensor/IO tasks.
- [ ] Add display watchdog (re-init on bus timeout/error burst).
- [ ] Reduce redraw scope (partial updates, dirty regions).
- [ ] Add frame-time metrics and dropped-frame counters to diagnostics.
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
