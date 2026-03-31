#pragma once
#include <Arduino.h>
#include "SimpleKalmanFilter.h"

class UltrasonicA02YYUW {
public:
  // mea_e = measurement error
  // est_e = estimation error
  // q     = process noise
  UltrasonicA02YYUW(
      HardwareSerial &serialPort,
      int rxPin,
      int txPin,
      float mea_e = 3.0f,
      float est_e = 10.0f,
      float q     = 0.05f
  );

  void begin(unsigned long baudrate = 9600);
  void update();        // debe llamarse en loop()

  // Distancia en cm
  float getDistanceRaw() const;      // crudo
  float getDistance() const;         // filtrado
  bool  isValid() const;             // indica si tenemos al menos un paquete válido

  // Ajustes opcionales
  void setMeasurementError(float mea_e);   // ruido de medición (R)
  void setEstimateError(float est_e);      // error de estimación (P)
  void setProcessNoise(float q);           // ruido de proceso (Q)

private:
  HardwareSerial &_serial;
  int _rx, _tx;

  uint8_t _buffer[4] = {0};
  uint8_t _index     = 0;

  float _distanceCmRaw      = NAN;
  float _distanceCmFiltered = NAN;

  uint8_t _errorFlags = 0;
  unsigned long _lastPacketTime = 0;

  bool  _hasValid          = false;
  bool  _kalmanInitialized = false;

  SimpleKalmanFilter _kalman;

  void processPacket();
};
