#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <globals.h>

namespace UartProto {

class UARTManager {
public:
  explicit UARTManager(Stream& inOut);

  void begin(unsigned long baud = 115200);
  void loop();

  // ====== Setters de estado general ======
  void setLevelH2O(bool v);
  void setLevelKCL(bool v);
  void setAutoRunning(bool v);

  void setLastPh(float v);
  void setLastO2(float v);
  void setLastTempC(float v);
  void setLastResult(const String& r);
  void setLastHasData(bool v);

  // Envío directo del último snapshot (mismo formato que get_last)
  void sendLastSnapshot();

  // Solicitud de medición automática
  void setAutoMeasureRequested(bool v);
  void setBusy(bool v);

  // ====== Getters de estado general ======
  bool   getLevelH2O() const;
  bool   getLevelKCL() const;
  bool   getAutoRunning() const;

  float  getLastPh() const;
  float  getLastO2() const;
  float  getLastTempC() const;
  String getLastResult() const;
  bool   getLastHasData() const;

  bool   getAutoMeasureRequested() const;
  bool   getBusy() const;

  // ====== Set/Get por SAMPLE (id 1..4) ======
  // Guarda/lee valores NUMÉRICOS (no bool) de pH y O2 por muestra.
  void  setSamplePhValueById(uint8_t id /*1..4*/, float v);
  void  setSampleO2ValueById(uint8_t id /*1..4*/, float v);
  void  setSampleTempCById(uint8_t id /*1..4*/, float v);
  float getSamplePhValueById(uint8_t id /*1..4*/) const;
  float getSampleO2ValueById(uint8_t id /*1..4*/) const;
  float getSampleTempCById(uint8_t id /*1..4*/) const;

private:
  Stream& io_;
  String lineBuf_;

  // Estado global
  volatile bool   levelH2O_ok_ = false;
  volatile bool   levelKCL_ok_ = false;
  volatile bool   auto_running_ = false;
  volatile bool   autoMeasureRequested_ = false;
  volatile bool   busy_ = false;

  volatile float  last_ph_ = 7.0f;
  volatile float  last_o2_ = NAN;
  volatile float  last_tempC_ = 25.0f;
  String          last_result_ = "OK";
  volatile bool   last_has_data_ = false;

  // Estado por SAMPLE (S1..S4) — valores numéricos
  volatile float  sample_ph_val_[4] = {NAN, NAN, NAN, NAN};
  volatile float  sample_o2_val_[4] = {NAN, NAN, NAN, NAN};
  volatile float  sample_tempC_[4] = {NAN, NAN, NAN, NAN};

#if defined(ESP32)
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  void lock()   { taskENTER_CRITICAL(&mux_); }
  void unlock() { taskEXIT_CRITICAL(&mux_); }
#else
  void lock()   {}
  void unlock() {}
#endif

  // Procesamiento de comandos NDJSON
  void processLine_(const String& line);
  void handle_get_status_();
  void handle_get_last_();
  void handle_auto_measure_(JsonObject dataIn);

  // Helpers de salida
  void sendOk_();
  void sendError_(const char* err);
  void sendJson_(const JsonDocument& doc);

  // Helper JSON: inyectar arreglo "samples" [{id, ph_val, o2_val} x4]
  void addSamplesArray_(JsonObject parent);

  // Helper índice interno (id 1..4 -> idx 0..3), devuelve 255 si inválido
  static uint8_t idToIndex_(uint8_t id) {
    if (id < 1 || id > 4) return 255;
    return (uint8_t)(id - 1);
  }
};

} // namespace UartProto
