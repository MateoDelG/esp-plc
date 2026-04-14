#ifndef O2_MANAGER_H
#define O2_MANAGER_H

#include <Arduino.h>
#include <math.h>
#include "ADS1115_manager.h"

class ConfigStore;

class O2Manager {
public:
  explicit O2Manager(ADS1115Manager* ads,
                     uint8_t ads_channel = 1,
                     uint8_t avgSamples = 8);

  bool begin();

  // Lectura de O2 disuelto (mg/L) con compensacion por temperatura.
  bool readDO(float tempC, float& do_mgL, float* volts = nullptr);

  // Calibracion 2 puntos (V1/T1 y V2/T2), voltajes en mV, temperaturas en C.
  void setTwoPointCalibration(float V1_mV, float T1_C, float V2_mV, float T2_C);
  // Calibracion 1 punto (Vsat/Tcal), voltaje en mV, temperatura en C.
  void setSinglePointCalibration(float Vsat_mV, float Tcal_C);
  void getTwoPointCalibration(float& V1_mV, float& T1_C, float& V2_mV, float& T2_C) const;

  // Utilidades
  void setAveraging(uint8_t n); // n>=1
  void setChannel(uint8_t ch);  // 0..3

  float lastDO() const { return last_do_mgL_; }
  float lastVolts() const { return last_volts_; }
  const char* lastError() const { return last_error_; }

  bool applyEEPROMCalibration(const ConfigStore& eeprom);

private:
  ADS1115Manager* ads_ = nullptr;
  uint8_t ch_ = 1;
  uint8_t avg_ = 8;

  // Calibracion 2 puntos
  float v1_mV_ = 1600.0f;
  float t1_c_  = 25.0f;
  float v2_mV_ = 1300.0f;
  float t2_c_  = 15.0f;

  enum class CalMode : uint8_t {
    NONE,
    ONE_POINT,
    TWO_POINT
  };
  CalMode mode_ = CalMode::TWO_POINT;

  // Calibracion 1 punto
  float vsat_mV_ = 1600.0f;
  float tcal_c_  = 25.0f;

  float last_do_mgL_ = NAN;
  float last_volts_ = NAN;
  char  last_error_[64] = {0};

  void  setError_(const char* msg);
  bool  readAveragedVolts_(float& volts);
  uint8_t clampTempIndex_(float tempC) const;
};

#endif // O2_MANAGER_H
