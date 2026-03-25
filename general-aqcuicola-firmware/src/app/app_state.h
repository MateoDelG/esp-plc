#pragma once

enum class AppState {
  Boot,
  ConnectingWifi,
  WifiReady,
  OtaReady,
  Running,
  Error
};
