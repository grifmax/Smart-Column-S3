# Fraction program API contract

The fraction program has one serialised profile shape and one runtime shape.
The local HTTP API and cloud tunnel expose the same fields; profiles are stored
through the shared `profiles.cpp` serializer and parser.

## Profile shape

`distillation.fractionProgram` has `schemaVersion`, `enabled`, `stepCount`,
`heatingTemperatureSensorIndex`, `heatingTargetTemperatureC`, and `steps`.
Each step has `name`, `routeIndex`, `pumpRateMlH`, `heaterPowerW`, operator
confirmation fields, end-condition fields, temperature-sensor fields and
`allowManualAdvance`.

Both `PUT /api/profiles/{id}` endpoints (local and cloud) parse the program
with `parseFractionProgramJson`. Profile export/import uses the same shared
serializer.

## Runtime shape

Top-level `fractionProgram` in `GET /api/status` has `enabled`, `active`,
`currentStep`, `waitingForConfirmation`, `routing`, `lastEndReason`, route
feedback, actual rate, collected volume, confirmation prompt and active-step
criteria. `distillation.fractionProgram` carries the configured profile shape.

Before collection the common coordinator requires: safety OK, no pause, a
ready route and the configured route-settling interval. A route timeout,
container level, emergency stop or ordinary pause therefore leaves collection
stopped; resume rechecks the same gate.
