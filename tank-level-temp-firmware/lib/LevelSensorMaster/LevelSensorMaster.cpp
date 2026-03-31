#include "LevelSensorMaster.h"

namespace LevelSensorMaster {

  static TwoWire *s_wire = &Wire;
  static uint8_t  s_addr = DEFAULT_SLAVE_ADDR;

  // ----------------------------------------------------
  // Inicialización
  // ----------------------------------------------------
  void begin(TwoWire &wire, uint8_t slaveAddr) {
    s_wire = &wire;
    setSlaveAddress(slaveAddr);
  }

  void setSlaveAddress(uint8_t addr) {
    if (addr < 1)   addr = 1;
    if (addr > 127) addr = 127;
    s_addr = addr;
  }

  uint8_t getSlaveAddress() {
    return s_addr;
  }

  // ----------------------------------------------------
  // Primitivas de bajo nivel
  // ----------------------------------------------------
  float readFloatReg(uint8_t reg) {
    union { float f; uint8_t b[4]; } u;

    // 1) Seleccionar registro
    s_wire->beginTransmission(s_addr);
    s_wire->write(reg);
    if (s_wire->endTransmission() != 0) {
      Serial.printf("[LevelSensorMaster] falló TX reg 0x%02X\n", reg);
      return NAN;
    }

    // 2) Solicitar 4 bytes
    int n = s_wire->requestFrom((int)s_addr, 4);
    if (n != 4) {
      Serial.printf("[LevelSensorMaster] esperaba 4 bytes en reg 0x%02X, recibí %d\n", reg, n);
      return NAN;
    }

    for (int i = 0; i < 4; ++i) {
      if (!s_wire->available()) return NAN;
      u.b[i] = s_wire->read();
    }
    return u.f;
  }

  bool writeFloatReg(uint8_t reg, float val) {
    union { float f; uint8_t b[4]; } u;
    u.f = val;

    s_wire->beginTransmission(s_addr);
    s_wire->write(reg);
    s_wire->write(u.b, 4);
    uint8_t err = s_wire->endTransmission();

    if (err != 0) {
      Serial.printf("[LevelSensorMaster] Error I2C al escribir reg 0x%02X, code=%u\n", reg, err);
      return false;
    }
    return true;
  }

  uint8_t readByteReg(uint8_t reg, uint8_t def) {
    s_wire->beginTransmission(s_addr);
    s_wire->write(reg);
    if (s_wire->endTransmission() != 0) {
      Serial.printf("[LevelSensorMaster] falló TX byte reg 0x%02X\n", reg);
      return def;
    }

    int n = s_wire->requestFrom((int)s_addr, 1);
    if (n != 1 || !s_wire->available()) {
      Serial.printf("[LevelSensorMaster] esperaba 1 byte en reg 0x%02X, recibí %d\n", reg, n);
      return def;
    }
    return s_wire->read();
  }

  // ----------------------------------------------------
  // Helpers de alto nivel
  // ----------------------------------------------------
  float readFilteredDistance() { return readFloatReg(REG_DIST_FILTERED); }
  float readRawDistance()      { return readFloatReg(REG_DIST_RAW);      }
  float readMinLevel()         { return readFloatReg(REG_MIN_LEVEL);     }
  float readMaxLevel()         { return readFloatReg(REG_MAX_LEVEL);     }
  uint8_t readStatus()         { return readByteReg(REG_STATUS, 0xFF);   }

  bool writeMinLevel(float cm) { return writeFloatReg(REG_MIN_LEVEL, cm); }
  bool writeMaxLevel(float cm) { return writeFloatReg(REG_MAX_LEVEL, cm); }
  float readTemperature() { return readFloatReg(REG_TEMP); }


  bool readAll(float &distFilt,
               float &distRaw,
               float &minLevel,
               float &maxLevel,
               float &tempC,
               uint8_t &status) {
    distFilt = readFilteredDistance();
    distRaw  = readRawDistance();
    minLevel = readMinLevel();
    maxLevel = readMaxLevel();
    tempC    = readTemperature();
    status   = readStatus();

    if (isnan(distFilt) || isnan(distRaw) || isnan(minLevel) || isnan(maxLevel) ||
        isnan(tempC)   || status == 0xFF)
      return false;
    return true;
  }


  // ----------------------------------------------------
  // Status helpers
  // ----------------------------------------------------
  String statusToString(uint8_t st) {
    if (st == 0) return "OK";

    String s;
    bool first = true;
    auto add = [&](const char *txt) {
      if (!first) s += " | ";
      s += txt;
      first = false;
    };

    if (st & ERR_CHECKSUM)     add("CHECKSUM_ERROR");
    if (st & ERR_OUT_OF_RANGE) add("OUT_OF_RANGE");
    if (st & ERR_NO_DATA)      add("NO_DATA");
    if (st & ERR_FILTER_NAN)   add("FILTER_NAN");
    if (st & ERR_PKT_FORMAT)   add("PACKET_FORMAT_ERROR");

    if (first) s = "UNKNOWN";  // por si ningún bit coincidió
    return s;
  }

  void printStatus(uint8_t st, Stream &out) {
    out.printf("Status   : 0x%02X (%u)\n", st, st);
    out.print("Estado   : ");
    out.println(statusToString(st));
  }

} // namespace LevelSensorMaster
