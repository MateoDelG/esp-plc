#include "Globals.h"
#include <string.h>

// Bits del estado del sensor A02
// Bit 0 = CHECKSUM_ERROR
// Bit 1 = OUT_OF_RANGE
// Bit 2 = NO_DATA (sin paquetes por más de X ms)
// Bit 3 = FILTER_NAN
// Bit 4 = PACKET_FORMAT_ERROR

namespace Globals {

  // ----------------------
  // Variables internas
  // ----------------------
    // Valor por defecto (ajústalo si quieres otro)
  static uint8_t g_i2cAddress = 0x30;   // 48 decimal

  static float distanceRaw = NAN;
  static float distanceFiltered = NAN;
  static float temperature = NAN;

  static uint8_t sensorStatus = 0;


  static float minLevel = 20.0f;
  static float maxLevel = 100.0f;

  static uint32_t samplePeriod = 1000;

  static float kalMea = 3.0f;
  static float kalEst = 10.0f;
  static float kalQ   = 0.05f;

  static float zeroOffset = 0.0f;

  static bool i2cEnabled = false;
  static uint8_t espNowPeerMac[6] = {0};
  static bool espNowEnabled = true;


  // ----------------------
  // Setters / Getters
  // ----------------------

  void setDistanceRaw(float v)      { distanceRaw = v; }
  float getDistanceRaw()            { return distanceRaw; }

  void setDistanceFiltered(float v) { distanceFiltered = v; }
  float getDistanceFiltered()       { return distanceFiltered; }

  void setTemperature(float v)        { temperature = v; }
  float getTemperature()              { return temperature; }

  void setSensorStatus(uint8_t s) { sensorStatus = s; }
  uint8_t getSensorStatus()       { return sensorStatus; }

  void setMinLevel(float v)         { minLevel = v; }
  float getMinLevel()               { return minLevel; }

  void setMaxLevel(float v)         { maxLevel = v; }
  float getMaxLevel()               { return maxLevel; }

  void setSamplePeriod(uint32_t v)  { samplePeriod = v; }
  uint32_t getSamplePeriod()        { return samplePeriod; }

  void setKalMea(float v)           { kalMea = v; }
  float getKalMea()                 { return kalMea; }

  void setKalEst(float v)           { kalEst = v; }
  float getKalEst()                 { return kalEst; }

  void setKalQ(float v)             { kalQ = v; }
  float getKalQ()                   { return kalQ; }

  uint8_t getI2CAddress() {return g_i2cAddress;}
  void setI2CAddress(uint8_t addr) {
    if (addr < 1)   addr = 1;
    if (addr > 127) addr = 127;
    g_i2cAddress = addr;
  }

  void setI2CEnabled(bool v) { i2cEnabled = v; }
  bool isI2CEnabled() { return i2cEnabled; }

  void setZeroOffset(float v) { zeroOffset = v; }
  float getZeroOffset() { return zeroOffset; }

  void setEspNowPeerMac(const uint8_t mac[6]) {
    memcpy(espNowPeerMac, mac, sizeof(espNowPeerMac));
  }

  void getEspNowPeerMac(uint8_t mac[6]) {
    memcpy(mac, espNowPeerMac, sizeof(espNowPeerMac));
  }

  void setEspNowEnabled(bool v) { espNowEnabled = v; }
  bool isEspNowEnabled() { return espNowEnabled; }


}
