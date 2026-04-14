#pragma once
#include <Arduino.h>
#include <EEPROM.h>

class ConfigStore {
public:
  static constexpr size_t kWifiSsidMax = 32;
  static constexpr size_t kWifiPassMax = 64;

  // Inicializa EEPROM (ESP32). eepromSize sugerido: 256 bytes.
  bool begin(size_t eepromSize = 256, uint16_t baseAddr = 0);

  // Carga y guarda configuración
  bool load();
  bool save();

  // Restablece a valores por defecto
  void resetDefaults();

  // --- ADC ---
  void setADC(float scale, float offset);
  void getADC(float& scale, float& offset) const;

  // --- pH 2 puntos (V7, V4, tCalC) ---
  void setPH2pt(float V7, float V4, float tCalC);
  void getPH2pt(float& V7, float& V4, float& tCalC) const;

  // --- pH 3 puntos (V4, V7, V10, tCalC) ---
  void setPH3pt(float V4, float V7, float V10, float tCalC);
  void getPH3pt(float& V4, float& V7, float& V10, float& tCalC) const;

  // Helpers para disponibilidad de calibraciones
  bool hasPH2pt() const;  // true si V7 y V4 no son NaN
  bool hasPH3pt() const;  // true si V4, V7 y V10 no son NaN

  // --- O2 (2 puntos: V1/T1 y V2/T2) ---
  void setO2Cal(float V1_mV, float T1_C, float V2_mV, float T2_C);
  void getO2Cal(float& V1_mV, float& T1_C, float& V2_mV, float& T2_C) const;
  bool hasO2Cal() const;

  // --- WiFi ---
  void setWifiCredentials(const char* ssid, const char* pass);
  void getWifiCredentials(char* ssidOut, size_t ssidLen,
                          char* passOut, size_t passLen) const;
  bool hasWifiCredentials() const;
  void setWifiAutoReconnect(bool enabled);
  bool wifiAutoReconnect() const;

  // === FILL TIMES (duraciones planificadas) ===
  void     setFillTimes(uint32_t kcl_ms, uint32_t h2o_ms, uint32_t sample_ms);
  void     getFillTimes(uint32_t& kcl_ms, uint32_t& h2o_ms, uint32_t& sample_ms) const;

  void     setKclFillMs(uint32_t ms);
  void     setH2oFillMs(uint32_t ms);
  void     setSampleFillMs(uint32_t ms);

  uint32_t kclFillMs() const;
  uint32_t h2oFillMs() const;
  uint32_t sampleFillMs() const;

  // === DRAIN (duración planificada) ===
  void     setDrainMs(uint32_t ms);
  uint32_t drainMs() const;

  // === TIMEOUTS (solo sample y drain) ===
  void     setSampleTimeoutMs(uint32_t ms);
  void     setDrainTimeoutMs(uint32_t ms);

  uint32_t sampleTimeoutMs() const;
  uint32_t drainTimeoutMs() const;

  // === Nº de bombas SAMPLE (módulo fijo, 0..4) ===
  void     setSampleCount(uint8_t n);
  uint8_t  sampleCount() const;

  // --- Stabilization (ms) ---
  void     setO2StabilizationMs(uint32_t ms);
  uint32_t o2StabilizationMs() const;
  void     setPhStabilizationMs(uint32_t ms);
  uint32_t phStabilizationMs() const;

  // Último error
  const char* lastError() const { return _err; }

  // Versión del layout actual
  static constexpr uint16_t kVersion = 0x000A;  // v10: add wifi credentials

private:
  // ====== Estructuras ======
  struct Pair  { float scale; float offset; };
  struct PH2pt { float V7; float V4; float tC; };
  struct PH3pt { float V4; float V7; float V10; float tC; };
  struct O2Cal { float V1_mV; float T1_C; float V2_mV; float T2_C; };

  // ====== Estructura persistente (v10) ======
  struct ConfigData {
    uint16_t magic;     // 0xC0AD
    uint16_t version;   // kVersion

    Pair     adc;       // Calibración ADC

    // Calibraciones pH
    PH2pt    ph2pt;     // 2 puntos (opcional)
    PH3pt    ph3pt;     // 3 puntos (opcional)

    // Calibración O2 (2 puntos)
    O2Cal    o2cal;

    // Credenciales WiFi
    char     wifi_ssid[kWifiSsidMax + 1];
    char     wifi_pass[kWifiPassMax + 1];
    uint8_t  wifi_auto_reconnect;

    // Fill/Drain planificados
    uint32_t kcl_fill_ms;
    uint32_t h2o_fill_ms;
    uint32_t sample_fill_ms;
    uint32_t drain_ms;

    // Timeouts (solo sample/drain)
    uint32_t sample_timeout_ms;
    uint32_t drain_timeout_ms;

    // Otros
    uint8_t  sample_count;       // 0..4 bombas sample
    uint32_t o2_stabilization_ms; // Espera de mezcla/estabilización O2
    uint32_t ph_stabilization_ms; // Espera de estabilización pH

    uint32_t crc;                // CRC32 (sin incluir este campo)
  } __attribute__((packed));

  // ====== Constantes y helpers ======
  static constexpr uint16_t kMagic = 0xC0AD;

  static uint32_t crc32(const uint8_t* data, size_t len);
  void computeCrc_();

  // Clamps internos
  static uint32_t clampMs(uint32_t ms, uint32_t lo=100, uint32_t hi=600000);
  static uint32_t clampTimeout(uint32_t ms) { return clampMs(ms, 100, 600000); }
  static uint32_t clampFill(uint32_t ms)    { return clampMs(ms, 100, 600000); }
  static uint32_t clampDrain(uint32_t ms)   { return clampMs(ms, 500, 600000); }

  // ====== Estado interno ======
  size_t     _eepromSize = 0;
  uint16_t   _base = 0;
  ConfigData _cfg{};
  char       _err[64] = {0};
};
