#include "ADS1115_manager.h"
#include <globals.h>
#include <cstring> // strncpy

ADS1115Manager::ADS1115Manager(uint8_t i2c_addr, TwoWire* wire)
: addr_(i2c_addr), wire_(wire) {}

bool ADS1115Manager::begin() {
  connected_ = ads_.begin(addr_, wire_);
  if (!connected_) {
    setError_("ADS1115 no responde");
    return false;
  }
  ads_.setGain(gain_);
  ads_.setDataRate(rate_);
  last_error_[0] = '\0';
  return true;
}

void ADS1115Manager::setGain(adsGain_t g) {
  gain_ = g;
  if (connected_) ads_.setGain(gain_);
}

void ADS1115Manager::setDataRate(uint16_t r) {
  rate_ = r;
  if (connected_) ads_.setDataRate(rate_);
}

void ADS1115Manager::setAveraging(uint8_t n) {
  if (n == 0) n = 1;
  avg_ = n;
}

void ADS1115Manager::setCalibration(float scale, float offset) {
  cal_scale_  = scale;
  cal_offset_ = offset;
}

bool ADS1115Manager::checkChannel_(uint8_t ch) {
  if (ch > 3) {
    setError_("Canal inválido (0..3)");
    return false;
  }
  if (!connected_) {
    setError_("ADS1115 no inicializado");
    return false;
  }
  return true;
}

void ADS1115Manager::setError_(const char* msg) {
  strncpy(last_error_, msg, sizeof(last_error_) - 1);
  last_error_[sizeof(last_error_) - 1] = '\0';
}

bool ADS1115Manager::readOnceRawSingle_(uint8_t ch, int16_t& raw) {
  switch (ch) {
    case 0: raw = ads_.readADC_SingleEnded(0); return true;
    case 1: raw = ads_.readADC_SingleEnded(1); return true;
    case 2: raw = ads_.readADC_SingleEnded(2); return true;
    case 3: raw = ads_.readADC_SingleEnded(3); return true;
    default: return false;
  }
}

bool ADS1115Manager::readOnceRawDiff_(uint8_t pair, int16_t& raw) {
  if (pair == 0) {           // A0-A1
    raw = ads_.readADC_Differential_0_1();
    return true;
  } else if (pair == 1) {    // A2-A3
    raw = ads_.readADC_Differential_2_3();
    return true;
  }
  return false;
}

//mediana aritmética de n lecturas single-ended
// bool ADS1115Manager::readSingleRaw(uint8_t channel, int16_t& raw) {
//   if (!checkChannel_(channel)) return false;

//   // ---- Dummy read para asentamiento ----
//   int16_t throwaway;
//   if (!readOnceRawSingle_(channel, throwaway)) {
//     setError_("Lectura dummy falló");
//     return false;
//   }

//   // ---- Lecturas promediadas ----
//   int32_t acc = 0;
//   for (uint8_t i = 0; i < avg_; ++i) {
//     int16_t r;
//     if (!readOnceRawSingle_(channel, r)) {
//       setError_("Lectura raw falló");
//       return false;
//     }
//     acc += r;
//   }

//   last_raw_ = static_cast<int16_t>(acc / (int32_t)avg_);
//   last_error_[0] = '\0';
//   raw = last_raw_;
//   return true;
// }

//mediana aritmética de n lecturas single-ended
bool ADS1115Manager::readSingleRaw(uint8_t channel, int16_t& raw) {
  if (!checkChannel_(channel)) return false;

  // ---- Dummy para estabilizar tras cambio de MUX/ganancia ----
  int16_t throwaway;
  if (!readOnceRawSingle_(channel, throwaway)) {
    setError_("Lectura dummy falló");
    return false;
  }

  // ---- Espera adicional tras el dummy (CLAVE) ----
  auto convDelayMs = [](uint16_t rate)->uint16_t {
    switch (rate) {
      case 8:   return 140;
      case 16:  return 75;
      case 32:  return 40;
      case 64:  return 20;
      case 128: return 10;
      case 250: return 5;
      case 475: return 3;
      case 860: return 2;
      default:  return 10;
    }
  };
  const uint16_t waitMs = convDelayMs(rate_);
  delay(waitMs);  // <<--- evita que el primer sample real salga 0/160/16383

  // ---- Tamaño de ventana ----
  static constexpr uint8_t MAX_SAMPLES = 32;
  uint8_t N = avg_;
  if (N < 5) N = 5;
  if (N > MAX_SAMPLES) N = MAX_SAMPLES;

  int16_t buf[MAX_SAMPLES];
  for (uint8_t i = 0; i < N; ++i) {
    int16_t r;
    if (!readOnceRawSingle_(channel, r)) {
      setError_("Lectura raw falló");
      return false;
    }
    buf[i] = r;
    if (i + 1 < N) delay(waitMs);
  }

  // ---- Ordena (insertion sort) ----
  for (uint8_t i = 1; i < N; ++i) {
    int16_t x = buf[i];
    int8_t j = i - 1;
    while (j >= 0 && buf[j] > x) { buf[j + 1] = buf[j]; --j; }
    buf[j + 1] = x;
  }

  // ---- Mediana base ----
  int16_t med;
  if (N & 1) med = buf[N / 2];
  else       med = (int16_t)((int32_t)buf[N/2 - 1] + (int32_t)buf[N/2]) / 2;

  // ---- Recorte anclado a la mediana (banda de aceptación) ----
  // Banda en cuentas ADC: ~0.1 V a PGA±4.096 V => 0.1 / 0.000125 ≈ 800 cuentas
  // Ajusta si usas otro PGA.
  const int16_t GATE = 800; // ≈ 0.1 V @ 4.096VFS. Sube/baja según tu ruido real.

  int32_t acc = 0; uint16_t cnt = 0;
  for (uint8_t i = 0; i < N; ++i) {
    int32_t d = (int32_t)buf[i] - (int32_t)med;
    if (d < 0) d = -d;
    if (d <= GATE) { acc += buf[i]; ++cnt; }
  }

  // Si la banda dejó muy pocos, cae a recortada 25% clásica
  if (cnt < (N / 2)) {
    uint8_t k = N / 4;                // 25%
    uint8_t start = k;
    uint8_t end = N - k;              // exclusivo
    if (end <= start) { start = 0; end = N; }

    acc = 0; cnt = 0;
    for (uint8_t i = start; i < end; ++i) { acc += buf[i]; ++cnt; }
    if (cnt == 0) {
      setError_("Trim vacio");
      return false;
    }
  }

  last_raw_ = (int16_t)(acc / (int32_t)cnt);
  last_error_[0] = '\0';
  raw = last_raw_;
  return true;
}




bool ADS1115Manager::readSingle(uint8_t channel, float& volts) {
  int16_t raw;
  if (!readSingleRaw(channel, raw)) return false;

  float v = ads_.computeVolts(raw);
  last_volts_ = applyCal_(v);
  volts = last_volts_;
  return true;
}

bool ADS1115Manager::readDifferentialRaw(uint8_t pair, int16_t& raw) {
  if (!connected_) {
    setError_("ADS1115 no inicializado");
    return false;
  }
  if (pair > 1) {
    setError_("Par inválido (0->01, 1->23)");
    return false;
  }

  int32_t acc = 0;
  for (uint8_t i = 0; i < avg_; ++i) {
    int16_t r;
    if (!readOnceRawDiff_(pair, r)) {
      setError_("Lectura diff raw falló");
      return false;
    }
    acc += r;
  }
  last_raw_ = static_cast<int16_t>(acc / (int32_t)avg_);
  last_error_[0] = '\0';
  raw = last_raw_;
  return true;
}

bool ADS1115Manager::readDifferential01(float& volts) {
  int16_t raw;
  if (!readDifferentialRaw(0, raw)) return false;
  float v = ads_.computeVolts(raw);
  last_volts_ = applyCal_(v);
  volts = last_volts_;
  return true;
}

bool ADS1115Manager::readDifferential23(float& volts) {
  int16_t raw;
  if (!readDifferentialRaw(1, raw)) return false;
  float v = ads_.computeVolts(raw);
  last_volts_ = applyCal_(v);
  volts = last_volts_;
  return true;
}
