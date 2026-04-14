# AGENTS

This repository is an ESP32 Arduino firmware project using PlatformIO.
Use the guidance below when making changes or running commands.

## Build, Upload, Lint, Test

Project environment is defined in `platformio.ini`.
Default env: `esp32doit-devkit-v1`.
Monitor speed: `115200` (matches `platformio.ini`).
Extra scripts: `scripts/auto_uploader.py` picks serial first, OTA fallback.

Common commands (run from `PH-O2-v2_firmware/` repo root):
- Build firmware: `pio run -e esp32doit-devkit-v1`
- Upload firmware: `pio run -e esp32doit-devkit-v1 -t upload`
- Clean build: `pio run -e esp32doit-devkit-v1 -t clean`
- Monitor serial: `pio device monitor -e esp32doit-devkit-v1`
- List PlatformIO envs: `pio project config`
- Verbose build output: `pio run -e esp32doit-devkit-v1 -v`

Testing with PlatformIO Test Runner:
- Run all tests: `pio test -e esp32doit-devkit-v1`
- Run a single test folder:
  `pio test -e esp32doit-devkit-v1 -f test/master_uart_simulation`
  (you can also pass just the suite name: `-f master_uart_simulation`)
- Test filtering is path- or suite-based; keep suite names unique.

Static analysis (if needed):
- `pio check -e esp32doit-devkit-v1`
  Note: there is no explicit configuration file in this repo.

Upload notes:
- `platformio.ini` uses `scripts/auto_uploader.py` to prefer serial
  and fall back to OTA when available.
- Default OTA host is `ph-remote.local` (configurable via
  `custom_ota_host` in `platformio.ini`).
- OTA auth is optional (`custom_ota_auth`), currently commented out.

## Repository Layout

- `src/` main firmware entry (`main.cpp`) and core logic.
- `include/` shared headers such as `globals.h`.
- `lib/` local libraries (project-specific) and vendored third-party libs.
- `test/` PlatformIO tests (each suite has a `main.cpp`).
- `scripts/` helper scripts for tooling (e.g., upload selection).
- `platformio.ini` is the single source for build/upload settings.

## Code Style and Conventions

General
- Language: C++ (Arduino framework, ESP32).
- Do not use exceptions; prefer `bool` return + `lastError()` getters.
- Avoid heavy dynamic allocation in hot paths; prefer stack buffers or
  fixed-size arrays (`char[]`, `snprintf`).
- Use `F("...")` for constant strings stored in flash when appropriate.
- Use `const`/`constexpr` for constants; avoid macros for typed values.
- Prefer explicit `uint8_t`, `uint16_t`, `uint32_t`, `int32_t` types for
  hardware-facing code.
- Clamp and validate sensor values before use (see `readPH()`/`readO2()`).
- Keep task loops short; always include `vTaskDelay` to yield CPU.

Formatting
- Keep existing formatting in each file; do not reformat unrelated lines.
- Indentation in `src/` is commonly 2 spaces (follow local file style).
- Braces are K&R style in most project files.
- Long calls are often wrapped with aligned parameters; keep readability.
- For LCD UI or fixed-width output, use `snprintf` with 16x2 buffers.

Includes
- Project headers first; prefer quotes: `#include "..."`.
- Some files use `#include <globals.h>` due to include paths; keep the
  existing style within the file for consistency.
- Third-party and Arduino headers after, with angle brackets.
- Keep include order stable within a file; avoid unused includes.
- Prefer `#include <Arduino.h>` in `.cpp` files needing core types/macros.

Naming
- Classes and structs: `PascalCase` (e.g., `PumpsManager`).
- Methods and functions: `lowerCamelCase` (e.g., `readSingleRaw`).
- Enums: `enum class` with `PascalCase` values.
- Constants/macros: `UPPER_SNAKE_CASE` (e.g., `TELNET_HOSTNAME`).
- Private members often end with `_` (e.g., `gain_`, `shadow_`).
- Task handles follow `TaskCoreN` style; keep globals obvious and minimal.

Globals and Singletons
- Global objects are declared in `include/globals.h` and defined once in
  `src/globals.cpp`. Do not create additional global definitions elsewhere.
- Prefer dependency injection where practical (pass pointers or refs).
- If you must use `extern`, keep declarations in headers only.

Error Handling and Logging
- Return `false` on failure and set `last_error_` or similar fields.
- Use `remoteManager.log(...)` for user-visible logging.
- Keep user-facing messages short; avoid large allocations.
- Log calibration and EEPROM updates when changing persistent state.
- Avoid `String` concatenation in failure paths; prefer `snprintf`.

Timing and IO
- Use `delay(...)` sparingly and document why when used.
- For sensor reads, validate ranges before using results (see `readPH()`).
- When handling buttons, clear latches after consuming events.
- Treat buttons as edge-triggered; call `Buttons::testButtons()` before reads.

EEPROM and Persistent Data
- Follow `ConfigStore` patterns: read -> validate -> write -> `save()`.
- Ensure any calibration write updates both runtime values and EEPROM.
- If write fails, log `lastError()` and keep runtime state consistent.

JSON Handling
- Use `ArduinoJson` with `JsonDocument` and handle `DeserializationError`.
- Validate keys before reading; clamp values to safe ranges.
- Avoid large documents; keep payloads small and defensive.

Concurrency (ESP32)
- Tasks are created with `xTaskCreatePinnedToCore`.
- Keep task loops short and include `vTaskDelay` to avoid hogging CPU.
- Avoid shared mutable state without clear ownership or guards.

Configuration and Hardware
- Pin and sensor constants live in `include/globals.h`; keep changes minimal.
- Prefer explicit pin mode setup in one place (see `initHardware()`).
- I2C addresses and bus pins are set near device init (see `initPumps()`).
- When using LCD, keep strings <= 16 chars per row; use fixed buffers.
- Use `isfinite`/`isnan` checks for sensor values before logging/using.

## Working with `lib/`

- `lib/` contains both local modules and vendored third-party libraries.
- Avoid editing vendored third-party code unless necessary for a fix.
- Prefer changes in `src/` or project-owned `lib/<name>/` modules.
- If a fix is needed in vendored code, document why in the commit/PR.

## Testing Guidance

- Tests live in `test/<suite>/main.cpp`.
- Use the `-f` flag to run a single test suite.
- Some tests are hardware-dependent; document any requirements in
  `test/<suite>/README` if you add new tests.
- When adding tests, keep them deterministic and short for embedded targets.

## Cursor/Copilot Rules

- No Cursor rules found in `.cursor/rules/` or `.cursorrules`.
- No Copilot rules found in `.github/copilot-instructions.md`.

## Safety and Hygiene

- Do not commit or edit secrets; prefer `platformio.ini` for config.
- Be cautious with pin definitions and hardware constants in
  `include/globals.h`.
- Avoid changing OTA or serial settings without a clear rationale.
- Avoid `String` churn in tight loops; prefer stack buffers when possible.
