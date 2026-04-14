#ifndef DS18B20_MANAGER_H
#define DS18B20_MANAGER_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/**
 * - Lecturas bloqueantes y directas (°C y °F)
 * - Detecta y guarda hasta MAX_SENSORS al iniciar
 * - Sin modos no-bloqueantes
 * - Pin, resolución
 */
class DS18B20Manager {
public:
  using DevAddr = DeviceAddress;
  static constexpr uint8_t MAX_SENSORS = 8;

  explicit DS18B20Manager(uint8_t dataPin, uint8_t resolutionBits = 12);

  /** Inicializa y descubre sensores. Retorna cantidad detectada (0 si no hay). */
  uint8_t begin();

  /** Cantidad de sensores detectados. */
  uint8_t sensorCount() const { return _count; }

  /** Lee en °C (bloqueante). Devuelve NAN si falla o índice inválido. */
  float readC(uint8_t index = 0);

  /** Lee en °F (bloqueante). Devuelve NAN si falla o índice inválido. */
  float readF(uint8_t index = 0);

  /** Obtiene la dirección del sensor (8 bytes). */
  bool getAddress(uint8_t index, DevAddr out) const;

  /** Convierte dirección a texto tipo "28-FF-...". */
  static void addrToString(const DevAddr addr, char *out, size_t outLen);

private:
  uint8_t  _pin;
  uint8_t  _resolution;   // 9..12
  OneWire           _ow;
  DallasTemperature _dt;

  uint8_t _count = 0;
  DevAddr _addr[MAX_SENSORS];

  bool  _valid(uint8_t i) const { return i < _count; }
  float _norm(float c) const; // convierte DEVICE_DISCONNECTED_C a NAN
};


#endif // DS18B20_MANAGER_H