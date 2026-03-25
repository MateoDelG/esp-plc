#include "drivers/ads1115/ads1115_manager.h"

#include <cstring>

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
  if (connected_) {
    ads_.setGain(gain_);
  }
}

void ADS1115Manager::setDataRate(uint16_t r) {
  rate_ = r;
  if (connected_) {
    ads_.setDataRate(rate_);
  }
}

void ADS1115Manager::setAveraging(uint8_t n) {
  if (n == 0) {
    n = 1;
  }
  avg_ = n;
}

void ADS1115Manager::setCalibration(float scale, float offset) {
  cal_scale_ = scale;
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
    case 0:
      raw = ads_.readADC_SingleEnded(0);
      return true;
    case 1:
      raw = ads_.readADC_SingleEnded(1);
      return true;
    case 2:
      raw = ads_.readADC_SingleEnded(2);
      return true;
    case 3:
      raw = ads_.readADC_SingleEnded(3);
      return true;
    default:
      return false;
  }
}

bool ADS1115Manager::readSingleRaw(uint8_t channel, int16_t& raw) {
  if (!checkChannel_(channel)) {
    return false;
  }

  int16_t throwaway;
  if (!readOnceRawSingle_(channel, throwaway)) {
    setError_("Lectura dummy falló");
    return false;
  }

  auto convDelayMs = [](uint16_t rate) -> uint16_t {
    switch (rate) {
      case 8:
        return 140;
      case 16:
        return 75;
      case 32:
        return 40;
      case 64:
        return 20;
      case 128:
        return 10;
      case 250:
        return 5;
      case 475:
        return 3;
      case 860:
        return 2;
      default:
        return 10;
    }
  };

  const uint16_t waitMs = convDelayMs(rate_);
  delay(waitMs);

  static constexpr uint8_t kMaxSamples = 32;
  uint8_t n = avg_;
  if (n < 5) {
    n = 5;
  }
  if (n > kMaxSamples) {
    n = kMaxSamples;
  }

  int16_t buf[kMaxSamples];
  for (uint8_t i = 0; i < n; ++i) {
    int16_t r;
    if (!readOnceRawSingle_(channel, r)) {
      setError_("Lectura raw falló");
      return false;
    }
    buf[i] = r;
    if (i + 1 < n) {
      delay(waitMs);
    }
  }

  for (uint8_t i = 1; i < n; ++i) {
    int16_t x = buf[i];
    int8_t j = static_cast<int8_t>(i) - 1;
    while (j >= 0 && buf[j] > x) {
      buf[j + 1] = buf[j];
      --j;
    }
    buf[j + 1] = x;
  }

  int16_t med;
  if (n & 1) {
    med = buf[n / 2];
  } else {
    med = static_cast<int16_t>((static_cast<int32_t>(buf[n / 2 - 1]) +
                                static_cast<int32_t>(buf[n / 2])) / 2);
  }

  const int16_t gate = 800;
  int32_t acc = 0;
  uint16_t cnt = 0;
  for (uint8_t i = 0; i < n; ++i) {
    int32_t d = static_cast<int32_t>(buf[i]) - static_cast<int32_t>(med);
    if (d < 0) {
      d = -d;
    }
    if (d <= gate) {
      acc += buf[i];
      ++cnt;
    }
  }

  if (cnt < (n / 2)) {
    uint8_t k = n / 4;
    uint8_t start = k;
    uint8_t end = n - k;
    if (end <= start) {
      start = 0;
      end = n;
    }

    acc = 0;
    cnt = 0;
    for (uint8_t i = start; i < end; ++i) {
      acc += buf[i];
      ++cnt;
    }
    if (cnt == 0) {
      setError_("Trim vacio");
      return false;
    }
  }

  last_raw_ = static_cast<int16_t>(acc / static_cast<int32_t>(cnt));
  last_error_[0] = '\0';
  raw = last_raw_;
  return true;
}

bool ADS1115Manager::readSingle(uint8_t channel, float& volts) {
  int16_t raw;
  if (!readSingleRaw(channel, raw)) {
    return false;
  }

  float v = ads_.computeVolts(raw);
  last_volts_ = applyCal_(v);
  volts = last_volts_;
  return true;
}
