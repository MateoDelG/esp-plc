# AGENTS

This file guides agentic coding tools working in this repository. It focuses
on the PlatformIO firmware projects, especially `test-modem-firmware`.

## Scope and layout

- Workspace root: `D:\Teo\IUDigital\001-Proyectos Telemetria\esp-plc\test-modem-firmware`
- Primary firmware project in this context: `test-modem-firmware`
- Key folders (PlatformIO):
  - `src/` application code
  - `include/` project headers
  - `lib/` vendored or third-party libraries
  - `test/` PlatformIO unit tests

Note: The `lib/` folder includes third-party code (e.g., ArduinoJson, WiFiManager).
Avoid modifying those unless the task explicitly requires it.

## Build, upload, lint, test

All commands below are run from `test-modem-firmware/` unless noted.

### Build

- Build default environment:
  - `pio run`
- Build the configured environment:
  - `pio run -e upesy_wrover`
- Clean build artifacts:
  - `pio run -t clean`

### Upload (if hardware is connected)

- Upload firmware:
  - `pio run -e upesy_wrover -t upload`
- Monitor serial:
  - `pio device monitor`

### Lint / static analysis

- PlatformIO check (Cppcheck/Clang-Tidy if configured):
  - `pio check`
  - `pio check -e upesy_wrover`

If `pio check` is not configured for this project, note that in your response
and skip it or propose adding a configuration.


## Code style guidelines

These guidelines reflect the current code style in `test-modem-firmware/src`.
Follow existing patterns when you touch nearby code.

### Formatting

- Indentation: 2 spaces.
- Braces: same-line style (K&R).
- Keep functions small; prefer early returns for error handling.
- Line length: keep under ~100 chars when practical.
- Use ASCII only unless a file already uses non-ASCII characters.

### Includes and ordering

- Prefer `<Arduino.h>` at the top of Arduino translation units.
- Group includes from standard library, framework, then project headers.
- Avoid unnecessary includes; keep the list minimal.

### Naming

- Functions and variables: `lowerCamelCase`.
- Types (classes, structs, enums): `UpperCamelCase`.
- Constants and macros: `UPPER_SNAKE_CASE`.
- File names: keep existing conventions; new files should be concise and
  descriptive.

### Types and memory

- Prefer fixed-width integer types (`uint8_t`, `int32_t`, etc.) for hardware
  or protocol data.
- Avoid dynamic allocation on constrained devices unless necessary.
- Use `String` sparingly; prefer `char` buffers when parsing or formatting.

### Error handling and logging

- Check return values from hardware and network APIs.
- Prefer explicit error codes or `bool` results for small functions.
- Log only what is needed for diagnostics; use `Serial` for debugging and
  keep logs gated if they are noisy.
- Do not spin in infinite loops for recoverable failures; attempt retries with
  backoff when feasible.

### Concurrency and timing

- Avoid blocking delays in `loop()` if the device must stay responsive.
- Use `millis()`-based scheduling for periodic tasks.
- Keep ISR routines minimal; avoid heavy work in interrupts.

### Tests

- Place unit tests under `test/` using PlatformIO Unity conventions.
- Name test suites to match the folder name, and keep tests deterministic.

## Repo-specific notes

- PlatformIO configuration lives in `test-modem-firmware/platformio.ini`.
  The active environment is `upesy_wrover`.
- There are multiple firmware subprojects at the workspace root; scope changes
  to the subproject requested by the user.

## Cursor / Copilot rules

- No `.cursor/rules/`, `.cursorrules`, or `.github/copilot-instructions.md`
  files were found in this repository at the time this file was created.
