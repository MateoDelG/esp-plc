#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

namespace Globals {

  // ---- Lecturas del sensor ----
  void setDistanceRaw(float v);
  float getDistanceRaw();

  void setDistanceFiltered(float v);
  float getDistanceFiltered();
  
  void setTemperature(float v);
  float getTemperature();
  
// ---- Estado del sensor (bits) ----
  void setSensorStatus(uint8_t s);
  uint8_t getSensorStatus();

  // ---- Configuración del usuario ----
  void setMinLevel(float v);
  float getMinLevel();

  void setMaxLevel(float v);
  float getMaxLevel();

  void setSamplePeriod(uint32_t ms);
  uint32_t getSamplePeriod();

  uint8_t getI2CAddress();
  void    setI2CAddress(uint8_t addr);

  void setI2CEnabled(bool v);
  bool isI2CEnabled();

  void setZeroOffset(float v);
  float getZeroOffset();

  void setEspNowPeerMac(const uint8_t mac[6]);
  void getEspNowPeerMac(uint8_t mac[6]);

  void setEspNowEnabled(bool v);
  bool isEspNowEnabled();


  // Parámetros filtro Kalman
  void setKalMea(float v);
  float getKalMea();

  void setKalEst(float v);
  float getKalEst();

  void setKalQ(float v);
  float getKalQ();


}

#endif
