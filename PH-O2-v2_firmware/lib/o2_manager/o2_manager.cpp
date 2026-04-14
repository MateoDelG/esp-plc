#include "o2_manager.h"
#include <string.h>

// Opcional: si vas a usar applyEEPROMCalibration()
#include "eeprom_manager.h"

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

uint8_t O2Manager::clampTempIndex_(float tempC) const {
  int t = (int)(tempC + 0.5f);
  if (t < 0) t = 0;
  if (t > 40) t = 40;
  return (uint8_t)t;
}

bool O2Manager::readDO(float tempC, float& do_mgL, float* voltsOut) {
  float volts = 0.0f;
  if (!readAveragedVolts_(volts)) return false;
  if (voltsOut) *voltsOut = volts;

  const uint8_t tIndex = clampTempIndex_(tempC);
  const float mv = volts * 1000.0f;

  float v_saturation = NAN;
  if (mode_ == CalMode::ONE_POINT) {
    v_saturation = vsat_mV_ + 35.0f * (tempC - tcal_c_);
  } else if (mode_ == CalMode::TWO_POINT) {
    if (fabsf(t1_c_ - t2_c_) < 1e-6f) {
      setError_("Calibracion O2 invalida (T1==T2)");
      return false;
    }
    v_saturation = (tempC - t2_c_) * (v1_mV_ - v2_mV_) / (t1_c_ - t2_c_) + v2_mV_;
  } else {
    setError_("Calibracion O2 no configurada");
    return false;
  }

  if (v_saturation <= 0.0f) {
    setError_("V_saturation invalido");
    return false;
  }

  const float do_raw = (mv * (float)DO_Table[tIndex]) / v_saturation;
  do_mgL = do_raw / 1000.0f;

  last_do_mgL_ = do_mgL;
  last_error_[0] = '\0';
  return true;
}

void O2Manager::setTwoPointCalibration(float V1_mV, float T1_C, float V2_mV, float T2_C) {
  v1_mV_ = V1_mV;
  t1_c_  = T1_C;
  v2_mV_ = V2_mV;
  t2_c_  = T2_C;
  mode_ = CalMode::TWO_POINT;
  last_error_[0] = '\0';
}

void O2Manager::setSinglePointCalibration(float Vsat_mV, float Tcal_C) {
  vsat_mV_ = Vsat_mV;
  tcal_c_  = Tcal_C;
  mode_ = CalMode::ONE_POINT;
  last_error_[0] = '\0';
}

void O2Manager::getTwoPointCalibration(float& V1_mV, float& T1_C, float& V2_mV, float& T2_C) const {
  V1_mV = v1_mV_;
  T1_C  = t1_c_;
  V2_mV = v2_mV_;
  T2_C  = t2_c_;
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
  const bool v1ok = isfinite(V1) && isfinite(T1);
  const bool v2ok = isfinite(V2) && isfinite(T2);
  if (v1ok && v2ok) {
    setTwoPointCalibration(V1, T1, V2, T2);
    return true;
  }
  if (v1ok && !v2ok) {
    setSinglePointCalibration(V1, T1);
    return true;
  }
  setError_("EEPROM O2 invalido");
  return false;
}
