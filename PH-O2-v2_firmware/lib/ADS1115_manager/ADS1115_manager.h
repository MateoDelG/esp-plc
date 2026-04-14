#ifndef ADS1115_MANAGER_H
#define ADS1115_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

class ADS1115Manager {
public:
  explicit ADS1115Manager(uint8_t i2c_addr = 0x48, TwoWire* wire = &Wire);

  // Inicialización
  bool begin();
  bool isConnected() const { return connected_; }

  // Configuración
  void setGain(adsGain_t g);
  adsGain_t gain() const { return gain_; }

  // En tu versión de Adafruit_ADS1X15, el data rate es uint16_t
  void setDataRate(uint16_t r);
  uint16_t dataRate() const { return rate_; }

  void setAveraging(uint8_t n);                 // n>=1 lecturas promediadas
  void setCalibration(float scale, float offset); // V_out = scale * V_meas + offset

  // Lecturas single-ended (A0..A3)
  bool readSingle(uint8_t channel, float& volts);
  bool readSingleRaw(uint8_t channel, int16_t& raw);

  // Lecturas diferenciales
  bool readDifferential01(float& volts);
  bool readDifferential23(float& volts);
  bool readDifferentialRaw(uint8_t pair, int16_t& raw); // pair: 0->01, 1->23

  // Utilidades
  float lastVoltage() const { return last_volts_; }
  int16_t lastRaw() const { return last_raw_; }
  const char* lastError() const { return last_error_; }

private:
  Adafruit_ADS1115 ads_;
  uint8_t addr_;
  TwoWire* wire_;
  bool connected_ = false;

  uint8_t avg_ = 1;
  float cal_scale_ = 1.0f;
  float cal_offset_ = 0.0f;

  adsGain_t gain_ = GAIN_TWOTHIRDS;
  uint16_t  rate_ = RATE_ADS1115_128SPS;

  float   last_volts_ = NAN;
  int16_t last_raw_ = 0;
  char    last_error_[64] = {0};

  bool  checkChannel_(uint8_t ch);
  float applyCal_(float v) const { return cal_scale_ * v + cal_offset_; }
  void  setError_(const char* msg);

  bool readOnceRawSingle_(uint8_t ch, int16_t& raw);
  bool readOnceRawDiff_(uint8_t pair, int16_t& raw);
};

#endif // ADS1115_MANAGER_H
