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

  // Calcula Vsat equivalente a partir de un equipo patron (ppm ~= mg/L en
  // agua dulce) y del voltaje medido por este equipo.
  bool calculateReferenceCalibration(float measured_mV,
                                     float reference_ppm,
                                     float reference_temp_c,
                                     float& vsat_mV);
  static bool saturationMgL(float tempC, float& saturation_mgL);
  static bool saturationMgL(float tempC, float pressureHpa,
                            float& saturation_mgL);

  // Calibracion 1 punto (Vsat/Tcal), voltaje en mV, temperatura en C.
  bool setSinglePointCalibration(float Vsat_mV, float Tcal_C);
  void getSinglePointCalibration(float& Vsat_mV, float& Tcal_C) const;
  void clearCalibration();
  bool hasCalibration() const { return calibrated_; }
  bool setAtmosphericPressureHpa(float pressureHpa);
  float atmosphericPressureHpa() const { return pressure_hpa_; }

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

  // Calibracion 1 punto
  float vsat_mV_ = 1600.0f;
  float tcal_c_  = 30.0f;
  float pressure_hpa_ = 1013.0f;
  bool calibrated_ = false;

  float last_do_mgL_ = NAN;
  float last_volts_ = NAN;
  char  last_error_[64] = {0};

  void  setError_(const char* msg);
  bool  readAveragedVolts_(float& volts);
};

#endif // O2_MANAGER_H
