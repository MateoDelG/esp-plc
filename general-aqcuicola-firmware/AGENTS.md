# AGENTS.md

This repository is a PlatformIO Arduino firmware project for ESP32.
Use this guide when making changes or running tooling.

## Repository Map
- `platformio.ini`: PlatformIO environments and board config.
- `src/`: Firmware entry point (`main.cpp`).
- `include/`: Project headers.
- `lib/`: Vendored or local libraries.
- `test/`: PlatformIO unit tests (if present).

## Dependency Management
- Prefer PlatformIO `lib_deps` in `platformio.ini` for new external libraries.
- If a library must be vendored, place it under `lib/` with clear naming.
- Avoid modifying third-party library sources unless required to fix a bug.
- If you patch a vendored library, document the change in a local README.

## Build, Upload, Lint, Test
Use PlatformIO CLI (`pio`). Run from the repo root.

### Build
- Build all default environments: `pio run`
- Build a specific environment: `pio run -e esp-wrover-kit`
- Clean build artifacts: `pio run -t clean`
- Build with verbose output when diagnosing toolchain issues: `pio run -v`

### Upload / Flash
- Upload firmware: `pio run -e esp-wrover-kit -t upload`
- Upload and open serial monitor: `pio run -e esp-wrover-kit -t upload -t monitor`
- Upload to a specific port (example): `pio run -e esp-wrover-kit -t upload --upload-port COM5`

### Serial Monitor
- Open monitor: `pio device monitor`
- With baud rate (example): `pio device monitor -b 115200`
- Close other serial tools before running the monitor (Port locked errors).

### Lint / Static Analysis
- No dedicated lint config found.
- Optional static analysis (if tools installed): `pio check -e esp-wrover-kit`
  - Configure checks in `platformio.ini` if needed.
- Avoid running analysis on vendored libs unless required.

### Tests
- Run all tests: `pio test -e esp-wrover-kit`
- Run a single test folder by name:
  - `pio test -e esp-wrover-kit --filter "test_*"`
  - Use a specific test name like `test_foo`.
- When adding new tests, prefer a dedicated folder under `test/`.

### Debug
- PlatformIO generates VS Code debug configs in `.vscode/launch.json`.
- Do not manually edit generated debug files; regenerate via PlatformIO if needed.

## Cursor / Copilot Rules
- No `.cursorrules`, `.cursor/rules/`, or `.github/copilot-instructions.md` found.

## Code Style Guidelines
The codebase is minimal and follows typical Arduino + C++ conventions.
When adding new code, keep style consistent and embedded-friendly.

### File Organization
- Keep `setup()` and `loop()` in `src/main.cpp` as the entry point.
- Put reusable classes in `lib/<module>/` or `include/` + `src/` pairs.
- Avoid editing vendored libraries in `lib/*-master` unless required.
- Keep module interfaces small; prefer single-purpose headers.
- If you add a module, include a short README in its folder when non-obvious.

### Includes
- Use `#include <Arduino.h>` in translation units using Arduino APIs.
- Order includes:
  1. C/C++ standard headers.
  2. Framework headers (Arduino, ESP32 SDK).
  3. Project headers (`"MyModule.h"`).
- Prefer forward declarations in headers to reduce compile time.
- Use header guards or `#pragma once` for project headers.
- Avoid including heavy Arduino or SDK headers in public headers when possible.

### Formatting
- Indentation: 2 spaces (matches current `main.cpp`).
- Braces: K&R style (`if (...) {` on same line).
- One statement per line; avoid deep nesting.
- Max line length: keep around 100–120 when reasonable.
- Use blank lines to separate logical blocks; avoid extra vertical whitespace.
- Keep `setup()` and `loop()` short by delegating to helpers.

### Naming Conventions
- Types/classes/structs: `PascalCase`.
- Functions/methods: `camelCase`.
- Variables: `camelCase`.
- Constants: `kCamelCase` or `ALL_CAPS` for macros.
- File names: `snake_case` or `kebab-case` are both acceptable; stay consistent within a module.
- Enums: `PascalCase` enum type with `kCamelCase` enumerators.
- Boolean helpers: use `isX`, `hasX`, `shouldX`.

### Types and Memory
- Prefer fixed-width types for hardware-facing data (`uint8_t`, `uint16_t`).
- Use `size_t` for sizes and indices where applicable.
- Minimize dynamic allocation (`new`, `malloc`) on embedded targets.
- Prefer `constexpr`, `const`, and `static` for compile-time or file-scope constants.
- Avoid Arduino `String` in tight loops; prefer `char` buffers when needed.
- Avoid recursion and large stack allocations on ESP32.
- Use `PROGMEM` for large constant tables when appropriate.

### Error Handling and Logging
- Favor early returns for error conditions.
- Return status via `bool`/`enum` when possible.
- Use `Serial` logging sparingly in production paths; gate with build flags if needed.
- Avoid blocking delays in error paths when the device must keep running.
- Prefer structured log prefixes (`[modem]`, `[mqtt]`) for clarity.

### Configuration and Build Flags
- Store build flags in `platformio.ini` instead of source files.
- Keep per-environment settings under `[env:<name>]` sections.
- Prefer `build_flags` for compile-time feature toggles.
- Document required hardware pins or peripherals in code comments near usage.

### Tests
- Place unit tests in `test/<test_name>/` using PlatformIO conventions.
- Keep tests deterministic and hardware-aware (mock where possible).
- Avoid tests that depend on live network or cloud services by default.
- For hardware-required tests, document setup in the test folder README.

### Platform-Specific Notes
- Target environment: `esp-wrover-kit` (ESP32, Arduino framework).
- Verify pin mappings and peripherals before refactors.
- Prefer non-blocking timing (`millis()`) for periodic work.
- Watch for watchdog resets if loops become too heavy.

### Timing and Concurrency
- Avoid long blocking delays in `loop()`; split work across ticks.
- For periodic tasks, use elapsed time checks instead of `delay()`.
- Keep ISR work minimal; defer processing to the main loop.
- Protect shared state accessed from ISRs with `volatile` and minimal critical sections.

## Contribution Tips for Agents
- If you add a new library, document it in `platformio.ini` or `lib/`.
- Keep public headers minimal and stable.
- Prefer small, testable functions with clear side effects.
- When uncertain, follow the patterns in existing modules under `lib/`.
- If you update wiring or peripherals, note it in comments near the pin definitions.
- Avoid reformatting unrelated files; keep diffs focused.
