#include "o2_manager.h"
#include <string.h>

// Opcional: si vas a usar applyEEPROMCalibration()
#include "eeprom_manager.h"

namespace {
constexpr float O2_CAL_MIN_MV = 1.0f;
constexpr float O2_CAL_MAX_MV = 4096.0f;
constexpr float O2_CAL_MIN_TEMP_C = 0.0f;
constexpr float O2_CAL_MAX_TEMP_C = 40.0f;
constexpr float O2_REFERENCE_MIN_PPM = 0.10f;
constexpr float O2_REFERENCE_MAX_PPM = 20.0f;
constexpr float O2_MIN_PRESSURE_HPA = 500.0f;
constexpr float O2_MAX_PRESSURE_HPA = 1100.0f;
constexpr float O2_SEA_LEVEL_PRESSURE_HPA = 1013.25f;
}

static const uint16_t DO_Table[41] = {
  14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
  11260, 11010, 10770, 10530, 10300, 10080,  9860,  9660,  9460,  9270,
   9080,  8900,  8730,  8570,  8410,  8250,  8110,  7960,  7820,  7690,
   7560,  7430,  7300,  7180,  7070,  6950,  6840,  6730,  6630,  6530,
   6410
};

O2Manager::O2Manager(ADS1115Manager* ads, uint8_t ads_channel, uint8_t avgSamples)
: ads_(ads), ch_(ads_channel), avg_(avgSamples) {}

bool O2Manager::begin() {
  if (!ads_) {
    setError_("ADS pointer null");
    return false;
  }
  if (avg_ == 0) avg_ = 1;
  last_error_[0] = '\0';
  return true;
}

void O2Manager::setError_(const char* msg) {
  strncpy(last_error_, msg, sizeof(last_error_) - 1);
  last_error_[sizeof(last_error_) - 1] = '\0';
}

bool O2Manager::readAveragedVolts_(float& volts) {
  if (!ads_) {
    setError_("ADS pointer null");
    return false;
  }
  float acc = 0.0f;
  float v = 0.0f;
  for (uint8_t i = 0; i < avg_; ++i) {
    if (!ads_->readSingle(ch_, v)) {
      setError_("ADS read fail");
      return false;
    }
    acc += v;
  }
  volts = acc / (float)avg_;
  last_volts_ = volts;
  return true;
}

bool O2Manager::saturationMgL(float tempC, float& saturation_mgL) {
  return saturationMgL(tempC, O2_SEA_LEVEL_PRESSURE_HPA,
                       saturation_mgL);
}

bool O2Manager::saturationMgL(float tempC, float pressureHpa,
                              float& saturation_mgL) {
  if (!isfinite(tempC) || tempC < O2_CAL_MIN_TEMP_C ||
      tempC > O2_CAL_MAX_TEMP_C || !isfinite(pressureHpa) ||
      pressureHpa < O2_MIN_PRESSURE_HPA ||
      pressureHpa > O2_MAX_PRESSURE_HPA) {
    return false;
  }

  const uint8_t lower = (uint8_t)floorf(tempC);
  const uint8_t upper = (lower < 40) ? lower + 1 : lower;
  const float fraction = tempC - (float)lower;
  const float lower_mgL = (float)DO_Table[lower] / 1000.0f;
  const float upper_mgL = (float)DO_Table[upper] / 1000.0f;
  const float seaLevelSaturation =
      lower_mgL + fraction * (upper_mgL - lower_mgL);
  const float vaporPressureHpa =
      6.1121f * expf((18.678f - tempC / 234.5f) *
                     (tempC / (257.14f + tempC)));
  const float drySeaLevelPressure =
      O2_SEA_LEVEL_PRESSURE_HPA - vaporPressureHpa;
  const float dryLocalPressure = pressureHpa - vaporPressureHpa;
  if (drySeaLevelPressure <= 0.0f || dryLocalPressure <= 0.0f) {
    return false;
  }
  saturation_mgL = seaLevelSaturation *
                   (dryLocalPressure / drySeaLevelPressure);
  return true;
}

bool O2Manager::readDO(float tempC, float& do_mgL, float* voltsOut) {
  if (!calibrated_) {
    setError_("Calibracion O2 requerida");
    return false;
  }
  float volts = 0.0f;
  if (!readAveragedVolts_(volts)) return false;
  if (voltsOut) *voltsOut = volts;

  const float mv = volts * 1000.0f;
  float saturation_mgL = NAN;
  if (!saturationMgL(tempC, pressure_hpa_, saturation_mgL)) {
    setError_("Temperatura/presion O2 invalida");
    return false;
  }

  const float v_saturation = vsat_mV_ + 35.0f * (tempC - tcal_c_);

  if (v_saturation <= 0.0f) {
    setError_("V_saturation invalido");
    return false;
  }

  do_mgL = (mv * saturation_mgL) / v_saturation;

  last_do_mgL_ = do_mgL;
  last_error_[0] = '\0';
  return true;
}

bool O2Manager::setSinglePointCalibration(float Vsat_mV, float Tcal_C) {
  if (!isfinite(Vsat_mV) || Vsat_mV < O2_CAL_MIN_MV ||
      Vsat_mV > O2_CAL_MAX_MV) {
    setError_("Voltaje cal O2 invalido");
    return false;
  }
  if (!isfinite(Tcal_C) || Tcal_C < O2_CAL_MIN_TEMP_C ||
      Tcal_C > O2_CAL_MAX_TEMP_C) {
    setError_("Temperatura cal O2 invalida");
    return false;
  }
  vsat_mV_ = Vsat_mV;
  tcal_c_  = Tcal_C;
  calibrated_ = true;
  last_error_[0] = '\0';
  return true;
}

bool O2Manager::calculateReferenceCalibration(float measured_mV,
                                              float reference_ppm,
                                              float reference_temp_c,
                                              float& vsat_mV) {
  if (!isfinite(measured_mV) || measured_mV < O2_CAL_MIN_MV ||
      measured_mV > O2_CAL_MAX_MV) {
    setError_("Voltaje patron O2 invalido");
    return false;
  }
  if (!isfinite(reference_ppm) || reference_ppm < O2_REFERENCE_MIN_PPM ||
      reference_ppm > O2_REFERENCE_MAX_PPM) {
    setError_("PPM patron O2 invalido");
    return false;
  }

  float saturation_mgL = NAN;
  if (!saturationMgL(reference_temp_c, pressure_hpa_, saturation_mgL)) {
    setError_("Temperatura/presion patron invalida");
    return false;
  }

  const float calculated_vsat = measured_mV * saturation_mgL / reference_ppm;
  if (!isfinite(calculated_vsat) || calculated_vsat < O2_CAL_MIN_MV ||
      calculated_vsat > O2_CAL_MAX_MV) {
    setError_("Vsat patron fuera de rango");
    return false;
  }

  vsat_mV = calculated_vsat;
  last_error_[0] = '\0';
  return true;
}

void O2Manager::clearCalibration() {
  calibrated_ = false;
  last_do_mgL_ = NAN;
  last_error_[0] = '\0';
}

bool O2Manager::setAtmosphericPressureHpa(float pressureHpa) {
  if (!isfinite(pressureHpa) || pressureHpa < O2_MIN_PRESSURE_HPA ||
      pressureHpa > O2_MAX_PRESSURE_HPA) {
    setError_("Presion O2 fuera de rango");
    return false;
  }
  pressure_hpa_ = pressureHpa;
  last_error_[0] = '\0';
  return true;
}

void O2Manager::getSinglePointCalibration(float& Vsat_mV, float& Tcal_C) const {
  Vsat_mV = vsat_mV_;
  Tcal_C = tcal_c_;
}

void O2Manager::setAveraging(uint8_t n) {
  if (n == 0) n = 1;
  avg_ = n;
}

void O2Manager::setChannel(uint8_t ch) {
  ch_ = ch;
}

bool O2Manager::applyEEPROMCalibration(const ConfigStore& eeprom) {
  float V1, T1, V2, T2;
  if (!eeprom.hasO2Cal()) {
    setError_("EEPROM sin calibracion O2 valida");
    return false;
  }
  eeprom.getO2Cal(V1, T1, V2, T2);
  (void)V2;
  (void)T2;
  return setSinglePointCalibration(V1, T1);
}
