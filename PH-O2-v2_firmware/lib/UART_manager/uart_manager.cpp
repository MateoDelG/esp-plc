#include "uart_manager.h"
#include "globals.h"   // remoteManager

namespace UartProto {

static void logPrettyJson_(const JsonDocument& doc) {
  String pretty;
  serializeJsonPretty(doc, pretty);

  if (pretty.length() == 0) {
    remoteManager.log("[UART] TX pretty: ");
    return;
  }

  bool first = true;
  int start = 0;
  while (start < (int)pretty.length()) {
    int end = pretty.indexOf('\n', start);
    if (end < 0) end = pretty.length();
    String line = pretty.substring(start, end);
    if (first) {
      remoteManager.log(String("[UART] TX pretty: ") + line);
      first = false;
    } else {
      remoteManager.log(line);
    }
    start = end + 1;
  }
}

UARTManager::UARTManager(Stream& inOut) : io_(inOut) {
  remoteManager.log("[UART] Manager creado");
}

void UARTManager::begin(unsigned long baud) {
  (void)baud; // El SerialX.begin(...) lo hace el usuario
  remoteManager.log("[UART] begin() llamado (recuerda inicializar SerialX)");
}

void UARTManager::loop() {
  while (io_.available()) {
    char c = (char)io_.read();
    if (c == '\n') {
      String line = lineBuf_;
      lineBuf_ = "";
      if (line.length() > 0) {
        remoteManager.log(String("[UART] RX: ") + line);
        processLine_(line);
      }
    } else if (c != '\r') {
      lineBuf_ += c;
      if (lineBuf_.length() > 1024) {
        lineBuf_.clear();
        remoteManager.log("[UART] ERROR: BAD_ARGS (línea demasiado larga)");
        sendError_("BAD_ARGS");
      }
    }
  }
}

// ====== Setters ======
void UARTManager::setLevelH2O(bool v)              { lock(); levelH2O_ok_ = v;            unlock(); }
void UARTManager::setLevelKCL(bool v)              { lock(); levelKCL_ok_ = v;            unlock(); }
void UARTManager::setAutoRunning(bool v)           { lock(); auto_running_ = v;           unlock(); }
void UARTManager::setAutoMeasureRequested(bool v)  { lock(); autoMeasureRequested_ = v;   unlock(); }
void UARTManager::setBusy(bool v)                  { lock(); busy_ = v;                   unlock(); }

void UARTManager::setLastPh(float v)               { lock(); last_ph_ = v;  last_has_data_ = true; unlock(); }
void UARTManager::setLastO2(float v)               { lock(); last_o2_ = v;  last_has_data_ = true; unlock(); }
void UARTManager::setLastTempC(float v)            { lock(); last_tempC_ = v; last_has_data_ = true; unlock(); }
void UARTManager::setLastResult(const String& r)   { lock(); last_result_ = r; last_has_data_ = true; unlock(); }
void UARTManager::setLastHasData(bool v)           { lock(); last_has_data_ = v; unlock(); }

// Setters por SAMPLE (por id 1..4)
void UARTManager::setSamplePhValueById(uint8_t id, float v) {
  uint8_t idx = idToIndex_(id);
  if (idx == 255) return;
  lock(); sample_ph_val_[idx] = v; unlock();
}
void UARTManager::setSampleO2ValueById(uint8_t id, float v) {
  uint8_t idx = idToIndex_(id);
  if (idx == 255) return;
  lock(); sample_o2_val_[idx] = v; unlock();
}
void UARTManager::setSampleTempCById(uint8_t id, float v) {
  uint8_t idx = idToIndex_(id);
  if (idx == 255) return;
  lock(); sample_tempC_[idx] = v; unlock();
}

// ====== Getters ======
bool   UARTManager::getLevelH2O() const            { return levelH2O_ok_; }
bool   UARTManager::getLevelKCL() const            { return levelKCL_ok_; }
bool   UARTManager::getAutoRunning() const         { return auto_running_; }
bool   UARTManager::getAutoMeasureRequested() const{ return autoMeasureRequested_; }
bool   UARTManager::getBusy() const                { return busy_; }

float  UARTManager::getLastPh() const              { return last_ph_; }
float  UARTManager::getLastO2() const              { return last_o2_; }
float  UARTManager::getLastTempC() const           { return last_tempC_; }
String UARTManager::getLastResult() const          { return last_result_; }
bool   UARTManager::getLastHasData() const         { return last_has_data_; }

// Getters por SAMPLE (por id 1..4)
float UARTManager::getSamplePhValueById(uint8_t id) const {
  uint8_t idx = idToIndex_(id);
  if (idx == 255) return NAN;
  return sample_ph_val_[idx];
}
float UARTManager::getSampleO2ValueById(uint8_t id) const {
  uint8_t idx = idToIndex_(id);
  if (idx == 255) return NAN;
  return sample_o2_val_[idx];
}
float UARTManager::getSampleTempCById(uint8_t id) const {
  uint8_t idx = idToIndex_(id);
  if (idx == 255) return NAN;
  return sample_tempC_[idx];
}

// ================== Procesamiento NDJSON ==================
void UARTManager::processLine_(const String& lineRaw) {
  String line = lineRaw;
  line.trim();
  int i = line.indexOf('{');
  if (i > 0) line.remove(0, i);
  if (line.isEmpty() || line[0] != '{') {
    remoteManager.log("[UART] DESCARTE: línea sin JSON válido");
    sendError_("BAD_JSON");
    return;
  }

  StaticJsonDocument<256> in;
  DeserializationError err = deserializeJson(in, line);
  if (err) {
    remoteManager.log(String("[UART] ERROR: BAD_JSON (") + err.c_str() + ")");
    sendError_("BAD_JSON");
    return;
  }

  const char* op = in["op"] | "";
  JsonObject dataIn = in["data"].isNull() ? JsonObject() : in["data"].as<JsonObject>();

  if      (!strcmp(op, "get_status"))   { remoteManager.log("[UART] OP: get_status");  handle_get_status_(); }
  else if (!strcmp(op, "get_last"))     { remoteManager.log("[UART] OP: get_last");    handle_get_last_(); }
  else if (!strcmp(op, "auto_measure")) { remoteManager.log("[UART] OP: auto_measure");handle_auto_measure_(dataIn); }
  else {
    remoteManager.log(String("[UART] ERROR: BAD_OP (") + op + ")");
    sendError_("BAD_OP");
  }
}

// Helper: inyecta arreglo "samples" con {id, ph_val, o2_val}
void UARTManager::addSamplesArray_(JsonObject parent) {
  JsonArray arr = parent.createNestedArray("samples");
  for (uint8_t i = 0; i < 4; ++i) {
    float phv, o2v, tc;
    lock(); 
      phv = sample_ph_val_[i]; 
      o2v = sample_o2_val_[i];
      tc  = sample_tempC_[i];
    unlock();

    JsonObject it = arr.createNestedObject();
    it["id"]      = (uint8_t)(i + 1);
    // Escribir null si no hay valor (NaN)
    if (isfinite(phv)) it["ph_val"] = phv; else it["ph_val"] = nullptr;
    if (isfinite(o2v)) it["o2_val"] = o2v; else it["o2_val"] = nullptr;
    if (isfinite(tc))  it["tempC"] = tc;  else it["tempC"] = nullptr;
  }
}

// --- get_status ---
void UARTManager::handle_get_status_() {
  bool h2o, run, areq;
  lock();
  h2o  = levelH2O_ok_;
  run  = auto_running_;
  areq = autoMeasureRequested_;
  unlock();

  StaticJsonDocument<512> out;
  out["ok"] = true;
  JsonObject data = out.createNestedObject("data");

  JsonObject js = data.createNestedObject("level_sensors");
  js["h2o"] = h2o;

  data["auto_running"] = run;
  data["auto_req"]     = areq;

  // [{id, ph_val, o2_val} x4]
  addSamplesArray_(data);

  sendJson_(out);

  logPrettyJson_(out);

  remoteManager.log(String("[UART] TX get_status -> h2o=") + (h2o?"1":"0") +
                    " auto_running=" + (run?"1":"0") +
                    " auto_req=" + (areq?"1":"0"));
}

// --- get_last ---
void UARTManager::handle_get_last_() {
  if (getAutoRunning()) {
    remoteManager.log("[UART] get_last -> BUSY (auto_running)");
    sendError_("BUSY");
    return;
  }

  bool has;
  float ph, o2, tc;
  String res;
  bool h2o;

  lock();
  has = last_has_data_;
  ph  = last_ph_;
  o2  = last_o2_;
  tc  = last_tempC_;
  res = last_result_;
  h2o = levelH2O_ok_;
  unlock();

  if (!has) {
    remoteManager.log("[UART] TX get_last -> NO_DATA");
    sendError_("NO_DATA");
    return;
  }

  StaticJsonDocument<576> out;
  out["ok"] = true;
  JsonObject data = out.createNestedObject("data");
  data["ph"]    = ph;
  if (isfinite(o2)) {
    data["o2"] = o2;
  } else {
    data["o2"] = nullptr;
  }
  data["tempC"] = tc;

  JsonObject js = data.createNestedObject("level_sensors");
  js["h2o"] = h2o;

  // [{id, ph_val, o2_val} x4]
  addSamplesArray_(data);

  data["result"] = res;
  sendJson_(out);

  logPrettyJson_(out);

  remoteManager.log(String("[UART] TX get_last -> ph=") + ph +
                    " tempC=" + tc +
                    " h2o=" + (h2o?"1":"0") +
                    " result=" + res);
}

// --- auto_measure ---
void UARTManager::handle_auto_measure_(JsonObject) {
  if (getAutoRunning() || getBusy()) {
    remoteManager.log("[UART] auto_measure rechazado -> BUSY");
    sendError_("BUSY");
    return;
  }

  setAutoMeasureRequested(true);
  sendOk_();
  remoteManager.log("[UART] auto_measure aceptado -> auto_req=true");
}

// ================== Helpers ==================
void UARTManager::sendOk_() {
  StaticJsonDocument<32> out;
  out["ok"] = true;
  sendJson_(out);
  remoteManager.log("[UART] TX ok=true");

  logPrettyJson_(out);
}

void UARTManager::sendError_(const char* err) {
  StaticJsonDocument<96> out;
  out["ok"] = false;
  out["error"] = err;
  sendJson_(out);
  remoteManager.log(String("[UART] TX ok=false error=") + err);

  logPrettyJson_(out);
}

void UARTManager::sendLastSnapshot() {
  bool has;
  float ph, o2, tc;
  String res;
  bool h2o;

  lock();
  has = last_has_data_;
  ph  = last_ph_;
  o2  = last_o2_;
  tc  = last_tempC_;
  res = last_result_;
  h2o = levelH2O_ok_;
  unlock();

  if (!has) {
    sendError_("NO_DATA");
    return;
  }

  StaticJsonDocument<576> out;
  out["ok"] = true;
  JsonObject data = out.createNestedObject("data");
  data["ph"] = ph;
  if (isfinite(o2)) {
    data["o2"] = o2;
  } else {
    data["o2"] = nullptr;
  }
  data["tempC"] = tc;

  JsonObject js = data.createNestedObject("level_sensors");
  js["h2o"] = h2o;

  addSamplesArray_(data);
  data["result"] = res;

  sendJson_(out);
  logPrettyJson_(out);
}

void UARTManager::sendJson_(const JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  io_.println(payload);
}

} // namespace UartProto
