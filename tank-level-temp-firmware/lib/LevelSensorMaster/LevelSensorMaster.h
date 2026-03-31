#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace LevelSensorMaster {

  // Dirección por defecto (puedes cambiarla luego con setSlaveAddress)
  constexpr uint8_t DEFAULT_SLAVE_ADDR = 0x30;

  // Registros del protocolo (deben coincidir con el slave)
  constexpr uint8_t REG_DIST_FILTERED = 0x00;
  constexpr uint8_t REG_DIST_RAW      = 0x04;
  constexpr uint8_t REG_MIN_LEVEL     = 0x08;
  constexpr uint8_t REG_MAX_LEVEL     = 0x0C;
  constexpr uint8_t REG_TEMP          = 0x10;  // <<< NUEVO: temperatura (float, °C)
  constexpr uint8_t REG_STATUS        = 0x20;


  // Flags de error (mismos bits que en el slave)
  constexpr uint8_t ERR_CHECKSUM      = 0x01;  // bit0
  constexpr uint8_t ERR_OUT_OF_RANGE  = 0x02;  // bit1
  constexpr uint8_t ERR_NO_DATA       = 0x04;  // bit2
  constexpr uint8_t ERR_FILTER_NAN    = 0x08;  // bit3
  constexpr uint8_t ERR_PKT_FORMAT    = 0x10;  // bit4

  // ----------------------------------------------------
  // Inicialización
  //  - llama a Wire.begin() por fuera
  //  - configura la dirección inicial del esclavo
  // ----------------------------------------------------
  void begin(TwoWire &wire = Wire, uint8_t slaveAddr = DEFAULT_SLAVE_ADDR);
  void setSlaveAddress(uint8_t addr);
  uint8_t getSlaveAddress();

  // ----------------------------------------------------
  // Primitivas de bajo nivel
  // ----------------------------------------------------
  float   readFloatReg(uint8_t reg);              // devuelve NAN en error
  bool    writeFloatReg(uint8_t reg, float val);  // true si OK
  uint8_t readByteReg(uint8_t reg, uint8_t def = 0xFF);

  // ----------------------------------------------------
  // Helpers de alto nivel
  // ----------------------------------------------------
  float readFilteredDistance();   // REG_DIST_FILTERED
  float readRawDistance();        // REG_DIST_RAW
  float readMinLevel();
  float readMaxLevel();
  float readTemperature();        // <<< NUEVO: REG_TEMP
  uint8_t readStatus();


  bool writeMinLevel(float cm);
  bool writeMaxLevel(float cm);

  // Lee todo en una sola llamada
  bool readAll(float &distFilt,
               float &distRaw,
               float &minLevel,
               float &maxLevel,
               float &tempC,
               uint8_t &status);

  // Devuelve una descripción legible del status
  String statusToString(uint8_t status);

  // Imprime status decodificado en el Stream indicado (Serial por defecto)
  void printStatus(uint8_t status, Stream &out = Serial);

}  // namespace LevelSensorMaster
