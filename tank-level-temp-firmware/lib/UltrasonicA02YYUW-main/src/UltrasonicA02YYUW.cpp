#include "UltrasonicA02YYUW.h"
#include "Globals.h"
UltrasonicA02YYUW::UltrasonicA02YYUW(
    HardwareSerial &serialPort,
    int rxPin,
    int txPin,
    float mea_e,
    float est_e,
    float q
)
  : _serial(serialPort),
    _rx(rxPin),
    _tx(txPin),
    _kalman(mea_e, est_e, q)
{}

void UltrasonicA02YYUW::begin(unsigned long baudrate) {
  pinMode(_rx, INPUT_PULLDOWN);   // evita ruido cuando el sensor está desconectado
  _serial.begin(baudrate, SERIAL_8N1, _rx, _tx);
}

void UltrasonicA02YYUW::update() {

  // Si no llegan datos por más de 150 ms → error NO_DATA
  if (millis() - _lastPacketTime > 150) {
    _errorFlags |= 0x04;  // bit2 = NO_DATA
    Globals::setSensorStatus(_errorFlags);
  }

  while (_serial.available()) {
    uint8_t byteIn = _serial.read();

    switch (_index) {
      case 0:
        if (byteIn == 0xFF) {
          _buffer[0] = byteIn;
          _index = 1;
        } else {
          _errorFlags |= 0x10; // PACKET_FORMAT_ERROR
        }
        break;

      case 1:
      case 2:
      case 3:
        _buffer[_index++] = byteIn;
        if (_index > 3) {
          processPacket();
          _index = 0;
          _lastPacketTime = millis();
        }
        break;

      default:
        _index = 0;
    }
  }
}


void UltrasonicA02YYUW::processPacket() {
  uint8_t h   = _buffer[1];
  uint8_t l   = _buffer[2];
  uint8_t sum = _buffer[3];

  _errorFlags = 0;  // limpiamos, se recalcula con cada paquete

  uint8_t expected = (uint8_t)((0xFF + h + l) & 0xFF);
  if (expected != sum) {
    _errorFlags |= 0x01;  // CHECKSUM_ERROR
    Globals::setSensorStatus(_errorFlags);
    return;
  }

  int mm = ((uint16_t)h << 8) | l;
  float cm = mm / 10.0f;

  if (cm < 3.0f || cm > 750.0f) {
    _errorFlags |= 0x02; // OUT_OF_RANGE
    Globals::setSensorStatus(_errorFlags);
    return;
  }

  // OK: lectura válida
  _distanceCmRaw = cm;
  _hasValid = true;

  // Inicialización del filtro
  if (!_kalmanInitialized) {
    _distanceCmFiltered = cm;
    _kalman.updateEstimate(cm);
    _kalmanInitialized = true;

    Globals::setSensorStatus(_errorFlags);
    return;
  }

  // Filtro Kalman
  _distanceCmFiltered = _kalman.updateEstimate(cm);

  if (isnan(_distanceCmFiltered)) {
    _errorFlags |= 0x08; // FILTER_NAN
  }

  Globals::setSensorStatus(_errorFlags);
}


float UltrasonicA02YYUW::getDistanceRaw() const {
  return _distanceCmRaw;
}

float UltrasonicA02YYUW::getDistance() const {
  return _distanceCmFiltered;
}

bool UltrasonicA02YYUW::isValid() const {
  return _hasValid;
}

void UltrasonicA02YYUW::setMeasurementError(float mea_e) {
  _kalman.setMeasurementError(mea_e);
}

void UltrasonicA02YYUW::setEstimateError(float est_e) {
  _kalman.setEstimateError(est_e);
}

void UltrasonicA02YYUW::setProcessNoise(float q) {
  _kalman.setProcessNoise(q);
}
