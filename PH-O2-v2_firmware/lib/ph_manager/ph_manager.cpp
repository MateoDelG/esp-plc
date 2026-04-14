#include "pH_manager.h"
#include <string.h>

// Opcional: si vas a usar applyEEPROMCalibration()
#include "eeprom_manager.h"  // define ConfigStore v7 con hasPH3pt/hasPH2pt y getters

PHManager::PHManager(ADS1115Manager* ads, uint8_t ads_channel, uint8_t avgSamples)
: ads_(ads), ch_(ads_channel), avg_(avgSamples) {}

bool PHManager::begin() {
  if (!ads_) {
    setError_("ADS pointer null");
    return false;
  }
  if (avg_ == 0) avg_ = 1;
  last_error_[0] = '\0';
  model_ = CalModel::TWO_PT; // por defecto deja un modelo lineal válido
  return true;
}

// ===== Helpers internos =====
void PHManager::setError_(const char* msg) {
  strncpy(last_error_, msg, sizeof(last_error_) - 1);
  last_error_[sizeof(last_error_) - 1] = '\0';
}

bool PHManager::readAveragedVolts_(float& volts) {
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

bool PHManager::evalLinearAt_(float volts, float tempC, float a_base, float b_base, float& ph_out) {
  const float TK_meas = kelvin_(tempC);
  const float TK_cal  = kelvin_(tcal_c_);
  if (TK_cal < 1.0f) {
    setError_("Tcal inválida");
    return false;
  }
  const float scale = TK_meas / TK_cal;
  const float a_T = a_base * scale;
  ph_out = a_T * volts + b_base;
  return true;
}

// ===== Calibración 2 PUNTOS =====
void PHManager::setTwoPointCalibration(float pH1, float V1, float pH2, float V2, float tCalC) {
  if (fabsf(V2 - V1) < 1e-6f) {
    setError_("Calibración inválida (V1≈V2)");
    return;
  }
  a_cal_ = (pH2 - pH1) / (V2 - V1);
  b_cal_ = pH1 - a_cal_ * V1;
  tcal_c_ = tCalC;
  model_ = CalModel::TWO_PT;
  last_error_[0] = '\0';
}

void PHManager::setCalibrationCoeffs(float a_cal, float b_cal, float tCalC) {
  a_cal_ = a_cal;
  b_cal_ = b_cal;
  tcal_c_ = tCalC;
  model_ = CalModel::TWO_PT; // por coherencia: define una recta única
  last_error_[0] = '\0';
}

void PHManager::getCalibrationCoeffs(float& a_cal, float& b_cal, float& tCalC) const {
  a_cal = a_cal_;
  b_cal = b_cal_;
  tCalC = tcal_c_;
}

// ===== Calibración 3 PUNTOS =====
bool PHManager::setThreePointCalibration(float V4, float V7, float V10, float tCalC, bool piecewise) {
  if (!isfinite(V4) || !isfinite(V7) || !isfinite(V10)) {
    setError_("V4/V7/V10 inválidos");
    return false;
  }
  // Guardamos por trazabilidad
  v4_ = V4; v7_ = V7; v10_ = V10;

  if (piecewise) {
    return build3ptPiecewise_(V4, V7, V10, tCalC);
  } else {
    return build3ptLeastSquares_(V4, V7, V10, tCalC);
  }
}

bool PHManager::build3ptLeastSquares_(float V4, float V7, float V10, float tCalC) {
  // Ajuste lineal por mínimos cuadrados sobre puntos (V, pH):
  // (V4,4), (V7,7), (V10,10)
  // Formula estándar: a = Cov(V,pH)/Var(V); b = mean(pH) - a*mean(V)
  const float x1 = V4,  y1 = 4.0f;
  const float x2 = V7,  y2 = 7.0f;
  const float x3 = V10, y3 = 10.0f;

  const float mx = (x1 + x2 + x3) / 3.0f;
  const float my = (y1 + y2 + y3) / 3.0f;

  float Sxx = (x1-mx)*(x1-mx) + (x2-mx)*(x2-mx) + (x3-mx)*(x3-mx);
  float Sxy = (x1-mx)*(y1-my) + (x2-mx)*(y2-my) + (x3-mx)*(y3-my);

  if (Sxx < 1e-9f) {
    setError_("3pt LS inválido (Sxx≈0)");
    return false;
  }
  a_cal_ = Sxy / Sxx;
  b_cal_ = my - a_cal_ * mx;

  tcal_c_ = tCalC;
  model_  = CalModel::THREE_PT_LS;

  // Anula piecewise para evitar confusión
  a1_pw_ = b1_pw_ = a2_pw_ = b2_pw_ = NAN;
  v_th_lo_ = v_th_hi_ = NAN;

  last_error_[0] = '\0';
  return true;
}

bool PHManager::build3ptPiecewise_(float V4, float V7, float V10, float tCalC) {
  // Construye 2 rectas: [4-7] y [7-10]
  if (fabsf(V7 - V4) < 1e-6f || fabsf(V10 - V7) < 1e-6f) {
    setError_("3pt PW inválido (voltajes coinciden)");
    return false;
  }
  // tramo 1: (V4,4) y (V7,7)
  a1_pw_ = (7.0f - 4.0f) / (V7 - V4);
  b1_pw_ = 4.0f - a1_pw_ * V4;

  // tramo 2: (V7,7) y (V10,10)
  a2_pw_ = (10.0f - 7.0f) / (V10 - V7);
  b2_pw_ = 7.0f - a2_pw_ * V7;

  // Umbrales (mitad de cada tramo en dominio de voltaje)
  v_th_lo_ = 0.5f * (V4 + V7);
  v_th_hi_ = 0.5f * (V7 + V10);

  tcal_c_ = tCalC;
  model_  = CalModel::THREE_PT_PW;

  // Recta global no aplicará; limpia para evitar uso accidental
  a_cal_ = -3.5f;
  b_cal_ = 7.0f;

  last_error_[0] = '\0';
  return true;
}

// ===== Lectura con compensación de temperatura =====
bool PHManager::readPH(float tempC, float& ph, float* voltsOut) {
  float v;
  if (!readAveragedVolts_(v)) {
    return false;
  }
  if (voltsOut) *voltsOut = v;

  switch (model_) {
    case CalModel::TWO_PT:
    case CalModel::THREE_PT_LS: {
      // Un solo par (a_cal_, b_cal_) a Tcal
      if (!evalLinearAt_(v, tempC, a_cal_, b_cal_, ph)) return false;
      break;
    }
    case CalModel::THREE_PT_PW: {
      // Elegir tramo por voltaje y aplicar la recta correspondiente
      if (!(isfinite(a1_pw_) && isfinite(b1_pw_) && isfinite(a2_pw_) && isfinite(b2_pw_))) {
        setError_("3pt PW no configurado");
        return false;
      }
      // Nota: el orden de V vs pH depende del hardware/ganancia. No asumimos monotonicidad ascendente
      // con pH: solo usamos umbrales calculados del propio V4,V7,V10.
      const bool useLow = (v <= v_th_lo_);
      const bool useHi  = (v >= v_th_hi_);
      float a_base, b_base;
      if (useLow) {           // cerca del buffer pH 4
        a_base = a1_pw_; b_base = b1_pw_;
      } else if (useHi) {     // cerca del buffer pH 10
        a_base = a2_pw_; b_base = b2_pw_;
      } else {                // alrededor de pH 7
        // Elegimos tramo según cercanía a V7 o interpolamos suavemente.
        // Para simplicidad: tramo inferior si v < V7, superior si v >= V7
        if (v < v7_) { a_base = a1_pw_; b_base = b1_pw_; }
        else         { a_base = a2_pw_; b_base = b2_pw_; }
      }
      if (!evalLinearAt_(v, tempC, a_base, b_base, ph)) return false;
      break;
    }
    default:
      setError_("Modelo de pH no configurado");
      return false;
  }

  last_ph_ = ph;
  last_error_[0] = '\0';
  return true;
}

// ===== Configuración varias =====
void PHManager::setAveraging(uint8_t n) {
  if (n == 0) n = 1;
  avg_ = n;
}
void PHManager::setChannel(uint8_t ch) {
  ch_ = ch;
}

// ===== Helper opcional: cargar desde EEPROM v7 =====
bool PHManager::applyEEPROMCalibration(const ConfigStore& eeprom) {
  // Se asume v7 con hasPH3pt()/hasPH2pt() y getters V4,V7,V10,tC
  float V4, V7, V10, tC3;
  float v7_2, v4_2, tC2;

  // Prioridad: 3pt si está disponible
  if (eeprom.hasPH3pt()) {
    eeprom.getPH3pt(V4, V7, V10, tC3);  // (V4, V7, V10, tC)
    if (isfinite(V4) && isfinite(V7) && isfinite(V10)) {
      // Uso por defecto LS (más robusto si hay pequeñas no linealidades)
      if (setThreePointCalibration(V4, V7, V10, tC3, /*piecewise=*/false)) {
        return true;
      }
    }
  }

  // Fallback: 2pt
  if (eeprom.hasPH2pt()) {
    eeprom.getPH2pt(v7_2, v4_2, tC2);   // (V7, V4, tC)
    // Define recta con pH1=7 a V1=v7_2 y pH2=4 a V2=v4_2
    setTwoPointCalibration(7.0f, v7_2, 4.0f, v4_2, tC2);
    return true;
  }

  setError_("EEPROM sin calibración pH válida");
  return false;
}
