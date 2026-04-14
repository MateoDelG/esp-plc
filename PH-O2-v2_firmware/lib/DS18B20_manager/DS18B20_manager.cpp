#include "DS18B20_manager.h"

DS18B20Manager::DS18B20Manager(uint8_t dataPin, uint8_t resolutionBits)
: _pin(dataPin),
  _resolution(constrain(resolutionBits, 9, 12)),
  _ow(dataPin),
  _dt(&_ow) {
}

uint8_t DS18B20Manager::begin() {
  _dt.begin();
  _dt.setWaitForConversion(true);      // bloqueante sencillo
  _count = _dt.getDS18Count();
  if (_count > MAX_SENSORS) _count = MAX_SENSORS;

  // Guardar direcciones
  for (uint8_t i = 0; i < _count; ++i) {
    if (!_dt.getAddress(_addr[i], i)) {
      _count = i; // hasta donde logró
      break;
    }
  }

  if (_count > 0) {
    _dt.setResolution(_resolution);    // aplicar a todos
  }
  return _count;
}

float DS18B20Manager::_norm(float c) const {
  if (c <= DEVICE_DISCONNECTED_C + 0.001f) return NAN;
  return c;
}

float DS18B20Manager::readC(uint8_t index) {
  if (!_valid(index)) return NAN;

  // Solicitar conversión y leer específicamente por dirección (bloqueante)
  _dt.requestTemperaturesByAddress(_addr[index]);
  float c = _dt.getTempC(_addr[index]);
  return _norm(c);
}

float DS18B20Manager::readF(uint8_t index) {
  float c = readC(index);
  if (isnan(c)) return NAN;
  return DallasTemperature::toFahrenheit(c);
}

bool DS18B20Manager::getAddress(uint8_t index, DevAddr out) const {
  if (!_valid(index)) return false;
  for (uint8_t i = 0; i < 8; ++i) out[i] = _addr[index][i];
  return true;
}

void DS18B20Manager::addrToString(const DevAddr a, char *out, size_t n) {
  if (n < 24) { if (n) out[0] = '\0'; return; }
  snprintf(out, n, "%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
           a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}