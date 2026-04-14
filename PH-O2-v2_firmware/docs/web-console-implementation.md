# Web Console Implementation Guide

This document explains how the web console is implemented and how log
messages are broadcast to all connected browser clients.

## Components

- HTTP server that serves the dashboard HTML.
- WebSocket server that broadcasts log lines.
- Log queue + ring buffer to store and forward messages.

## Key Files

- `src/services/console/console_service.h`
- `src/services/console/console_service.cpp`
- `src/services/console/log_buffer.h`
- `src/services/console/log_buffer.cpp`
- `src/services/console/pages/console_fragment.h`
- `src/services/console/pages/dashboard_page.h`
- `src/services/console/pages/dashboard_style.h`
- `src/services/console/pages/dashboard_script.h`

## How the Dashboard HTML is Built

In `ConsoleService::begin()` the dashboard page is assembled by
concatenating:

- `kDashboardStyle` (CSS)
- `kDashboardPage` (main layout)
- `kConsoleFragment` (console UI)
- `kDashboardScript` (client JS)

The server then responds on `/` with that HTML.

## Broadcast Flow (How Logs Appear in the Web Console)

### 1) Logger sink wiring

The firmware logger is wired to the console sink:

```
logger_.setSink(ConsoleService::logSink);
```

### 2) Enqueue to the log queue

`ConsoleService::logSink()` enqueues each line to a FreeRTOS queue.

### 3) Update loop drains queue and broadcasts

In `ConsoleService::update()`:

- The queue is drained.
- Each line is pushed into `LogBuffer`.
- Each line is broadcast to all WebSocket clients:

```
ws_.broadcastTXT(line);
```

### 4) Browser receives messages

In `dashboard_script.h`, the browser opens a WebSocket to port 81 and
appends each message to the `#console` element.

## Showing Custom Messages

To show any message in the web console, use the logger:

```
logger_.info("[uart1] rx json");
```

The broadcast path will forward it automatically.

## Notes

- The queue has a fixed depth (64). Avoid unbounded log spam.
- The log buffer stores a fixed number of lines for new clients.
- Messages are line-based and should be short.
