#include "eeprom_manager.h"
#include <string.h>
#include <stddef.h>
#include <math.h>

// ===== Privados =====
static constexpr uint16_t kMagic = 0xC0AD;

uint32_t ConfigStore::clampMs(uint32_t ms, uint32_t lo, uint32_t hi) {
  if (ms < lo) ms = lo;
  if (ms > hi) ms = hi;
  return ms;
}

// ===== CRC32 =====
uint32_t ConfigStore::crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i)
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
  }
  return ~crc;
}

void ConfigStore::computeCrc_() {
  const size_t len = offsetof(ConfigData, crc);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&_cfg);
  _cfg.crc = crc32(bytes, len);
}

// ===== API =====
bool ConfigStore::begin(size_t eepromSize, uint16_t baseAddr) {
  _eepromSize = eepromSize;
  _base = baseAddr;
  if (!EEPROM.begin(_eepromSize)) {
    strncpy(_err, "EEPROM.begin() fallo", sizeof(_err)-1);
    _err[sizeof(_err)-1] = '\0';
    return false;
  }
  _err[0] = '\0';
  return true;
}

bool ConfigStore::load() {
  uint16_t magic = 0, version = 0;
  EEPROM.get(_base + offsetof(ConfigData, magic),   magic);
  EEPROM.get(_base + offsetof(ConfigData, version), version);

  if (magic != kMagic) {
    strncpy(_err, "MAGIC invalido", sizeof(_err)-1);
    return false;
  }

  if (version == 0x000A) {
    ConfigDataV10 oldCfg{};
    EEPROM.get(_base, oldCfg);
    const size_t oldLen = offsetof(ConfigDataV10, crc);
    const uint32_t oldCrc =
        crc32(reinterpret_cast<const uint8_t*>(&oldCfg), oldLen);
    if (oldCrc != oldCfg.crc) {
      strncpy(_err, "CRC v10 invalido", sizeof(_err)-1);
      return false;
    }

    static_assert(offsetof(ConfigData, temperature_offset_c) ==
                      offsetof(ConfigDataV10, crc),
                  "El prefijo v10 debe permanecer compatible");
    memset(&_cfg, 0, sizeof(_cfg));
    memcpy(&_cfg, &oldCfg, oldLen);
    _cfg.magic = kMagic;
    _cfg.version = kVersion;
    _cfg.temperature_offset_c = 0.0f;
    _cfg.o2_measurement_offset_mg_l = 0.0f;
    _cfg.o2_atmospheric_pressure_hpa = 1013.0f;

    if (!save()) {
      _migrationPending = true;
      strncpy(_err, "Migracion v10 a v13 no persistida", sizeof(_err)-1);
      _err[sizeof(_err)-1] = '\0';
    }
    return true;
  }

  if (version == 0x000B) {
    ConfigDataV11 oldCfg{};
    EEPROM.get(_base, oldCfg);
    const size_t oldLen = offsetof(ConfigDataV11, crc);
    const uint32_t oldCrc =
        crc32(reinterpret_cast<const uint8_t*>(&oldCfg), oldLen);
    if (oldCrc != oldCfg.crc) {
      strncpy(_err, "CRC v11 invalido", sizeof(_err)-1);
      return false;
    }

    static_assert(offsetof(ConfigData, o2_measurement_offset_mg_l) ==
                      offsetof(ConfigDataV11, crc),
                  "El prefijo v11 debe permanecer compatible");
    memset(&_cfg, 0, sizeof(_cfg));
    memcpy(&_cfg, &oldCfg, oldLen);
    _cfg.magic = kMagic;
    _cfg.version = kVersion;
    _cfg.o2_measurement_offset_mg_l = 0.0f;
    _cfg.o2_atmospheric_pressure_hpa = 1013.0f;

    if (!save()) {
      _migrationPending = true;
      strncpy(_err, "Migracion v11 a v13 no persistida", sizeof(_err)-1);
      _err[sizeof(_err)-1] = '\0';
    }
    return true;
  }

  if (version == 0x000C) {
    ConfigDataV12 oldCfg{};
    EEPROM.get(_base, oldCfg);
    const size_t oldLen = offsetof(ConfigDataV12, crc);
    const uint32_t oldCrc =
        crc32(reinterpret_cast<const uint8_t*>(&oldCfg), oldLen);
    if (oldCrc != oldCfg.crc) {
      strncpy(_err, "CRC v12 invalido", sizeof(_err)-1);
      return false;
    }

    static_assert(offsetof(ConfigData, o2_atmospheric_pressure_hpa) ==
                      offsetof(ConfigDataV12, crc),
                  "El prefijo v12 debe permanecer compatible");
    memset(&_cfg, 0, sizeof(_cfg));
    memcpy(&_cfg, &oldCfg, oldLen);
    _cfg.magic = kMagic;
    _cfg.version = kVersion;
    _cfg.o2_atmospheric_pressure_hpa = 1013.0f;

    if (!save()) {
      _migrationPending = true;
      strncpy(_err, "Migracion v12 a v13 no persistida", sizeof(_err)-1);
      _err[sizeof(_err)-1] = '\0';
    }
    return true;
  }

  if (version != kVersion) {
    strncpy(_err, "VERSION distinta (no soportada)", sizeof(_err)-1);
    return false;
  }

  ConfigData tmp{};
  EEPROM.get(_base, tmp);

  // Verificar CRC del bloque leído
  const size_t len = offsetof(ConfigData, crc);
  const uint32_t calc = crc32(reinterpret_cast<const uint8_t*>(&tmp), len);
  if (calc != tmp.crc) {
    strncpy(_err, "CRC invalido", sizeof(_err)-1);
    return false;
  }

  _cfg = tmp;
  _migrationPending = false;
  _err[0] = '\0';
  return true;
}

bool ConfigStore::save() {
  _cfg.magic   = kMagic;
  _cfg.version = kVersion;
  computeCrc_();
  EEPROM.put(_base, _cfg);
  if (!EEPROM.commit()) {
    strncpy(_err, "EEPROM.commit() fallo", sizeof(_err)-1);
    return false;
  }
  _migrationPending = false;
  _err[0] = '\0';
  return true;
}

void ConfigStore::resetDefaults() {
  memset(&_cfg, 0, sizeof(_cfg));
  _cfg.magic   = kMagic;
  _cfg.version = kVersion;

  // ADC
  _cfg.adc.scale  = 1.0f;
  _cfg.adc.offset = 0.0f;

  // pH 2pt sin calibrar
  _cfg.ph2pt.V7 = NAN;
  _cfg.ph2pt.V4 = NAN;
  _cfg.ph2pt.tC = NAN;

  // pH 3pt sin calibrar
  _cfg.ph3pt.V4  = NAN;
  _cfg.ph3pt.V7  = NAN;
  _cfg.ph3pt.V10 = NAN;
  _cfg.ph3pt.tC  = NAN;

  // O2 2pt sin calibrar
  _cfg.o2cal.V1_mV = NAN;
  _cfg.o2cal.T1_C  = NAN;
  _cfg.o2cal.V2_mV = NAN;
  _cfg.o2cal.T2_C  = NAN;

  // WiFi credentials vacias
  _cfg.wifi_ssid[0] = '\0';
  _cfg.wifi_pass[0] = '\0';
  _cfg.wifi_auto_reconnect = 1;

  // FILL TIMES por defecto (ms)
  _cfg.kcl_fill_ms    = 3000;
  _cfg.h2o_fill_ms    = 3000;
  _cfg.sample_fill_ms = 3000;
  _cfg.drain_ms       = 15000;

  // TIMEOUTS por defecto (ms) (solo sample y drain)
  _cfg.sample_timeout_ms = 3000;
  _cfg.drain_timeout_ms  = 15000;

  // Nº SAMPLE (módulo fijo)
  _cfg.sample_count = 4;

  // Stabilization (ms)
  _cfg.o2_stabilization_ms = 30000;
  _cfg.ph_stabilization_ms = 30000;

  _cfg.temperature_offset_c = 0.0f;
  _cfg.o2_measurement_offset_mg_l = 0.0f;
  _cfg.o2_atmospheric_pressure_hpa = 1013.0f;

  computeCrc_();
  _err[0] = '\0';
}

// ---- ADC ----
void ConfigStore::setADC(float s, float o) {
  _cfg.adc.scale  = s;
  _cfg.adc.offset = o;
}
void ConfigStore::getADC(float& s, float& o) const {
  s = _cfg.adc.scale;
  o = _cfg.adc.offset;
}

// ---- pH 2pt ----
void ConfigStore::setPH2pt(float V7, float V4, float tCalC) {
  _cfg.ph2pt.V7 = V7;
  _cfg.ph2pt.V4 = V4;
  _cfg.ph2pt.tC = tCalC;
}
void ConfigStore::getPH2pt(float& V7, float& V4, float& tCalC) const {
  V7   = _cfg.ph2pt.V7;
  V4   = _cfg.ph2pt.V4;
  tCalC= _cfg.ph2pt.tC;
}

// ---- pH 3pt ----
void ConfigStore::setPH3pt(float V4, float V7, float V10, float tCalC) {
  _cfg.ph3pt.V4  = V4;
  _cfg.ph3pt.V7  = V7;
  _cfg.ph3pt.V10 = V10;
  _cfg.ph3pt.tC  = tCalC;
}
void ConfigStore::getPH3pt(float& V4, float& V7, float& V10, float& tCalC) const {
  V4   = _cfg.ph3pt.V4;
  V7   = _cfg.ph3pt.V7;
  V10  = _cfg.ph3pt.V10;
  tCalC= _cfg.ph3pt.tC;
}
bool ConfigStore::hasPH2pt() const {
  return !(isnan(_cfg.ph2pt.V7) || isnan(_cfg.ph2pt.V4));
}
bool ConfigStore::hasPH3pt() const {
  return !(isnan(_cfg.ph3pt.V4) || isnan(_cfg.ph3pt.V7) || isnan(_cfg.ph3pt.V10));
}

// ---- O2 2pt ----
void ConfigStore::setO2Cal(float V1_mV, float T1_C, float V2_mV, float T2_C) {
  lock_();
  _cfg.o2cal.V1_mV = V1_mV;
  _cfg.o2cal.T1_C  = T1_C;
  _cfg.o2cal.V2_mV = V2_mV;
  _cfg.o2cal.T2_C  = T2_C;
  unlock_();
}
void ConfigStore::getO2Cal(float& V1_mV, float& T1_C, float& V2_mV, float& T2_C) const {
  lock_();
  V1_mV = _cfg.o2cal.V1_mV;
  T1_C  = _cfg.o2cal.T1_C;
  V2_mV = _cfg.o2cal.V2_mV;
  T2_C  = _cfg.o2cal.T2_C;
  unlock_();
}
bool ConfigStore::hasO2Cal() const {
  lock_();
  const bool valid =
      isfinite(_cfg.o2cal.V1_mV) && _cfg.o2cal.V1_mV >= 1.0f &&
      _cfg.o2cal.V1_mV <= 4096.0f && isfinite(_cfg.o2cal.T1_C) &&
      _cfg.o2cal.T1_C >= 0.0f && _cfg.o2cal.T1_C <= 40.0f;
  unlock_();
  return valid;
}

// ---- Temperature offset ----
void ConfigStore::setTemperatureOffsetC(float offsetC) {
  lock_();
  _cfg.temperature_offset_c = offsetC;
  unlock_();
}

float ConfigStore::temperatureOffsetC() const {
  lock_();
  const float value = _cfg.temperature_offset_c;
  unlock_();
  return value;
}

// ---- O2 measurement offset ----
void ConfigStore::setO2MeasurementOffsetMgL(float offsetMgL) {
  lock_();
  _cfg.o2_measurement_offset_mg_l = offsetMgL;
  unlock_();
}

float ConfigStore::o2MeasurementOffsetMgL() const {
  lock_();
  const float value = _cfg.o2_measurement_offset_mg_l;
  unlock_();
  return value;
}

// ---- O2 atmospheric pressure ----
void ConfigStore::setO2AtmosphericPressureHpa(float pressureHpa) {
  lock_();
  _cfg.o2_atmospheric_pressure_hpa = pressureHpa;
  unlock_();
}

float ConfigStore::o2AtmosphericPressureHpa() const {
  lock_();
  const float value = _cfg.o2_atmospheric_pressure_hpa;
  unlock_();
  return value;
}

// ---- WiFi ----
void ConfigStore::setWifiCredentials(const char* ssid, const char* pass) {
  if (!ssid) ssid = "";
  if (!pass) pass = "";
  strncpy(_cfg.wifi_ssid, ssid, sizeof(_cfg.wifi_ssid) - 1);
  _cfg.wifi_ssid[sizeof(_cfg.wifi_ssid) - 1] = '\0';
  strncpy(_cfg.wifi_pass, pass, sizeof(_cfg.wifi_pass) - 1);
  _cfg.wifi_pass[sizeof(_cfg.wifi_pass) - 1] = '\0';
}

void ConfigStore::getWifiCredentials(char* ssidOut, size_t ssidLen,
                                     char* passOut, size_t passLen) const {
  if (ssidOut && ssidLen > 0) {
    strncpy(ssidOut, _cfg.wifi_ssid, ssidLen - 1);
    ssidOut[ssidLen - 1] = '\0';
  }
  if (passOut && passLen > 0) {
    strncpy(passOut, _cfg.wifi_pass, passLen - 1);
    passOut[passLen - 1] = '\0';
  }
}

bool ConfigStore::hasWifiCredentials() const {
  return _cfg.wifi_ssid[0] != '\0';
}

void ConfigStore::setWifiAutoReconnect(bool enabled) {
  _cfg.wifi_auto_reconnect = enabled ? 1 : 0;
}

bool ConfigStore::wifiAutoReconnect() const {
  return _cfg.wifi_auto_reconnect != 0;
}

// ---- FILL TIMES ----
void ConfigStore::setFillTimes(uint32_t kcl_ms, uint32_t h2o_ms, uint32_t sample_ms) {
  _cfg.kcl_fill_ms    = clampFill(kcl_ms);
  _cfg.h2o_fill_ms    = clampFill(h2o_ms);
  _cfg.sample_fill_ms = clampFill(sample_ms);
}
void ConfigStore::getFillTimes(uint32_t& kcl_ms, uint32_t& h2o_ms, uint32_t& sample_ms) const {
  kcl_ms    = _cfg.kcl_fill_ms;
  h2o_ms    = _cfg.h2o_fill_ms;
  sample_ms = _cfg.sample_fill_ms;
}
void ConfigStore::setKclFillMs(uint32_t ms)    { _cfg.kcl_fill_ms    = clampFill(ms); }
void ConfigStore::setH2oFillMs(uint32_t ms)    { _cfg.h2o_fill_ms    = clampFill(ms); }
void ConfigStore::setSampleFillMs(uint32_t ms) { _cfg.sample_fill_ms = clampFill(ms); }
uint32_t ConfigStore::kclFillMs()    const { return _cfg.kcl_fill_ms; }
uint32_t ConfigStore::h2oFillMs()    const { return _cfg.h2o_fill_ms; }
uint32_t ConfigStore::sampleFillMs() const { return _cfg.sample_fill_ms; }

// ---- DRAIN planned duration + timeout ----
void ConfigStore::setDrainMs(uint32_t ms) { _cfg.drain_ms = clampDrain(ms); }
uint32_t ConfigStore::drainMs() const     { return _cfg.drain_ms; }

// ---- TIMEOUTS (solo sample & drain) ----
void ConfigStore::setSampleTimeoutMs(uint32_t ms) { _cfg.sample_timeout_ms = clampTimeout(ms); }
void ConfigStore::setDrainTimeoutMs(uint32_t ms)  { _cfg.drain_timeout_ms  = clampTimeout(ms); }
uint32_t ConfigStore::sampleTimeoutMs() const     { return _cfg.sample_timeout_ms; }
uint32_t ConfigStore::drainTimeoutMs()  const     { return _cfg.drain_timeout_ms; }

// ---- SAMPLE COUNT ----
void    ConfigStore::setSampleCount(uint8_t n) { _cfg.sample_count = n; }
uint8_t ConfigStore::sampleCount() const       { return _cfg.sample_count; }

// ---- Stabilization ----
void ConfigStore::setO2StabilizationMs(uint32_t ms) { _cfg.o2_stabilization_ms = ms; }
uint32_t ConfigStore::o2StabilizationMs() const     { return _cfg.o2_stabilization_ms; }
void ConfigStore::setPhStabilizationMs(uint32_t ms) { _cfg.ph_stabilization_ms = ms; }
uint32_t ConfigStore::phStabilizationMs() const     { return _cfg.ph_stabilization_ms; }
