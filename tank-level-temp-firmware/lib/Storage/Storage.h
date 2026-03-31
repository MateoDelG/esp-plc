#pragma once

#include <Arduino.h>

// Estructura con todos los parámetros que queremos persistir
struct ConfigData {
  float minLevel;       // cm
  float maxLevel;       // cm
  int   samplePeriod;   // ms

  float kalMea;         // error de medición
  float kalEst;         // error de estimación
  float kalQ;           // ruido de proceso

  uint8_t i2cAddress;   // dirección esclavo I2C
  uint8_t espNowPeerMac[6];
  bool espNowEnabled;
  float zeroOffsetCm;
};


namespace Storage {

  // Debe llamarse en setup() antes de usar load/save
  void begin();

  // Carga configuración desde NVS.
  //  - Devuelve true si encontró datos válidos
  //  - Devuelve false si no hay datos (o versión distinta)
  bool loadConfig(ConfigData &cfg);

  // Guarda configuración completa en NVS.
  void saveConfig(const ConfigData &cfg);

} // namespace Storage
