# UART Protocol (NDJSON) - PH-O2 Firmware

This document describes the UART protocol used between this firmware
and an external device. It is based on the implementation in
`lib/UART_manager/` and the test harness in `test/master_uart_simulation/`.

## Transport and Framing

- Interface: UART (ESP32 Serial2).
- Baud rate: 115200.
- Framing: 8N1.
- Message framing: one JSON object per line (NDJSON).
- Line terminator: `\n` (CR `\r` is accepted and ignored).
- Max line length: 1024 characters (longer lines are rejected).

## General Message Format

Requests are JSON objects with an `op` field and optional `data`.

```
{"op":"<operation>","data":{...}}
```

Responses always include `ok: true|false`. On errors, `error` is set.

```
{"ok":true,"data":{...}}
{"ok":false,"error":"ERROR_CODE"}
```

## Supported Operations

### 1) get_status
Returns general status, level sensors, and sample values.

Request:
```
{"op":"get_status"}
```

Response (success):
```
{
  "ok": true,
  "data": {
    "level_sensors": {
      "h2o": true
    },
    "auto_running": false,
    "auto_req": false,
    "samples": [
      {"id":1,"ph_val":7.02,"o2_val":7.18},
      {"id":2,"ph_val":null,"o2_val":null},
      {"id":3,"ph_val":null,"o2_val":null},
      {"id":4,"ph_val":null,"o2_val":null}
    ]
  }
}
```

### 2) get_last
Returns the most recent measurement snapshot.

Request:
```
{"op":"get_last"}
```

Response (success):
```
{
  "ok": true,
  "data": {
    "ph": 7.01,
    "tempC": 25.3,
    "level_sensors": {
      "h2o": true
    },
    "samples": [
      {"id":1,"ph_val":7.01,"o2_val":7.20},
      {"id":2,"ph_val":null,"o2_val":null},
      {"id":3,"ph_val":null,"o2_val":null},
      {"id":4,"ph_val":null,"o2_val":null}
    ],
    "result": "OK"
  }
}
```

Response (no data yet):
```
{"ok":false,"error":"NO_DATA"}
```

### 3) auto_measure
Requests starting an auto measurement cycle.

Request:
```
{"op":"auto_measure"}
```

Response (accepted):
```
{"ok":true}
```

Response (busy):
```
{"ok":false,"error":"BUSY"}
```

## Error Codes

- `BAD_JSON`: line is not valid JSON.
- `BAD_ARGS`: line is too long (>1024 chars).
- `BAD_OP`: unsupported `op` value.
- `NO_DATA`: no measurement snapshot is available yet.
- `BUSY`: auto measurement already running.

## Field Notes

- `level_sensors.h2o`: true if the H2O level sensor is OK.
- `level_sensors.kcl` is NOT sent over UART (removed from protocol).
- `samples`: always 4 entries with ids 1..4.
- `ph_val` and `o2_val` can be `null` when no value is available.
- `result`: last operation result string (e.g., calibration tags).
- `auto_running`: true while auto mode is executing.
- `auto_req`: true if an auto measurement was requested.

## Recommended Usage Flow (External Device)

1) On startup, send `get_status` to verify connectivity.
2) If you need a measurement, send `auto_measure`.
3) Poll `get_status` or `get_last` until `auto_running` is false.
4) Read `get_last` for the final snapshot.

Timing guidance:
- Use small delays (100-200 ms) between commands.
- If no response arrives, resend the request (idempotent ops).

## Example Session

TX:
```
{"op":"get_status"}
```

RX:
```
{"ok":true,"data":{"level_sensors":{"h2o":true},"auto_running":false,"auto_req":false,"samples":[{"id":1,"ph_val":null,"o2_val":null},{"id":2,"ph_val":null,"o2_val":null},{"id":3,"ph_val":null,"o2_val":null},{"id":4,"ph_val":null,"o2_val":null}]}}
```

TX:
```
{"op":"auto_measure"}
```

RX:
```
{"ok":true}
```

TX:
```
{"op":"get_last"}
```

RX (after auto finished):
```
{"ok":true,"data":{"ph":7.03,"tempC":25.2,"level_sensors":{"h2o":true},"samples":[{"id":1,"ph_val":7.03,"o2_val":7.21},{"id":2,"ph_val":null,"o2_val":null},{"id":3,"ph_val":null,"o2_val":null},{"id":4,"ph_val":null,"o2_val":null}],"result":"OK"}}
```

## Integration Notes

- The parser trims whitespace and ignores characters before the first `{`.
- Always end each JSON object with a newline.
- Do not rely on field order.
- Expect `null` values and handle them safely.

## Test Harness

See `test/master_uart_simulation/main.cpp` for a simple UART master that
sends commands and prints responses on USB Serial.
