# USB-PD Charger (TTGO T-Display / ESP32, Arduino + PlatformIO)

Firmware negotiates USB-PD, drives a boost regulator via digipot, limits power
by temperature, shows status on ST7789. `src/main.cpp` owns the `PeriodicTask`
table; one manager per subsystem.

## Docs

`docs/` holds notes written alongside the work: measurements, debug sessions,
dead ends, trial and error. Useful context, and often the fastest route to a
question about observed hardware behavior.

It is a record of what was tried, not a spec of what the firmware must be.
Experiments documented there may have been dropped — e.g. brownout detection
exists in the notes but not in the code. `src/` is the only source of truth for
how the project has to look.

## Build

```text
pio run
pio run -t upload
pio device monitor
```

Settings live in `platformio.ini`.

## Tests

- When adding behavior, fixing a bug, or changing existing behavior, consider whether tests need adding or updating; consult `test/AGENTS.md` for test work.
- Update tests alongside code changes when behavior changes.
- Native regression tests use Unity and run with `pio test -e native`.
- Keep native mocks isolated to the native test environment; production builds must use the real framework and libraries.

## Gotchas

- TFT config lives in `platformio.ini` `build_flags` — never edit `User_Setup.h` manually.
- `lib/AP33772S` is a local fork with PD-contract helpers. Do not replace with upstream.
- RotoPD Pro has two outputs: always-on 5V aux (powers ESP) and switchable PD main. INA measures PD main and is rail truth; the AP33772S voltage register measures 5V aux — never use it for PD logic.

## Conventions

- Add periodic work as `PeriodicTask(intervalMs, fn)` in `main.cpp`.
  `millis()` subtraction is rollover-safe.
- Keep managers decoupled; `main.cpp` wires them. No cross-includes between managers.
- Latch permanent faults via `PDManager::shutdown()` / `DiagLog::event()`, never
  silently continue.
