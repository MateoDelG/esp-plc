#include "WiFiPortalManager.h"
#include "eeprom_manager.h"
#include "menu_manager.h"
#include "pH_manager.h"
#include "o2_manager.h"
#include "pumps_manager.h"
#include "level_sensors_manager.h"
#include "test_board.h"
#include "uart_manager.h"
#include "WebPortalManager.h"
#include "services/console/console_service.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>
#include <globals.h>


TaskHandle_t TaskCore0;
TaskHandle_t TaskCore1;

WiFiPortalManager wifiManager(TELNET_HOSTNAME, "12345678", SW_DOWN);
#define RX_PIN 16
#define TX_PIN 17
UartProto::UARTManager uart2(Serial2);

PumpsManager pumps;
PHManager ph(&ads, /*channel=*/0, /*avgSamples=*/6);
O2Manager o2(&ads, /*channel=*/1, /*avgSamples=*/6);
ConfigStore eeprom;
LevelSensorsManager levels; 
WebPortalManager webPortal(&pumps, &levels, &uart2, &eeprom);
ConsoleService consoleService;

volatile bool autoCancelRequest = false;

bool startProcess = false;

void initDualCore();
void initHardware();
void initWiFi();
void initThermo();
void initLCD();
void initADC();
void initPH();
void initO2();
void initEEPROM();
void initPumps();
float readThermo();
float readADC();
float readPH();
float readO2();
static bool handleWebAction(const String& action, const String& value, String& outMessage);

void APIUI();
static void MenuDemoTick();
static bool AutoModeTick();

void setup() {
  initPumps();
  Serial.begin(115200);
  Serial2.begin(115200);
  initHardware();
  initDualCore();
  initEEPROM();
  initWiFi();
  // delay(5000);
  initThermo();
  initLCD();
  initADC();
  initPH();
  initO2();
  startProcess = true;
}

void loop() { delay(100); }

void testpin(){

  pinMode(SENSOR_LEVEL_H2O, OUTPUT);
  digitalWrite(SENSOR_LEVEL_H2O, HIGH);
  delay(10000);
  remoteManager.log(String("Pin ") + String(SENSOR_LEVEL_H2O) + " set HIGH.");

  digitalWrite(SENSOR_LEVEL_H2O, LOW);
  delay(10000);
  remoteManager.log(String("Pin ") + String(SENSOR_LEVEL_H2O) + " set LOW.");

  pinMode(SENSOR_LEVEL_H2O, INPUT_PULLUP);
  remoteManager.log(String("Pin ") + String(SENSOR_LEVEL_H2O) + " set INPUT.");
  delay(1000);

  remoteManager.log(String("Pin ") + String(SENSOR_LEVEL_H2O) + " test done." + String("state=") + String(digitalRead(SENSOR_LEVEL_H2O)));
}

void taskCore0(void *pvParameters) {
  for (;;) {
    if (startProcess) {

      // TestBoard::testPumps(PUMP_1, PUMP_2, PUMP_3, PUMP_4, MIXER);
      // readThermo();
      // TestBoard::testButtons();
      // readADS();
      // readPH();
      MenuDemoTick();
      // AutoModeTick();
      // testpin();


    }

    vTaskDelay(100 / portTICK_PERIOD_MS); // Espera 100 ms
  }
}
// loop
void taskCore1(void *pvParameters) {
  for (;;) {
    wifiManager.loop();
    remoteManager.handle();
    webPortal.loop();
    consoleService.update();
    uart2.loop();
    vTaskDelay(200 / portTICK_PERIOD_MS); // Espera 100 ms
  }
}

void initDualCore() {
  // Crear tarea para Core 0
  xTaskCreatePinnedToCore(taskCore0,   // función
                          "TaskCore0", // nombre
                          4096,        // stack size en palabras
                          NULL,        // parámetros
                          1,           // prioridad
                          &TaskCore0,  // handle
                          0            // core (0 ó 1)
  );

  // Crear tarea para Core 1
  xTaskCreatePinnedToCore(taskCore1, "TaskCore1", 4096, NULL, 1, &TaskCore1,
                          1 // core 1
  );
}

void initHardware() {
  pinMode(SW_DOWN, INPUT);
  pinMode(SW_UP, INPUT);
  pinMode(SW_OK, INPUT);
  pinMode(SW_ESC, INPUT);

  pinMode(BUZZER, OUTPUT);

  digitalWrite(BUZZER, LOW);

  levels.begin(SENSOR_LEVEL_O2, SENSOR_LEVEL_PH, SENSOR_LEVEL_KCL,
               SENSOR_LEVEL_H2O);
}

void initWiFi() {
  char ssid[ConfigStore::kWifiSsidMax + 1] = {0};
  char pass[ConfigStore::kWifiPassMax + 1] = {0};
  eeprom.getWifiCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
  wifiManager.setStoredCredentials(ssid, pass);
  wifiManager.setAutoReconnect(eeprom.wifiAutoReconnect());
  wifiManager.begin();
  if (wifiManager.isConnected() || wifiManager.isAPActive()) {
    remoteManager.begin(); // Solo si hay WiFi
    consoleService.begin();
    remoteManager.setLogHook(ConsoleService::logSink);
    webPortal.setActionHandler(handleWebAction);
    webPortal.begin();
    if (wifiManager.isConnected()) {
      remoteManager.log(String("Web portal listo en http://") + wifiManager.getLocalIP().toString());
    } else {
      remoteManager.log(String("Web portal AP listo en http://") + wifiManager.getAPIP().toString());
    }
  }
}

void initThermo() {
  uint8_t n = thermo.begin();
  remoteManager.log("DS18B20 encontrados: " + String(n));
}

void initLCD() {
  lcd.begin();
  lcd.setBacklight(true);
  lcd.clear();

  lcd.centerPrint(0, "IUDigital");
  lcd.centerPrint(1, "de Antioquia");
  delay(2000);
  lcd.clear();
  lcd.centerPrint(0, "ph - O2 metter");
  lcd.centerPrint(1, "v1.0");
  delay(2000);
}

void initADC() {
  if (!ads.begin()) {
    remoteManager.log(String("ADS1115 no responde: ") + ads.lastError());
    return;
  }
  // Rango y tasa recomendados (tu versión usa uint16_t en set/getDataRate)
  ads.setGain(GAIN_ONE); // ±4.096V (1 LSB ≈ 0.125 mV)
  ads.setDataRate(
      RATE_ADS1115_250SPS); // RATE_ADS1115_250SPS 250 SPS (uint16_t)
  ads.setAveraging(4);

  float m, b; // suaviza ruido promediando 4 lecturas
  eeprom.getADC(m, b);
  ads.setCalibration(m, b);

  remoteManager.log("ADS1115 listo (GAIN_ONE, 250SPS)");
}

void initPH() {
  ph.begin();

  // Cargar calibración pH (V7, V4, tC) desde EEPROM
  float V7 = NAN, V4 = NAN, tCalC = NAN;
  eeprom.getPH2pt(V7, V4, tCalC);

  const bool v7ok = !isnan(V7);
  const bool v4ok = !isnan(V4);
  bool tcok = (!isnan(tCalC) && tCalC > -40.0f && tCalC < 125.0f);

  if (v7ok && v4ok) {
    if (!tcok)
      tCalC = 25.0f; // fallback razonable si no hay T guardada
    ph.setTwoPointCalibration(7.00f, V7, 4.00f, V4, tCalC);

    // Log de verificación
    remoteManager.log(String("pH cal cargada: V7=") + String(V7, 5) + " V4=" +
                      String(V4, 5) + " Tcal=" + String(tCalC, 1) + "C");
  } else {
    // No hay datos persistidos aún: dejar sin cal específica o valores por
    // defecto
    remoteManager.log(
        "pH cal: no hay V7/V4 en EEPROM. Usa defaults o ejecuta cal.");
    // Si tu clase 'ph' requiere un estado definido, puedes fijar un default
    // aquí:
    ph.setTwoPointCalibration(7.00f, 1.650f, 4.00f, 1.650f,
                              25.0f); // (ejemplo neutro, opcional)
  }
}

void initEEPROM() {
  eeprom.begin(256);
  if (!eeprom.load()) {
    eeprom.resetDefaults();
    eeprom.save();
  }

  // float m,b;
  // eeprom.getADC(m,b);
  // ads.setCalibration(m,b);
}

void initPumps() {
static const int I2C_SDA_PIN  = 21;
static const int  I2C_SCL_PIN = 22;
static const int  PCF_ADDR    = 0x20;
  if (!pumps.begin(PCF_ADDR, I2C_SDA_PIN, I2C_SCL_PIN, 100000, /*activeHigh=*/true)) {
    Serial.println(F("ERROR: PCF no responde"));
    while (1) delay(1000);
  }
  pumps.allOff();
}

float readADC() {
  // Lee A0..A3 single-ended
  // for (uint8_t ch = 0; ch < 4; ch++) {
  //   float v;
  //   if (ads.readSingle(ch, v)) {
  //     remoteManager.log("A" + String(ch) + ": " + String(v, 6) + " V");
  //   } else {
  //     remoteManager.log(String("ADS err A") + ch + ": " + ads.lastError());
  //   }
  // }
  float v;
  if (ads.readSingle(0, v)) {
    remoteManager.log("A0"
                      ": " +
                      String(v, 6) + " V");
    return v;
  } else {
    remoteManager.log(String("ADS err A0") + ": " + ads.lastError());
    return -1;
  }
}

float readThermo() {
  if (thermo.sensorCount() == 0) {
    // Serial.println("Sin sensores");
    remoteManager.log("Sin sensores");
    // delay(1000);
    return -1;
  }
  float c = thermo.readC(0);
  if (isnan(c)) {
    // Serial.println("Lectura inválida");
    remoteManager.log("Lectura inválida");
    return -1;
  } else {
    // Serial.printf("T0: %.2f °C\n", c);
    remoteManager.log("T: " + String(c) + " °C");
    uart2.setLastTempC(c);
    return c;
  }
  // delay(1000);
}

float readPH() {
  extern ConfigStore eeprom;
  extern UartProto::UARTManager uart2;
  extern float readThermo();

  // 1) Leer temperatura actual (para compensación)
  float tC = readThermo();
  bool tOk = isfinite(tC) && tC > -40.0f && tC < 125.0f;
  if (!tOk) tC = 25.0f; // fallback si el sensor falla
  uart2.setLastTempC(tC);

  // 2) Leer pH con compensación de temperatura
  float phValue = NAN, volts = NAN;
  if (!ph.readPH(tC, phValue, &volts)) {
    remoteManager.log(String("pH ERR: ") + ph.lastError());
    uart2.setLastPh(-99.0f);
    return -99.0f;
  }

  // 3) Determinar tipo de calibración vigente
  float V4 = NAN, V7 = NAN, V10 = NAN, Tcal = NAN;
  bool has3p = false;
  bool has2p = false;

  // Intentar leer 3 puntos (si el layout lo tiene)
  eeprom.getPH3pt(V4, V7, V10, Tcal);
  if (isfinite(V4) && isfinite(V7) && isfinite(V10)) has3p = true;

  // Si no hay 3 puntos válidos, intentar 2 puntos
  if (!has3p) {
    float V7b = NAN, V4b = NAN, Tc2 = NAN;
    eeprom.getPH2pt(V7b, V4b, Tc2);
    if (isfinite(V7b) && isfinite(V4b)) has2p = true;
  }

  // 4) Mostrar en log UART según tipo de calibración
  String calTag;
  if (has3p)      calTag = "[3p]";
  else if (has2p) calTag = "[2p]";
  else            calTag = "[NoCal]";

  // 5) Registro en log y UART
  remoteManager.log(
    "pH " + calTag + " = " + String(phValue, 3) +
    "  V=" + String(volts, 4) +
    "  T=" + String(tC, 1) + "°C"
  );

  uart2.setLastPh(phValue);
  return phValue;
}

float readO2() {
  extern float readThermo();

  float tC = readThermo();
  bool tOk = isfinite(tC) && tC > -40.0f && tC < 125.0f;
  if (!tOk) tC = 25.0f;

  float do_mgL = NAN, volts = NAN;
  if (!o2.readDO(tC, do_mgL, &volts)) {
    remoteManager.log(String("O2 ERR: ") + o2.lastError());
    return NAN;
  }

  uart2.setLastO2(do_mgL);

  remoteManager.log(
    "O2 = " + String(do_mgL, 3) +
    " mg/L  V=" + String(volts, 4) +
    "  T=" + String(tC, 1) + "°C"
  );

  return do_mgL;
}

void initO2() {
  o2.begin();

  if (o2.applyEEPROMCalibration(eeprom)) {
    float V1, T1, V2, T2;
    o2.getTwoPointCalibration(V1, T1, V2, T2);
    remoteManager.log(String("O2 cal cargada: V1=") + String(V1, 1) +
                      "mV T1=" + String(T1, 1) + "C V2=" +
                      String(V2, 1) + "mV T2=" + String(T2, 1) + "C");
  } else {
    remoteManager.log(String("O2 cal: ") + o2.lastError());
  }
}

static bool handleWebAction(const String& action, const String& value, String& outMessage) {
  (void)value;
  if (action == "auto_start") {
    uart2.setAutoMeasureRequested(true);
    outMessage = "auto requested";
    return true;
  }
  if (action == "auto_cancel") {
    autoCancelRequest = true;
    outMessage = "auto cancel requested";
    return true;
  }
  if (action == "read_ph") {
    float v = readPH();
    outMessage = String("ph=") + String(v, 2);
    return true;
  }
  if (action == "read_o2") {
    float v = readO2();
    outMessage = String("o2=") + String(v, 3);
    return true;
  }
  if (action == "read_temp") {
    float t = readThermo();
    outMessage = String("temp=") + String(t, 1);
    return true;
  }
  if (action == "toggle_pump") {
    PumpId id;
    bool ok = true;
    if      (value == "kcl")   id = PumpId::KCL;
    else if (value == "h2o")   id = PumpId::H2O;
    else if (value == "drain") id = PumpId::DRAIN;
    else if (value == "mixer") id = PumpId::MIXER;
    else if (value == "s1")    id = PumpId::SAMPLE1;
    else if (value == "s2")    id = PumpId::SAMPLE2;
    else if (value == "s3")    id = PumpId::SAMPLE3;
    else if (value == "s4")    id = PumpId::SAMPLE4;
    else ok = false;

    if (!ok) {
      outMessage = "invalid pump";
      return false;
    }

    bool nowOn = !pumps.isOn(id);
    pumps.set(id, nowOn);
    outMessage = String("pump ") + value + (nowOn ? " on" : " off");
    remoteManager.log(outMessage);
    return true;
  }
  if (action == "wifi_apply") {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) {
      outMessage = "invalid json";
      return false;
    }

    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    if (ssid[0] == '\0') {
      outMessage = "ssid empty";
      return false;
    }
    if (strlen(ssid) > ConfigStore::kWifiSsidMax ||
        strlen(pass) > ConfigStore::kWifiPassMax) {
      outMessage = "ssid/pass too long";
      return false;
    }

    eeprom.setWifiCredentials(ssid, pass);
    if (!eeprom.save()) {
      outMessage = "eeprom save fail";
      remoteManager.log(String("WiFi EEPROM save: ") + eeprom.lastError());
      return false;
    }

    bool ok = wifiManager.connectWithCredentials(ssid, pass);
    outMessage = ok ? "wifi reconnecting" : "wifi connect failed";
    remoteManager.log(String("WiFi apply: ") + ssid + (ok ? " ok" : " fail"));
    return ok;
  }
  if (action == "wifi_auto_reconnect") {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) {
      outMessage = "invalid json";
      return false;
    }
    const bool enabled = doc["enabled"] | false;
    eeprom.setWifiAutoReconnect(enabled);
    if (!eeprom.save()) {
      outMessage = "eeprom save fail";
      remoteManager.log(String("WiFi auto reconnect save: ") + eeprom.lastError());
      return false;
    }
    wifiManager.setAutoReconnect(enabled);
    outMessage = enabled ? "auto reconnect on" : "auto reconnect off";
    remoteManager.log(String("WiFi auto reconnect: ") + (enabled ? "on" : "off"));
    return true;
  }
  if (action == "set_times") {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) {
      outMessage = "invalid json";
      return false;
    }

    auto readU = [&](const char* key, uint32_t def) -> uint32_t {
      if (!doc[key].is<long>()) return def;
      long v = doc[key].as<long>();
      if (v < 0) v = 0;
      return (uint32_t)v;
    };
    auto readSampleCount = [&](const char* key, uint8_t def) -> uint8_t {
      if (!doc[key].is<long>()) return def;
      long v = doc[key].as<long>();
      if (v < 0) v = 0;
      if (v > 4) v = 4;
      return (uint8_t)v;
    };

    uint32_t kclFill = readU("kcl_fill_s", (uint32_t)(eeprom.kclFillMs() / 1000UL));
    uint32_t h2oFill = readU("h2o_fill_s", (uint32_t)(eeprom.h2oFillMs() / 1000UL));
    uint32_t sampleFill = readU("sample_fill_s", (uint32_t)(eeprom.sampleFillMs() / 1000UL));
    uint32_t drain = readU("drain_s", (uint32_t)(eeprom.drainMs() / 1000UL));
    uint32_t sampleTimeout = readU("sample_timeout_s", (uint32_t)(eeprom.sampleTimeoutMs() / 1000UL));
    uint32_t drainTimeout = readU("drain_timeout_s", (uint32_t)(eeprom.drainTimeoutMs() / 1000UL));
    static constexpr uint32_t kUnset = 0xFFFFFFFFUL;
    uint32_t o2Stab = readU("o2_stabilization_s", kUnset);
    if (o2Stab == kUnset) {
      o2Stab = readU("stabilization_s", (uint32_t)(eeprom.o2StabilizationMs() / 1000UL));
    }
    uint32_t phStab = readU("ph_stabilization_s", (uint32_t)(eeprom.phStabilizationMs() / 1000UL));
    uint8_t sampleCount = readSampleCount("sample_count", (uint8_t)eeprom.sampleCount());

    eeprom.setKclFillMs(kclFill * 1000UL);
    eeprom.setH2oFillMs(h2oFill * 1000UL);
    eeprom.setSampleFillMs(sampleFill * 1000UL);
    eeprom.setDrainMs(drain * 1000UL);
    eeprom.setSampleTimeoutMs(sampleTimeout * 1000UL);
    eeprom.setDrainTimeoutMs(drainTimeout * 1000UL);
    eeprom.setO2StabilizationMs(o2Stab * 1000UL);
    eeprom.setPhStabilizationMs(phStab * 1000UL);
    eeprom.setSampleCount(sampleCount);
    eeprom.save();

    outMessage = "times saved";
    remoteManager.log(outMessage);
    return true;
  }
  outMessage = "unknown action";
  return false;
}

static bool runADSCalibration_0V_3p31V(uint8_t channel = 0,
                                       uint8_t samples = 32) {
  enum class Step : uint8_t {
    START,
    WAIT_ZERO,
    CAPT_ZERO,
    WAIT_VREF,
    CAPT_VREF,
    APPLY,
    DONE,
    CANCEL
  };
  static Step step = Step::START;

  static float v_meas_0 = 0.0f;   // medición SIN cal a 0 V
  static float v_meas_ref = 0.0f; // medición SIN cal a 3.31 V

  static constexpr float V_ACT_0 = 0.00f;
  static constexpr float V_ACT_REF = 3.31f;
  static constexpr float kEps = 1e-6f;

  // --- UI helpers ---
  auto showStep0 = []() {
    lcd.printAt(0, 0, "Calibrar ADS");
    lcd.printAt(0, 1, "0V: GND y OK");
  };
  auto showStepRef = []() {
    lcd.printAt(0, 0, "Calibrar ADS");
    lcd.printAt(0, 1, "Vref=3.31V y OK");
  };
  auto showBusy = [](const char *msg) {
    lcd.printAt(0, 0, "Calibrar ADS");
    lcd.printAt(0, 1, msg);
  };
  auto showError = [](const char *m1, const char *m2 = "") {
    lcd.splash(m1, m2, 900);
  };

  // Botones (latcheados)
  Buttons::testButtons();

  switch (step) {
  case Step::START: {
    // Medir "en crudo": desactiva cal temporalmente
    ads.setCalibration(1.0f, 0.0f);
    ads.setAveraging(samples);

    lcd.clear();
    showStep0();
    remoteManager.log("ADS cal: START -> 0V");
    step = Step::WAIT_ZERO;
    return false;
  }

  case Step::WAIT_ZERO:
    if (Buttons::BTN_ESC.value) {
      Buttons::BTN_ESC.reset();
      step = Step::CANCEL;
    } else if (Buttons::BTN_OK.value) {
      Buttons::BTN_OK.reset();
      step = Step::CAPT_ZERO;
    }
    return false;

  case Step::CAPT_ZERO: {
    showBusy("Midiendo 0V...");
    float v;
    if (!ads.readSingle(channel, v)) {
      remoteManager.log("ADS cal: fallo lectura 0V");
      showError("Error lectura", "0V");
      step = Step::CANCEL;
      return false;
    }
    v_meas_0 = v; // sin cal (m=1, b=0)
    remoteManager.log(String("ADS cal: V_meas0=") + String(v_meas_0, 6));

    lcd.splash("0V capturado", "", 500);
    lcd.clear();
    showStepRef();
    step = Step::WAIT_VREF;
    return false;
  }

  case Step::WAIT_VREF:
    if (Buttons::BTN_ESC.value) {
      Buttons::BTN_ESC.reset();
      step = Step::CANCEL;
    } else if (Buttons::BTN_OK.value) {
      Buttons::BTN_OK.reset();
      step = Step::CAPT_VREF;
    }
    return false;

  case Step::CAPT_VREF: {
    showBusy("Mid. 3.31V...");
    float v;
    if (!ads.readSingle(channel, v)) {
      remoteManager.log("ADS cal: fallo lectura Vref");
      showError("Error lectura", "Vref");
      step = Step::CANCEL;
      return false;
    }
    v_meas_ref = v; // sin cal
    remoteManager.log(String("ADS cal: V_measRef=") + String(v_meas_ref, 6));
    step = Step::APPLY;
    return false;
  }

  case Step::APPLY: {
    // V_real = scale * V_meas + offset
    const float denom = (v_meas_ref - v_meas_0);
    if (fabsf(denom) < kEps) {
      showError("Error cal ADS", "Denominador ~0");
      step = Step::CANCEL;
      return false;
    }
    const float scale = (V_ACT_REF - V_ACT_0) / denom;
    const float offset = V_ACT_0 - scale * v_meas_0;

    // Aplica al ADS
    ads.setCalibration(scale, offset);

    // Persiste en EEPROM (usa tu instancia global 'eeprom')
    eeprom.setADC(scale, offset);
    if (!eeprom.save()) {
      remoteManager.log(String("EEPROM save fallo: ") + eeprom.lastError());
      showError("EEPROM", "Save fallo");
      step = Step::DONE; // igual finalizamos
      return false;
    }

    remoteManager.log(String("ADS cal aplicada: m=") + String(scale, 6) +
                      " b=" + String(offset, 6));
    char l2[17];
    snprintf(l2, sizeof(l2), "m=%.3f b=%.3f", scale, offset);
    lcd.splash("ADS calibrado", l2, 800);

    step = Step::DONE;
    return false;
  }

  case Step::CANCEL:
    remoteManager.log("ADS cal: CANCEL");
    showError("Calibracion", "Cancelada");
    step = Step::DONE;
    return false;

  case Step::DONE:
  default:
    step = Step::START; // listo para próxima vez
    return true;        // informa al caller que ya terminó
  }
}

static bool runPHCalibration_7_4(uint8_t samples = 64) {
  // Externs típicos del proyecto
  extern PHManager               ph;
  extern ADS1115Manager          ads;
  extern float                   readThermo();
  extern ConfigStore             eeprom;
  extern UartProto::UARTManager  uart2;
  // y, por supuesto, el lcd y Buttons::* ya existentes

  enum class Step : uint8_t {
    START,
    WAIT_7,
    CAPT_7,
    WAIT_4,
    CAPT_4,
    APPLY,
    DONE,
    CANCEL
  };
  static Step step = Step::START;

  static float V7 = NAN, V4 = NAN; // voltajes (mediana)
  static float T1 = NAN, T2 = NAN; // temperaturas (trazabilidad)

  // --- UI helpers ---
  auto title = []() { lcd.printAt(0, 0, "Calibrar pH"); };
  auto ask7 = [&]() { title(); lcd.printAt(0, 1, "Buffer 7.00  OK"); };
  auto ask4 = [&]() { title(); lcd.printAt(0, 1, "Buffer 4.00  OK"); };
  auto busy = [&](const char *m){ title(); lcd.printAt(0, 1, m); };
  auto showErr = [](const char *a, const char *b = "") { lcd.splash(a, b, 900); };

  // ---- Utilidad: ordenación por insertion sort (simple y estable) ----
  auto insertionSort = [](float* arr, uint16_t n){
    for (uint16_t i = 1; i < n; ++i) {
      float key = arr[i];
      int j = (int)i - 1;
      while (j >= 0 && arr[j] > key) {
        arr[j+1] = arr[j];
        --j;
      }
      arr[j+1] = key;
    }
  };

  // ---- Mediana del VOLTAJE usando ph.readPH() (con compensación T) ----
  auto medianVoltPH = [&](uint16_t N) -> float {
    // Bound razonable para stack en microcontrolador
    static constexpr uint16_t MAX_S = 128;
    if (N == 0) N = 1;
    if (N > MAX_S) N = MAX_S;

    float buf[MAX_S];
    uint16_t k = 0;

    for (uint16_t i = 0; i < N; ++i) {
      float tC = readThermo();
      bool tOk = isfinite(tC) && tC > -40.0f && tC < 125.0f;
      float tUsed = tOk ? tC : 25.0f;

      float phVal = NAN, v = NAN;
      if (!ph.readPH(tUsed, phVal, &v)) {
        // si una lectura falla, simplemente la omitimos
      } else if (isfinite(v)) {
        buf[k++] = v;
      }
      delay(4); // pequeño settling
    }

    if (k < 3) {
      // muy pocas válidas para una mediana robusta
      return NAN;
    }

    insertionSort(buf, k);
    if (k & 1) {
      // impar
      return buf[k/2];
    } else {
      // par
      uint16_t r = k/2;
      return 0.5f * (buf[r-1] + buf[r]);
    }
  };

  switch (step) {
    case Step::START: {
      ads.setAveraging(samples);
      lcd.clear();
      ask7();
      step = Step::WAIT_7;
      return false;
    }

    case Step::WAIT_7: {
      if (Buttons::BTN_ESC.value) { Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value)  { Buttons::BTN_OK.reset();  step = Step::CAPT_7; return false; }
      return false;
    }

    case Step::CAPT_7: {
      busy("Midiendo pH7...");
      V7 = medianVoltPH(samples);
      T1 = readThermo();
      if (!isfinite(V7)) {
        showErr("Error lectura", "pH 7.00");
        step = Step::CANCEL;
        return false;
      }
      if (&remoteManager) remoteManager.log(String("pH cal: V7=") + String(V7,5) + " T1=" + String(T1,1));
      lcd.splash("pH 7.00 OK", "", 600);
      lcd.clear();
      ask4();
      step = Step::WAIT_4;
      return false;
    }

    case Step::WAIT_4: {
      if (Buttons::BTN_ESC.value) { Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value)  { Buttons::BTN_OK.reset();  step = Step::CAPT_4; return false; }
      return false;
    }

    case Step::CAPT_4: {
      busy("Midiendo pH4...");
      V4 = medianVoltPH(samples);
      T2 = readThermo();
      if (!isfinite(V4)) {
        showErr("Error lectura", "pH 4.00");
        step = Step::CANCEL;
        return false;
      }
      if (&remoteManager) remoteManager.log(String("pH cal: V4=") + String(V4,5) + " T2=" + String(T2,1));
      step = Step::APPLY;
      return false;
    }

    case Step::APPLY: {
      // Temperatura de calibración (promedio válido, o fallback 25 °C)
      float tCalC;
      bool t1ok = isfinite(T1) && T1 > -40.0f && T1 < 125.0f;
      bool t2ok = isfinite(T2) && T2 > -40.0f && T2 < 125.0f;
      if (t1ok && t2ok)      tCalC = 0.5f * (T1 + T2);
      else if (t1ok)         tCalC = T1;
      else if (t2ok)         tCalC = T2;
      else                   tCalC = 25.0f;

      // Aplica en el driver -> usa VOLTAJES mediana
      ph.setTwoPointCalibration(7.00f, V7, 4.00f, V4, tCalC);

      // Persistir en EEPROM (2p) e invalidar 3p
      eeprom.setPH2pt(V7, V4, tCalC);
      eeprom.setPH3pt(NAN, NAN, NAN, NAN);
      #ifdef CONFIGSTORE_HAS_PH_CAL_MODE
        eeprom.setPHCalMode(ConfigStore::PHCalMode::PH2);
      #endif

      if (!eeprom.save()) {
        if (&remoteManager) remoteManager.log(String("EEPROM save PH2pt fallo: ") + eeprom.lastError());
      } else {
        if (&remoteManager) remoteManager.log("EEPROM: PH2pt guardado (3p invalidado)");
      }

      uart2.setLastResult("CAL_PH_2PT_OK");

      char l2[17];
      snprintf(l2, sizeof(l2), "V7=%.3f V4=%.3f", V7, V4);
      lcd.splash("pH calibrado", l2, 900);
      if (&remoteManager) remoteManager.log(String("pH cal 2pt OK (Tcal=") + String(tCalC,1) + "C)");
      step = Step::DONE;
      return false;
    }

    case Step::CANCEL: {
      lcd.splash("Calibracion", "Cancelada", 700);
      uart2.setLastResult("CAL_PH_2PT_CANCEL");
      step = Step::DONE;
      return false;
    }

    case Step::DONE:
    default: {
      step = Step::START;
      return true;
    }
  }
}

static bool runPHCalibration_4_7_10(uint16_t samples = 64, bool piecewise = false) {
  // Externs como en 2 puntos
  extern PHManager               ph;
  extern ADS1115Manager          ads;
  extern float                   readThermo();
  extern ConfigStore             eeprom;
  extern UartProto::UARTManager  uart2;

  enum class Step : uint8_t {
    START,
    WAIT_4,  CAPT_4,
    WAIT_7,  CAPT_7,
    WAIT_10, CAPT_10,
    APPLY,
    DONE,
    CANCEL
  };
  static Step step = Step::START;

  // Voltajes (mediana) y temperaturas (trazabilidad)
  static float V4 = NAN, V7 = NAN, V10 = NAN;
  static float T4 = NAN, T7 = NAN, T10 = NAN;

  // --- UI helpers ---
  auto title = []() { lcd.printAt(0,0,"Calibrar pH 3pt"); };
  auto ask4  = [&](){ title(); lcd.printAt(0,1,"Buffer 4.00  OK"); };
  auto ask7  = [&](){ title(); lcd.printAt(0,1,"Buffer 7.00  OK"); };
  auto ask10 = [&](){ title(); lcd.printAt(0,1,"Buffer 10.0 OK"); };
  auto busy  = [&](const char* m){ title(); lcd.printAt(0,1,m); };
  auto splash= [&](const char* a,const char* b="", uint16_t ms=800){ lcd.splash(a,b,ms); };

  // ---- Progreso mapeado a 0..100 (mantener “x/sample” pero en % en pantalla) ----
  auto showProgressPct = [&](const char* etiqueta, uint16_t i, uint16_t N){
    // i: 1..N
    uint16_t pct = (N == 0) ? 100 : (uint16_t)((uint32_t)i * 100UL / (uint32_t)N);
    if (pct > 100) pct = 100;
    char l0[17], l1[17];
    snprintf(l0, sizeof(l0), "Calibrar pH");
    // Ej: "pH7  63%"  (mantiene la idea de avance por muestra, mapeado a 0-100)
    snprintf(l1, sizeof(l1), "%-4s %3u%%", etiqueta, pct);
    lcd.printAt(0,0,l0);
    lcd.printAt(0,1,l1);
  };

  // ---- Utilidades: insertion sort + mediana ----
  auto insertionSort = [](float* arr, uint16_t n){
    for (uint16_t i = 1; i < n; ++i) {
      float key = arr[i];
      int j = (int)i - 1;
      while (j >= 0 && arr[j] > key) { arr[j+1] = arr[j]; --j; }
      arr[j+1] = key;
    }
  };
  auto medianFrom = [&](float* buf, uint16_t k) -> float {
    if (k < 3) return NAN;              // muy pocas válidas para robustez
    insertionSort(buf, k);
    if (k & 1) return buf[k/2];
    uint16_t r = k/2; return 0.5f*(buf[r-1] + buf[r]);
  };

  // ---- Mediana del VOLTAJE (con compensación T) mostrando % de progreso ----
  auto medianVoltPH = [&](uint16_t N, const char* etiqueta) -> float {
    static constexpr uint16_t MAX_S = 128;
    if (N == 0) N = 1;
    if (N > MAX_S) N = MAX_S;

    float buf[MAX_S];
    uint16_t k = 0;

    for (uint16_t i = 0; i < N; ++i) {
      showProgressPct(etiqueta, (uint16_t)(i+1), N);

      float tC = readThermo();
      bool tOk = isfinite(tC) && tC > -40.0f && tC < 125.0f;
      float tUsed = tOk ? tC : 25.0f;

      float phVal = NAN, v = NAN;
      if (ph.readPH(tUsed, phVal, &v) && isfinite(v)) {
        buf[k++] = v;
      }
      delay(4);
    }
    return medianFrom(buf, k);
  };

  Buttons::testButtons();

  switch (step) {
    case Step::START: {
      ads.setAveraging((uint8_t)min<uint16_t>(samples, 255));
      V4 = V7 = V10 = NAN;  T4 = T7 = T10 = NAN;
      lcd.clear(); ask4();
      if (&remoteManager) remoteManager.log("pH 3pt: START -> pH4");
      step = Step::WAIT_4;
      return false;
    }

    case Step::WAIT_4:
      if (Buttons::BTN_ESC.value){ Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value) { Buttons::BTN_OK.reset();  step = Step::CAPT_4;  return false; }
      return false;

    case Step::CAPT_4: {
      busy("Midiendo pH4...");
      V4 = medianVoltPH(samples, "pH4");
      T4 = readThermo();
      if (!isfinite(V4)) { splash("Error lectura","pH 4.00", 900); step = Step::CANCEL; return false; }
      if (&remoteManager) remoteManager.log(String("pH 3pt: V4=")+String(V4,5)+" T4="+String(T4,1));
      splash("pH 4.00 OK","",600);
      lcd.clear(); ask7();
      step = Step::WAIT_7;
      return false;
    }

    case Step::WAIT_7:
      if (Buttons::BTN_ESC.value){ Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value) { Buttons::BTN_OK.reset();  step = Step::CAPT_7;  return false; }
      return false;

    case Step::CAPT_7: {
      busy("Midiendo pH7...");
      V7 = medianVoltPH(samples, "pH7");
      T7 = readThermo();
      if (!isfinite(V7)) { splash("Error lectura","pH 7.00", 900); step = Step::CANCEL; return false; }
      if (&remoteManager) remoteManager.log(String("pH 3pt: V7=")+String(V7,5)+" T7="+String(T7,1));
      splash("pH 7.00 OK","",600);
      lcd.clear(); ask10();
      step = Step::WAIT_10;
      return false;
    }

    case Step::WAIT_10:
      if (Buttons::BTN_ESC.value){ Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value) { Buttons::BTN_OK.reset();  step = Step::CAPT_10; return false; }
      return false;

    case Step::CAPT_10: {
      busy("Midiendo pH10...");
      V10 = medianVoltPH(samples, "pH10");
      T10 = readThermo();
      if (!isfinite(V10)) { splash("Error lectura","pH 10.0", 900); step = Step::CANCEL; return false; }
      if (&remoteManager) remoteManager.log(String("pH 3pt: V10=")+String(V10,5)+" T10="+String(T10,1));
      step = Step::APPLY;
      return false;
    }

    case Step::APPLY: {
      // Tcal promedio válido (o 25 °C)
      int nOK = 0; float accT = 0.0f;
      auto addIfOk = [&](float T){ if (isfinite(T) && T>-40.0f && T<125.0f) { accT += T; ++nOK; } };
      addIfOk(T4); addIfOk(T7); addIfOk(T10);
      float tCalC = (nOK > 0) ? (accT / nOK) : 25.0f;

      if (!isfinite(V4) || !isfinite(V7) || !isfinite(V10)) {
        splash("Cal pH 3pt","incompleta", 900); step = Step::CANCEL; return false;
      }

      // Persistencia primero
      eeprom.setPH3pt(V4, V7, V10, tCalC);
      #ifdef CONFIGSTORE_HAS_PH_CAL_MODE
        eeprom.setPHCalMode(ConfigStore::PHCalMode::PH3);
      #endif
      if (!eeprom.save()) {
        if (&remoteManager) remoteManager.log(String("EEPROM save PH3pt fallo: ")+eeprom.lastError());
      } else {
        if (&remoteManager) remoteManager.log("EEPROM: PH3pt guardado");
      }

      // Aplicar al PHManager (modo PW o LS según flag)
      if (!ph.setThreePointCalibration(V4, V7, V10, tCalC, /*piecewise=*/piecewise)) {
        if (&remoteManager) remoteManager.log("pH 3pt: setThreePointCalibration fallo");
        splash("PHManager","Set cal 3pt fallo", 900);
        step = Step::DONE; return false;
      }

      // UI final
      char l2[17];
      snprintf(l2, sizeof(l2), "V4=%.3f V7=%.3f", V4, V7);
      splash("pH 3pt OK", l2, 700);
      snprintf(l2, sizeof(l2), "V10=%.3f T=%.1fC", V10, tCalC);
      splash("Guardado", l2, 900);

      uart2.setLastResult(piecewise ? "CAL_PH_3PT_PW_OK" : "CAL_PH_3PT_LS_OK");
      step = Step::DONE;
      return false;
    }

    case Step::CANCEL:
      lcd.splash("Calibracion","Cancelada",700);
      uart2.setLastResult("CAL_PH_3PT_CANCEL");
      step = Step::DONE;
      return false;

    case Step::DONE:
    default:
      step = Step::START;
      return true;
  }
}

static bool PumpFillLearnWizard() {
  extern ConfigStore  eeprom;
  extern PumpsManager pumps;

  enum class ItemId : uint8_t { KCL_FILL, H2O_FILL, SAMPLE_FILL, DRAIN, COUNT };
  struct Item { const char* name; ItemId id; };

  static const Item items[] = {
    { "KCL FILL",   ItemId::KCL_FILL   },
    { "H2O FILL",   ItemId::H2O_FILL   },
    { "SAMPLE F",   ItemId::SAMPLE_FILL},
    { "DRAIN",      ItemId::DRAIN      },
  };
  static const uint8_t N = (uint8_t)ItemId::COUNT;

  enum class Step : uint8_t { SELECT, WAIT, RUN, DONE };
  static Step     step          = Step::SELECT;
  static uint8_t  cursor        = 0;
  static int8_t   lastCursor    = -1;
  static bool     needFirstDraw = true;
  static bool     armed         = false;
  static unsigned long t0_ms    = 0;
  static ItemId   fillId        = ItemId::KCL_FILL;

  auto pumpForFill = [&](ItemId id)->PumpId{
    switch(id){
      case ItemId::KCL_FILL:    return PumpId::KCL;
      case ItemId::H2O_FILL:    return PumpId::H2O;
      case ItemId::SAMPLE_FILL: return PumpId::SAMPLE1;
      case ItemId::DRAIN:       return PumpId::DRAIN;
      default: return PumpId::KCL;
    }
  };

  // Guardado según layout v6
  auto saveDurationMs = [&](ItemId id, uint32_t ms){
    if (id == ItemId::DRAIN) ms = (ms < 500UL) ? 500UL : ms;
    else                     ms = (ms < 100UL) ? 100UL : ms;
    switch(id){
      case ItemId::KCL_FILL:    eeprom.setKclFillMs(ms);     break;
      case ItemId::H2O_FILL:    eeprom.setH2oFillMs(ms);     break;
      case ItemId::SAMPLE_FILL: eeprom.setSampleFillMs(ms);  break;
      case ItemId::DRAIN:       eeprom.setDrainMs(ms);       break;
      default: break;
    }
    eeprom.save();
  };

  auto renderSelect = [&](bool force=false){
    if(force || (int8_t)cursor!=lastCursor){
      char l0[17], l1[17];
      snprintf(l0,sizeof(l0),"Aprender tiempo");
      snprintf(l1,sizeof(l1),">%-8s OK>", items[cursor].name);
      lcd.printAt(0,0,l0); lcd.printAt(0,1,l1);
      lastCursor=(int8_t)cursor;
    }
  };
  auto renderWait = [&](){
    char l0[17], l1[17];
    if (items[cursor].id == ItemId::DRAIN) snprintf(l0,sizeof(l0),"%-8s", items[cursor].name);
    else                                    snprintf(l0,sizeof(l0),"%-8s FILL", items[cursor].name);
    snprintf(l1,sizeof(l1),"t=0s  OK:start");
    lcd.printAt(0,0,l0); lcd.printAt(0,1,l1);
  };
  auto renderRun = [&](){
    unsigned long secs=(millis()-t0_ms)/1000UL;
    char l0[17], l1[17];
    if (items[cursor].id == ItemId::DRAIN) snprintf(l0,sizeof(l0),"%-8s", items[cursor].name);
    else                                    snprintf(l0,sizeof(l0),"%-8s FILL",items[cursor].name);
    snprintf(l1,sizeof(l1),"t=%lus ESC:stop",(unsigned long)secs);
    lcd.printAt(0,0,l0); lcd.printAt(0,1,l1);
  };

  if(needFirstDraw){
    lcd.clear(); needFirstDraw=false; lastCursor=-1; step=Step::SELECT; armed=false;
    Buttons::BTN_OK.reset(); Buttons::BTN_ESC.reset(); Buttons::BTN_UP.reset(); Buttons::BTN_DOWN.reset();
    renderSelect(true);
  }

  if (Buttons::BTN_ESC.value && step==Step::SELECT){
    Buttons::BTN_ESC.reset(); needFirstDraw=true; return true;
  }

  switch(step){
    case Step::SELECT:{
      if(!armed){ Buttons::BTN_OK.reset(); Buttons::BTN_ESC.reset(); Buttons::BTN_UP.reset(); Buttons::BTN_DOWN.reset(); armed=true; return false; }
      if (Buttons::BTN_UP.value){ Buttons::BTN_UP.reset(); cursor=(cursor==0)?(N-1):(cursor-1); renderSelect(true); return false; }
      if (Buttons::BTN_DOWN.value){ Buttons::BTN_DOWN.reset(); cursor=(cursor+1)%N; renderSelect(true); return false; }
      if (Buttons::BTN_OK.value){
        Buttons::BTN_OK.reset(); fillId=items[cursor].id; lcd.clear(); renderWait(); step=Step::WAIT; return false;
      }
      return false;
    }
    case Step::WAIT:{
      if (Buttons::BTN_OK.value){
        Buttons::BTN_OK.reset(); pumps.on(pumpForFill(fillId));
        t0_ms=millis(); lcd.clear(); renderRun(); step=Step::RUN; return false;
      }
      if (Buttons::BTN_ESC.value){
        Buttons::BTN_ESC.reset(); lcd.splash("Cancelado","",500); step=Step::DONE; return false;
      }
      renderWait(); return false;
    }
    case Step::RUN:{
      renderRun();
      if (Buttons::BTN_ESC.value){
        Buttons::BTN_ESC.reset();
        pumps.off(pumpForFill(fillId));
        uint32_t ms=(uint32_t)(millis()-t0_ms);
        saveDurationMs(fillId, ms);
        char l0[17], l1[17];
        if (items[cursor].id == ItemId::DRAIN) snprintf(l0,sizeof(l0),"%-8s",items[cursor].name);
        else                                    snprintf(l0,sizeof(l0),"%-8s FILL",items[cursor].name);
        snprintf(l1,sizeof(l1),"save %lus",(unsigned long)(ms/1000));
        lcd.splash(l0,l1,800);
        step=Step::DONE; return false;
      }
      return false;
    }
    case Step::DONE:
    default:{
      lcd.clear(); renderSelect(true); step=Step::SELECT; return false;
    }
  }
}

static bool PumpTimeoutsWizard() {
  extern ConfigStore eeprom;

  enum class ItemId : uint8_t { SAMPLE_T, DRAIN_T, ESTAB_T, SAMPLE_CNT, COUNT };
  struct Item { const char* name; ItemId id; bool isSeconds; };

  static const Item items[] = {
    { "SAMPLE",  ItemId::SAMPLE_T,  true  },
    { "DRAIN",   ItemId::DRAIN_T,   true  },
    { "O2 STAB", ItemId::ESTAB_T,   true  },
    { "SAMPLES", ItemId::SAMPLE_CNT,false },
  };
  static const uint8_t N = (uint8_t)ItemId::COUNT;

  enum class Step : uint8_t { SELECT, EDIT, DONE };
  static Step     step          = Step::SELECT;
  static uint8_t  cursor        = 0;
  static int8_t   lastCursor    = -1;
  static bool     needFirstDraw = true;
  static int32_t  editValue     = 0;

  auto clampSecs = [&](int32_t s){ if (s<1) s=1; if (s>600) s=600; return s; };
  auto clampSamples = [&](int32_t n){ if (n<0) n=0; if (n>4) n=4; return n; };

  auto getSecs = [&](ItemId id)->int32_t {
    switch(id){
      case ItemId::SAMPLE_T: return (int32_t)(eeprom.sampleTimeoutMs()/1000UL);
      case ItemId::DRAIN_T:  return (int32_t)(eeprom.drainTimeoutMs()/1000UL);
      case ItemId::ESTAB_T:  return (int32_t)(eeprom.o2StabilizationMs()/1000UL);
      default: return 0;
    }
  };
  auto setSecs = [&](ItemId id, int32_t s){
    uint32_t ms = (uint32_t)clampSecs(s)*1000UL;
    switch(id){
      case ItemId::SAMPLE_T: eeprom.setSampleTimeoutMs(ms); break;
      case ItemId::DRAIN_T:  eeprom.setDrainTimeoutMs(ms);  break;
      case ItemId::ESTAB_T:  eeprom.setO2StabilizationMs(ms); break;
      default: break;
    }
  };
  auto getSamples = [&](){ return (int32_t)eeprom.sampleCount(); };
  auto setSamples = [&](int32_t n){ eeprom.setSampleCount((uint8_t)clampSamples(n)); };

  auto renderSelect = [&](bool force=false){
    if(force || (int8_t)cursor!=lastCursor){
      char l0[17], l1[17];
      snprintf(l0,sizeof(l0),"Timeouts v6");
      snprintf(l1,sizeof(l1),">%-8s OK>", items[cursor].name);
      lcd.printAt(0,0,l0); lcd.printAt(0,1,l1);
      lastCursor=(int8_t)cursor;
    }
  };
  auto renderEdit = [&](){
    char l0[17]; snprintf(l0,sizeof(l0),"%-8s EDIT",items[cursor].name);
    char l1[17];
    if(items[cursor].isSeconds) snprintf(l1,sizeof(l1),"%4lds  OK:save",(long)editValue);
    else                        snprintf(l1,sizeof(l1),"%4ldx  OK:save",(long)editValue);
    lcd.printAt(0,0,l0); lcd.printAt(0,1,l1);
  };

  if(needFirstDraw){
    lcd.clear(); needFirstDraw=false; lastCursor=-1; step=Step::SELECT;
    Buttons::BTN_OK.reset(); Buttons::BTN_ESC.reset(); Buttons::BTN_UP.reset(); Buttons::BTN_DOWN.reset();
    renderSelect(true);
  }

  if (Buttons::BTN_ESC.value) { Buttons::BTN_ESC.reset(); needFirstDraw=true; return true; }

  switch(step){
    case Step::SELECT:{
      if (Buttons::BTN_UP.value){ Buttons::BTN_UP.reset(); cursor=(cursor==0)?(N-1):(cursor-1); renderSelect(true); return false; }
      if (Buttons::BTN_DOWN.value){ Buttons::BTN_DOWN.reset(); cursor=(cursor+1)%N; renderSelect(true); return false; }
      if (Buttons::BTN_OK.value){
        Buttons::BTN_OK.reset();
        if(items[cursor].isSeconds)
          editValue = clampSecs(getSecs(items[cursor].id));
        else
          editValue = clampSamples(getSamples());
        lcd.clear(); renderEdit(); step=Step::EDIT; return false;
      }
      return false;
    }
    case Step::EDIT:{
      if (Buttons::BTN_UP.value){ Buttons::BTN_UP.reset();
        editValue = items[cursor].isSeconds ? clampSecs(editValue+1) : clampSamples(editValue+1);
        renderEdit(); return false; }
      if (Buttons::BTN_DOWN.value){ Buttons::BTN_DOWN.reset();
        editValue = items[cursor].isSeconds ? clampSecs(editValue-1) : clampSamples(editValue-1);
        renderEdit(); return false; }

      if (Buttons::BTN_OK.value){
        Buttons::BTN_OK.reset();
        if(items[cursor].isSeconds) setSecs(items[cursor].id, editValue);
        else                        setSamples(editValue);
        eeprom.save();
        char l0[17], l1[17];
        snprintf(l0,sizeof(l0),"%-8s GUARD",items[cursor].name);
        if(items[cursor].isSeconds) snprintf(l1,sizeof(l1),"%ld s",(long)editValue);
        else                        snprintf(l1,sizeof(l1),"%ld bombas",(long)editValue);
        lcd.splash(l0,l1,700);
        step=Step::DONE; return false;
      }
      if (Buttons::BTN_ESC.value){
        Buttons::BTN_ESC.reset(); lcd.splash("Sin cambios","",500); step=Step::DONE; return false;
      }
      renderEdit(); return false;
    }
    case Step::DONE:
    default:{
      lcd.clear(); renderSelect(true); step=Step::SELECT; return false;
    }
  }
}

static bool PumpTimesTestWizard() {
  // extern PumpsManager         pumps;
  // extern ConfigStore          eeprom;
  // extern LevelSensorsManager  levels;   // O2/PH/H2O/KCL (pull-up externas, activo en alto)

  // // ---- Timeouts por bomba desde EEPROM ----
  // auto getTimeoutMs = [&](PumpId id) -> uint32_t {
  //   switch (id) {
  //     case PumpId::KCL:      return eeprom.kclTimeoutMs();
  //     case PumpId::H2O:      return eeprom.h2oTimeoutMs();
  //     case PumpId::SAMPLE1:
  //     case PumpId::SAMPLE2:
  //     case PumpId::SAMPLE3:
  //     case PumpId::SAMPLE4:  return eeprom.sampleTimeoutMs();
  //     default:               return 0;
  //   }
  // };

  // // ---- Construcción dinámica del menú según sampleCount ----
  // struct Item { const char* name; PumpId id; };

  // // Máx: KCL, H2O + 4 samples = 6 ítems
  // static Item items[6];
  // static uint8_t N = 0;

  // auto rebuildItems = [&](){
  //   N = 0;
  //   items[N++] = { "KCL", PumpId::KCL };
  //   items[N++] = { "H2O", PumpId::H2O };

  //   uint8_t sc = eeprom.sampleCount();      // 1..4 recomendado
  //   if (sc > 4) sc = 4;                     // tope de seguridad

  //   if (sc >= 1) items[N++] = { "S1", PumpId::SAMPLE1 };
  //   if (sc >= 2) items[N++] = { "S2", PumpId::SAMPLE2 };
  //   if (sc >= 3) items[N++] = { "S3", PumpId::SAMPLE3 };
  //   if (sc >= 4) items[N++] = { "S4", PumpId::SAMPLE4 };
  // };

  // // ---- FSM ----
  // enum class Step : uint8_t { SELECT, RUNNING, DONE };
  // static Step step = Step::SELECT;

  // static uint8_t  cursor        = 0;
  // static int8_t   lastCursor    = -1;
  // static bool     needFirstDraw = true;
  // static bool     armed         = false;   // evita auto-arranque por OK previo
  // static unsigned long tEnd     = 0;
  // static PumpId   selId         = PumpId::KCL;
  // static uint32_t runMs         = 0;

  // auto clearLatches = [](){
  //   if (Buttons::BTN_OK.value)   Buttons::BTN_OK.reset();
  //   if (Buttons::BTN_ESC.value)  Buttons::BTN_ESC.reset();
  //   if (Buttons::BTN_UP.value)   Buttons::BTN_UP.reset();
  //   if (Buttons::BTN_DOWN.value) Buttons::BTN_DOWN.reset();
  // };

  // auto renderSelect = [&](bool force=false) {
  //   if (force || (int8_t)cursor != lastCursor) {
  //     uint32_t ms = getTimeoutMs(items[cursor].id);
  //     char l0[17]; snprintf(l0, sizeof(l0), "Probar bombeo");
  //     char l1[17]; snprintf(l1, sizeof(l1), ">%-3s %lus", items[cursor].name,
  //                           (unsigned long)(ms/1000));
  //     lcd.printAt(0,0, l0);
  //     lcd.printAt(0,1, l1);
  //     lastCursor = (int8_t)cursor;
  //   }
  // };

  // auto renderRunning = [&](){
  //   long rem = (long)(tEnd - millis());
  //   if (rem < 0) rem = 0;
  //   char l0[17]; snprintf(l0, sizeof(l0), "%-3s TEST", items[cursor].name);
  //   char l1[17]; snprintf(l1, sizeof(l1), "rem=%lus ESC:stop", (unsigned long)(rem/1000));
  //   lcd.printAt(0,0, l0);
  //   lcd.printAt(0,1, l1);
  // };

  // // ---- Paro por sensor (igual que en AutoMode actual) ----
  // auto shouldStopBySensor = [&](PumpId pump) -> bool {
  //   switch (pump) {
  //     case PumpId::KCL:      return levels.kcl();                 // HIGH -> activo
  //     case PumpId::H2O:      return levels.h2o();                 // HIGH -> activo
  //     case PumpId::SAMPLE1:
  //     case PumpId::SAMPLE2:
  //     case PumpId::SAMPLE3:
  //     case PumpId::SAMPLE4:  return (!levels.o2() && !levels.ph()); // confirmación doble (mantener corrección)
  //     default:               return false;
  //   }
  // };

  // if (needFirstDraw) {
  //   lcd.clear();
  //   needFirstDraw = false;
  //   lastCursor    = -1;
  //   step          = Step::SELECT;
  //   armed         = false;

  //   rebuildItems();          // <<-- construir menú según sampleCount
  //   if (cursor >= N) cursor = 0;

  //   renderSelect(true);
  // }

  // switch (step) {
  //   case Step::SELECT: {
  //     // Primera pasada: limpia latches para no entrar corriendo por un OK previo
  //     if (!armed) {
  //       clearLatches();
  //       armed = true;
  //       return false;
  //     }

  //     if (Buttons::BTN_UP.value)   { Buttons::BTN_UP.reset();   cursor = (cursor==0)? (N-1) : (cursor-1); renderSelect(true); }
  //     if (Buttons::BTN_DOWN.value) { Buttons::BTN_DOWN.reset(); cursor = (cursor+1) % N;                  renderSelect(true); }

  //     if (Buttons::BTN_OK.value) {
  //       Buttons::BTN_OK.reset();
  //       selId = items[cursor].id;
  //       runMs = getTimeoutMs(selId);
  //       if (runMs == 0) {
  //         lcd.splash("Timeout=0", "Configuralo", 800);
  //         return false;
  //       }
  //       pumps.on(selId);
  //       tEnd = millis() + runMs;
  //       lcd.clear();
  //       renderRunning();
  //       step = Step::RUNNING;
  //       return false;
  //     }

  //     if (Buttons::BTN_ESC.value) {
  //       Buttons::BTN_ESC.reset();
  //       needFirstDraw = true;
  //       return true;
  //     }
  //     return false;
  //   }

  //   case Step::RUNNING: {
  //     // Cancelación manual
  //     if (Buttons::BTN_ESC.value) {
  //       Buttons::BTN_ESC.reset();
  //       pumps.off(selId);
  //       lcd.splash("Prueba cancel", "", 600);
  //       step = Step::DONE;
  //       return false;
  //     }

  //     // Paro por sensor o por timeout
  //     const bool timeReached   = ((long)(millis() - tEnd) >= 0);
  //     const bool sensorReached = shouldStopBySensor(selId);

  //     if (timeReached || sensorReached) {
  //       pumps.off(selId);
  //       char l0[17], l1[17];
  //       snprintf(l0, sizeof(l0), "%-3s OK", items[cursor].name);
  //       snprintf(l1, sizeof(l1), sensorReached ? "Fin: SENS" : "Fin: TIME");
  //       lcd.splash(l0, l1, 700);
  //       step = Step::DONE;
  //       return false;
  //     }

  //     renderRunning();
  //     return false;
  //   }

  //   case Step::DONE:
  //   default: {
  //     needFirstDraw = true;
  //     step = Step::SELECT;
  //     return true;
  //   }
  // }
}

static bool ManualPumpsTick() {
  struct Item { const char* name; PumpId id; };
  static const Item items[] = {
    { "KCL",     PumpId::KCL     },
    { "H2O",     PumpId::H2O     },
    { "DRAIN",   PumpId::DRAIN   },
    { "MIXER",   PumpId::MIXER   },
    { "SAMPLE1", PumpId::SAMPLE1 },
    { "SAMPLE2", PumpId::SAMPLE2 },
    { "SAMPLE3", PumpId::SAMPLE3 },
    { "SAMPLE4", PumpId::SAMPLE4 },
  };
  static const uint8_t N = sizeof(items) / sizeof(items[0]);

  // Estado persistente de la pantalla
  static uint8_t cursor        = 0;   // índice actual 0..N-1
  static int8_t  lastCursor    = -1;  // para repintado condicional
  static bool    lastState     = false;
  static bool    needFirstDraw = true;

  auto render = [&](bool force=false) {
    const bool st = pumps.isOn(items[cursor].id);
    if (force || (int8_t)cursor != lastCursor || st != lastState) {
      char l0[17];
      snprintf(l0, sizeof(l0), "%-10s %s", items[cursor].name, st ? "ON " : "OFF");
      lcd.printAt(0, 0, l0);
      lcd.printAt(0, 1, "OK:on  ESC:off");
      lastCursor = (int8_t)cursor;
      lastState  = st;
    }
  };

  // Primer pintado al entrar
  if (needFirstDraw) {
    lcd.clear();
    needFirstDraw = false;
    render(true);
  } else if (lastCursor < 0) {
    // Asegura al menos un render
    render(true);
  }

  // Navegación
  if (Buttons::BTN_UP.value) {
    Buttons::BTN_UP.reset();
    cursor = (cursor == 0) ? (N - 1) : (cursor - 1);
    render(true);
    return false;
  }
  if (Buttons::BTN_DOWN.value) {
    Buttons::BTN_DOWN.reset();
    cursor = (cursor + 1) % N;
    render(true);
    return false;
  }

  // Acciones
  if (Buttons::BTN_OK.value) {
    Buttons::BTN_OK.reset();
    pumps.on(items[cursor].id);     // ENCENDER
    render(true);
    return false;
  }
  if (Buttons::BTN_ESC.value) {
    Buttons::BTN_ESC.reset();
    if (pumps.isOn(items[cursor].id)) {
      pumps.off(items[cursor].id);  // APAGAR y quedarse
      render(true);
      return false;
    } else {
      // Ya estaba OFF → salir al menú anterior
      needFirstDraw = true;         // fuerza primer render al reingresar
      return true;
    }
  }

  return false; // permanecer en esta pantalla
}

static bool ManualLevelsTick() {
  // Estado persistente local
  static bool  needFirstDraw = true;
  static int8_t lastO2  = -1;  // -1 = desconocido, 0 = LOW, 1 = HIGH
  static int8_t lastPH  = -1;
  static int8_t lastKCL = -1;
  static int8_t lastH2O = -1;

  auto readAndRender = [&](bool force = false) {
  // Activo = HIGH (pull-ups externas).
    const bool o2  = levels.o2();   // true → activo → "BAJO"
    const bool ph  = levels.ph();
    const bool kcl = levels.kcl();
    const bool h2o = levels.h2o();

    // Codificamos en 0/1 para comparar cambios
    const int8_t o2_i  = o2  ? 1 : 0;
    const int8_t ph_i  = ph  ? 1 : 0;
    const int8_t kcl_i = kcl ? 1 : 0;
    const int8_t h2o_i = h2o ? 1 : 0;

    const bool changed =
      (o2_i  != lastO2)  ||
      (ph_i  != lastPH)  ||
      (kcl_i != lastKCL) ||
      (h2o_i != lastH2O);

    if (force || changed) {
      // Formato compacto para 16x2:
      // L0: "O2:XXX  pH:XXX"
      // L1: "H2O:XXX KCL:XXX"
      const char* sO2  = o2  ? "HI " : "LO ";
      const char* sPH  = ph  ? "HI " : "LO ";
      const char* sH2O = h2o ? "HI " : "LO ";
      const char* sKCL = kcl ? "HI " : "LO ";

      char l0[17], l1[17];
      snprintf(l0, sizeof(l0), "O2:%s  pH:%s",  sO2,  sPH);
      snprintf(l1, sizeof(l1), "H2O:%s KCL:%s", sH2O, sKCL);

      lcd.printAt(0, 0, l0);
      lcd.printAt(0, 1, l1);

      lastO2  = o2_i;
      lastPH  = ph_i;
      lastKCL = kcl_i;
      lastH2O = h2o_i;
    }
  };

  // Primer pintado al entrar
  if (needFirstDraw) {
    lcd.clear();
    needFirstDraw = false;
    readAndRender(true);
  } else {
    // refresco oportunista si cambia el estado
    readAndRender(false);
  }

  // Controles
  if (Buttons::BTN_ESC.value) {
    Buttons::BTN_ESC.reset();
    needFirstDraw = true;   // fuerza redraw al regresar a esta pantalla
    return true;            // salir al menú anterior
  }
  if (Buttons::BTN_OK.value) {
    Buttons::BTN_OK.reset();
    readAndRender(true);    // refresco manual
  }
  if (Buttons::BTN_UP.value)   Buttons::BTN_UP.reset();   // sin acción
  if (Buttons::BTN_DOWN.value) Buttons::BTN_DOWN.reset(); // sin acción

  return false; // permanecer en esta pantalla
}

static bool AutoModeTick() {
  extern PumpsManager           pumps;
  extern ConfigStore            eeprom;
  extern LevelSensorsManager    levels;     // O2/PH/H2O (pull-up externas, activo=HIGH)
  extern float                  readPH();
  extern float                  readO2();
  extern float                  readThermo();
  extern UartProto::UARTManager uart2;      // para registrar pH por sample

  // ---------- Tipos de paso ----------
  enum class Op : uint8_t {
    READ_LEVELS,
    PUMP_FOR,        // 1) espera sensor (con/ sin timeout)  2) mantiene FILL  3) continúa
    MIXER_ON,
    MIXER_OFF,
    WAIT,
    READ_O2,
    READ_PH,
    END
  };

  enum class DurKind : uint8_t {
    CONST,           // usa Step.ms
    SAMPLE_T,        // timeout de SAMPLE (fase 1)
    DRAIN_T,         // timeout de DRAIN  (fase 1)
    MIX_WAIT_VAR,    // estabilización configurable O2
    PH_WAIT_VAR      // estabilización configurable pH
  };

  struct Step {
    Op          op;
    PumpId      pump;      // SAMPLE es placeholder (se resuelve por ciclo)
    DurKind     dkind;
    uint32_t    ms;        // sólo si dkind==CONST
    const char* label;
  };

  // ---------- Tiempos ----------
  static uint32_t mixWaitMs = 30000;            // fallback
  mixWaitMs = eeprom.o2StabilizationMs();
  static const uint32_t MSG_MS = 1500;

  // Timeouts (fase 1: esperar sensor)
  auto getSampleTimeoutMs = [&](){ return eeprom.sampleTimeoutMs(); };
  auto getDrainTimeoutMs  = [&](){ return eeprom.drainTimeoutMs();  };

  auto computeTimeoutMs = [&](DurKind dk) -> uint32_t {
    switch (dk) {
      case DurKind::SAMPLE_T:     return getSampleTimeoutMs();
      case DurKind::DRAIN_T:      return getDrainTimeoutMs();
      case DurKind::MIX_WAIT_VAR: return mixWaitMs;
      case DurKind::PH_WAIT_VAR:  return eeprom.phStabilizationMs();
      case DurKind::CONST:        return 0;
      default:                    return 0;
    }
  };

  // Fill (fase 2: mantener encendida)
  auto getH2oFillMs    = [&](){ return eeprom.h2oFillMs();    };
  auto getSampleFillMs = [&](){ return eeprom.sampleFillMs(); };
  auto getDrainMs      = [&](){ return eeprom.drainMs();      };

  auto computeFillMs = [&](PumpId id) -> uint32_t {
    switch (id) {
      case PumpId::H2O:     return getH2oFillMs();
      case PumpId::SAMPLE1:
      case PumpId::SAMPLE2:
      case PumpId::SAMPLE3:
      case PumpId::SAMPLE4: return getSampleFillMs();
      case PumpId::DRAIN:   return getDrainMs();
      default:              return 0;
    }
  };

  // ---------- Receta ----------
  static const Step STEPS[] = {
    { Op::READ_LEVELS, PumpId::H2O,     DurKind::CONST,        0, "Lee niveles" },
    { Op::PUMP_FOR,    PumpId::DRAIN,   DurKind::DRAIN_T,      0, "Drenando"    },
    { Op::PUMP_FOR,    PumpId::SAMPLE1, DurKind::SAMPLE_T,     0, "Sample"      }, // SAMPLE dinámico
    { Op::MIXER_ON,    PumpId::MIXER,   DurKind::CONST,        0, "Mixer ON"    },
    { Op::WAIT,        PumpId::MIXER,   DurKind::MIX_WAIT_VAR, 0, "Mezclando O2" },
    { Op::MIXER_OFF,   PumpId::MIXER,   DurKind::CONST,        0, "Mixer OFF"   },
    { Op::READ_O2,     PumpId::MIXER,   DurKind::CONST,        0, "Leer O2"     },
    { Op::WAIT,        PumpId::MIXER,   DurKind::PH_WAIT_VAR,  0, "Espera pH"   },
    { Op::READ_PH,     PumpId::MIXER,   DurKind::CONST,        0, "Leer pH"     },
    { Op::PUMP_FOR,    PumpId::DRAIN,   DurKind::DRAIN_T,      0, "Drenando"    },
    // H2O: espera pH HIGH con timeout y luego FILL ascendente
    { Op::PUMP_FOR,    PumpId::H2O,     DurKind::SAMPLE_T,     0, "H2O"         },
    { Op::END,         PumpId::H2O,     DurKind::CONST,        0, "FIN"         },
  };
  static const uint8_t N_STEPS = sizeof(STEPS)/sizeof(STEPS[0]);

  // ---------- Estado ----------
  enum class Phase   : uint8_t { ENTER, RUN, POST, EXIT };
  enum class PumpSub : uint8_t { WAIT_SENSOR, FILL, DONE };

  static bool          started      = false;
  static uint8_t       idx          = 0;
  static Phase         phase        = Phase::ENTER;
  static PumpSub       pumpSub      = PumpSub::WAIT_SENSOR;

  static uint32_t      timeoutMs    = 0;      // fase 1
  static uint32_t      fillMs       = 0;      // fase 2
  static unsigned long t0           = 0;      // inicio WAIT_SENSOR
  static unsigned long tFillStart   = 0;      // inicio FILL
  static unsigned long tPost        = 0;
  static float         lastPHShown  = NAN;

  // iteraciones por nº de samples
  static uint8_t totalSamples = 0;   // EEPROM
  static uint8_t currentSample = 0;  // 0..totalSamples-1

  // ---------- UI ----------
  auto show = [&](const char* l0, const char* l1) {
    lcd.printAt(0, 0, l0);
    lcd.printAt(0, 1, l1);
  };

  // Timeouts: cuenta regresiva (restante / total)
  auto progressTimeoutCountdown = [&](const char* title, uint32_t totalMs, unsigned long tStart) {
    uint32_t elapsed = (uint32_t)(millis() - tStart);
    uint32_t remain  = (elapsed >= totalMs) ? 0 : (totalMs - elapsed);
    char L0[17], L1[17];
    snprintf(L0, sizeof(L0), "%s", title);
    snprintf(L1, sizeof(L1), "t=%lus/%lus", (unsigned long)(remain/1000UL), (unsigned long)(totalMs/1000UL));
    show(L0, L1);
  };

  // Fills: ascendente (transcurrido / total)
  auto progressFillUp = [&](const char* title, uint32_t totalMs, unsigned long tStart) {
    uint32_t elapsed = (uint32_t)(millis() - tStart);
    if (elapsed > totalMs) elapsed = totalMs;
    char L0[17], L1[17];
    snprintf(L0, sizeof(L0), "%s", title);
    snprintf(L1, sizeof(L1), "t=%lus/%lus", (unsigned long)(elapsed/1000UL), (unsigned long)(totalMs/1000UL));
    show(L0, L1);
  };

  // SAMPLE dinámico por ciclo
  auto samplePumpForCycle = [&]() -> PumpId {
    uint8_t n = totalSamples;
    if (n == 0) n = 1;
    uint8_t i = (currentSample % n);
    switch (i) {
      case 0:  return PumpId::SAMPLE1;
      case 1:  return PumpId::SAMPLE2;
      case 2:  return PumpId::SAMPLE3;
      default: return PumpId::SAMPLE4;
    }
  };

  // ---------- ESC cancela ----------
  if (Buttons::BTN_ESC.value) {
    Buttons::BTN_ESC.reset();
    pumps.allOff();
    lcd.splash("AUTO cancelado", "", 800);
    started = false; idx = 0; phase = Phase::ENTER; lastPHShown = NAN;
    currentSample = 0; totalSamples = 0;
    return true;
  }

  if (autoCancelRequest) {
    autoCancelRequest = false;
    pumps.allOff();
    lcd.splash("AUTO cancelado", "web", 800);
    started = false; idx = 0; phase = Phase::ENTER; lastPHShown = NAN;
    currentSample = 0; totalSamples = 0;
    return true;
  }

  // ---------- Precondición: H2O debe estar en HIGH ----------
  if (!started) {
    const bool h2oHigh = levels.h2o();
    const bool o2High  = levels.o2();
    if (!h2oHigh) {
      const char* sH2O = h2oHigh ? "HI " : "LO ";
      const char* sO2  = o2High  ? "HI " : "LO ";
      char L0[17], L1[17];
      snprintf(L0, sizeof(L0), "H2O:%s", sH2O);
      snprintf(L1, sizeof(L1), "O2:%s", sO2);
      show(L0, L1);
      return false;
    }

    totalSamples  = eeprom.sampleCount();     // 0..4
    if (totalSamples == 0) totalSamples = 1;
    currentSample = 0;

    pumps.allOff();
    lcd.splash("Niveles OK", "Iniciando...", 500);
    started     = true;
    idx         = 0;
    phase       = Phase::ENTER;
    lastPHShown = NAN;
  }

  // ---------- Sensor alcanzado? ----------
  auto sensorReached = [&](PumpId pump) -> bool {
    switch (pump) {
      case PumpId::H2O:     return levels.ph();
      case PumpId::SAMPLE1:
      case PumpId::SAMPLE2:
      case PumpId::SAMPLE3:
      case PumpId::SAMPLE4: return levels.ph();
      case PumpId::DRAIN:   return !levels.ph();
      default:              return false;
    }
  };

  // ---------- Paso actual ----------
  const Step& S = STEPS[idx];

  switch (S.op) {
    case Op::READ_LEVELS: {
      if (phase == Phase::ENTER) {
        const bool h2oHigh = levels.h2o();
        const bool phHigh  = levels.ph();
        const bool o2High  = levels.o2();
        const char* sH2O = h2oHigh ? "HI " : "LO ";
        const char* sPH  = phHigh  ? "HI " : "LO ";
        const char* sO2  = o2High  ? "HI " : "LO ";
        char L0[17], L1[17];
        snprintf(L0, sizeof(L0), "H2O:%s pH:%s", sH2O, sPH);
        snprintf(L1, sizeof(L1), "O2:%s", sO2);
        show(L0, L1);
        tPost = millis();
        phase = Phase::POST;
      } else if (phase == Phase::POST) {
        if (millis() - tPost >= MSG_MS) phase = Phase::EXIT;
      } else if (phase == Phase::EXIT) {
        idx++; phase = Phase::ENTER;
      }
    } break;

    case Op::PUMP_FOR: {
      // Resolver SAMPLE dinámico
      PumpId effPump = (S.pump == PumpId::SAMPLE1) ? samplePumpForCycle() : S.pump;

      if (phase == Phase::ENTER) {
        timeoutMs = computeTimeoutMs(S.dkind);   // 0 para CONST
        if (effPump == PumpId::H2O) timeoutMs = getSampleTimeoutMs();
        fillMs    = computeFillMs(effPump);
        if (fillMs == 0) fillMs = 500;           // mínimo

        pumps.on(effPump);

        pumpSub = PumpSub::WAIT_SENSOR;
        t0      = millis();
        if (timeoutMs > 0)
          progressTimeoutCountdown(S.label, timeoutMs, t0); // regresiva
        else
          show(S.label, "Esperando sens");

        phase = Phase::RUN;
      }
      else if (phase == Phase::RUN) {
        if (pumpSub == PumpSub::WAIT_SENSOR) {
          bool reached = sensorReached(effPump);
          bool to_exp  = (timeoutMs > 0) && ((millis() - t0) >= timeoutMs);

          if (reached) {
            pumpSub    = PumpSub::FILL;
            tFillStart = millis();
            progressFillUp(S.label, fillMs, tFillStart);       // ascendente
          } else if (to_exp) {
            pumps.off(effPump);
            lcd.splash("AUTO STOP", "Timeout sensor", 900);
            started = false; idx = 0; phase = Phase::ENTER; lastPHShown = NAN;
            currentSample = 0; totalSamples = 0;
            return true;
          } else {
            if (timeoutMs > 0) progressTimeoutCountdown(S.label, timeoutMs, t0); // regresiva
          }
        }
        else if (pumpSub == PumpSub::FILL) {
          progressFillUp(S.label, fillMs, tFillStart);         // ascendente
          if ((millis() - tFillStart) >= fillMs) {
            pumpSub = PumpSub::DONE;
          }
        }
        else { // DONE
          pumps.off(effPump);
          show(S.label, "OK");
          tPost = millis();
          phase = Phase::POST;
        }
      }
      else if (phase == Phase::POST) {
        if (millis() - tPost >= MSG_MS) phase = Phase::EXIT;
      }
      else if (phase == Phase::EXIT) {
        idx++; phase = Phase::ENTER;
      }
    } break;

    case Op::MIXER_ON: {
      if (phase == Phase::ENTER) {
        pumps.on(PumpId::MIXER);
        show("Mixer ON", "OK");
        tPost = millis();
        phase = Phase::POST;
      } else if (phase == Phase::POST) {
        if (millis() - tPost >= MSG_MS) phase = Phase::EXIT;
      } else if (phase == Phase::EXIT) {
        idx++; phase = Phase::ENTER;
      }
    } break;

    case Op::MIXER_OFF: {
      if (phase == Phase::ENTER) {
        pumps.off(PumpId::MIXER);
        show("Mixer OFF", "OK");
        tPost = millis();
        phase = Phase::POST;
      } else if (phase == Phase::POST) {
        if (millis() - tPost >= MSG_MS) phase = Phase::EXIT;
      } else if (phase == Phase::EXIT) {
        idx++; phase = Phase::ENTER;
      }
    } break;

    case Op::WAIT: {
      if (phase == Phase::ENTER) {
        uint32_t waitMs = (S.dkind == DurKind::CONST) ? S.ms : computeTimeoutMs(S.dkind);
        t0 = millis();
        timeoutMs = waitMs;
        phase = Phase::RUN;
        progressTimeoutCountdown(S.label, timeoutMs, t0); // regresiva
      } else if (phase == Phase::RUN) {
        progressTimeoutCountdown(S.label, timeoutMs, t0); // regresiva
        if ((millis() - t0) >= timeoutMs) {
          show(S.label, "OK");
          tPost = millis();
          phase = Phase::POST;
        }
      } else if (phase == Phase::POST) {
        if (millis() - tPost >= MSG_MS) phase = Phase::EXIT;
      } else if (phase == Phase::EXIT) {
        idx++; phase = Phase::ENTER;
      }
    } break;

    case Op::READ_PH: {
      if (phase == Phase::ENTER) {
        char L0[17], L1[17];
        snprintf(L0, sizeof(L0), "Leyendo pH");
        snprintf(L1, sizeof(L1), " ");
        show(L0, L1);

        float phv = readPH();
        lastPHShown = phv;
        float tC = readThermo();

        // Guardar en JSON del sample actual (1..4)
        uint8_t sampleId = (uint8_t)(currentSample + 1);
        if (sampleId < 1) sampleId = 1;
        if (sampleId > 4) sampleId = 4;

        uart2.setLastPh(phv);
        uart2.setSamplePhValueById(sampleId, phv);
        uart2.setSampleTempCById(sampleId, tC);

        snprintf(L0, sizeof(L0), "pH: %.02f pH", phv);
        snprintf(L1, sizeof(L1), "OK");
        show(L0, L1);

        tPost = millis();
        phase = Phase::POST;
      } else if (phase == Phase::POST) {
        if (millis() - tPost >= MSG_MS) phase = Phase::EXIT;
      } else if (phase == Phase::EXIT) {
        if (millis() - tPost >= MSG_MS) {
          idx++; phase = Phase::ENTER;
        }
      }
    } break;

    case Op::READ_O2: {
      if (phase == Phase::ENTER) {
        char L0[17], L1[17];
        snprintf(L0, sizeof(L0), "Leyendo O2");
        snprintf(L1, sizeof(L1), " ");
        show(L0, L1);

        float o2v = readO2();
        uint8_t sampleId = (uint8_t)(currentSample + 1);
        if (sampleId < 1) sampleId = 1;
        if (sampleId > 4) sampleId = 4;
        uart2.setSampleO2ValueById(sampleId, o2v);
        snprintf(L0, sizeof(L0), "O2: %.03f mg/L", o2v);
        snprintf(L1, sizeof(L1), "OK");
        show(L0, L1);

        tPost = millis();
        phase = Phase::POST;
      } else if (phase == Phase::POST) {
        if (millis() - tPost >= MSG_MS) phase = Phase::EXIT;
      } else if (phase == Phase::EXIT) {
        idx++; phase = Phase::ENTER;
      }
    } break;

    case Op::END: {
      pumps.allOff();

      // Fin de un ciclo de sample
      currentSample++;

      if (currentSample < totalSamples) {
        char l0[17], l1[17];
        snprintf(l0, sizeof(l0), "Sample %u/%u", (unsigned)currentSample, (unsigned)totalSamples);
        snprintf(l1, sizeof(l1), "Repitiendo...");
        lcd.splash(l0, l1, 800);

        // Reiniciar receta
        idx     = 0;
        phase   = Phase::ENTER;
        started = true;
        return false;
      }

      // Todo completado
      lcd.splash("AUTO OK", "Completado", 900);
      uart2.sendLastSnapshot();
      started = false; idx = 0; phase = Phase::ENTER; lastPHShown = NAN;
      currentSample = 0; totalSamples = 0;
      return true;
    }

    default: {
      pumps.allOff();
      lcd.splash("AUTO", "Error step", 800);
      started = false; idx = 0; phase = Phase::ENTER; lastPHShown = NAN;
      currentSample = 0; totalSamples = 0;
      return true;
    }
  }

  return false;
}

static bool runO2Calibration_1P(uint8_t samples = 31) {
  extern O2Manager              o2;
  extern ADS1115Manager         ads;
  extern float                  readThermo();
  extern ConfigStore            eeprom;

  enum class Step : uint8_t {
    START,
    WAIT,
    CAPT,
    APPLY,
    DONE,
    CANCEL
  };
  static Step step = Step::START;

  static float V1_mV = NAN;
  static float T1_C = NAN;

  auto title = []() { lcd.printAt(0, 0, "Calibrar O2"); };
  auto ask = [&]() { title(); lcd.printAt(0, 1, "Punto  OK"); };
  auto busy = [&](const char* m) { title(); lcd.printAt(0, 1, m); };
  auto showErr = [](const char* a, const char* b = "") { lcd.splash(a, b, 900); };

  auto insertionSort = [](float* arr, uint16_t n){
    for (uint16_t i = 1; i < n; ++i) {
      float key = arr[i];
      int j = (int)i - 1;
      while (j >= 0 && arr[j] > key) {
        arr[j+1] = arr[j];
        --j;
      }
      arr[j+1] = key;
    }
  };

  auto medianVoltADS = [&](uint16_t N) -> float {
    static constexpr uint16_t MAX_S = 128;
    if (N == 0) N = 1;
    if (N > MAX_S) N = MAX_S;

    float buf[MAX_S];
    uint16_t k = 0;

    for (uint16_t i = 0; i < N; ++i) {
      float v = NAN;
      if (ads.readSingle(1, v) && isfinite(v)) {
        buf[k++] = v;
      }
      delay(4);
    }

    if (k < 3) return NAN;

    insertionSort(buf, k);
    if (k & 1) {
      return buf[k/2];
    }
    uint16_t r = k/2;
    return 0.5f * (buf[r-1] + buf[r]);
  };

  switch (step) {
    case Step::START: {
      ads.setAveraging(samples);
      lcd.clear();
      ask();
      step = Step::WAIT;
      return false;
    }

    case Step::WAIT: {
      if (Buttons::BTN_ESC.value) { Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value)  { Buttons::BTN_OK.reset();  step = Step::CAPT; return false; }
      return false;
    }

    case Step::CAPT: {
      busy("Calibrando...");
      float v = medianVoltADS(samples);
      float tC = readThermo();
      bool tOk = isfinite(tC) && tC > -40.0f && tC < 125.0f;
      if (!tOk) tC = 25.0f;

      if (!isfinite(v)) {
        showErr("O2 cal", "Voltaje invalido");
        step = Step::DONE;
        return false;
      }

      V1_mV = v * 1000.0f;
      T1_C = tC;
      lcd.splash("Punto OK", "", 700);
      step = Step::APPLY;
      return false;
    }

    case Step::APPLY: {
      o2.setSinglePointCalibration(V1_mV, T1_C);
      eeprom.setO2Cal(V1_mV, T1_C, NAN, NAN);
      if (!eeprom.save()) {
        remoteManager.log(String("EEPROM save fallo: ") + eeprom.lastError());
        showErr("EEPROM", "Save fallo");
        step = Step::DONE;
        return false;
      }

      remoteManager.log(String("O2 cal 1p aplicada: V=") + String(V1_mV, 1) +
                        "mV T=" + String(T1_C, 1) + "C");
      char l2[17];
      snprintf(l2, sizeof(l2), "V=%.0f T=%.1f", V1_mV, T1_C);
      lcd.splash("O2 calibrado", l2, 800);

      step = Step::DONE;
      return false;
    }

    case Step::CANCEL:
      remoteManager.log("O2 cal 1p: CANCEL");
      showErr("Calibracion", "Cancelada");
      step = Step::DONE;
      return false;

    case Step::DONE:
    default:
      step = Step::START;
      return true;
  }
  return false;
}

static bool runO2Calibration_2P(uint8_t samples = 31) {
  extern O2Manager              o2;
  extern ADS1115Manager         ads;
  extern float                  readThermo();
  extern ConfigStore            eeprom;

  enum class Step : uint8_t {
    START,
    WAIT_HIGH,
    CAPT_HIGH,
    WAIT_LOW,
    CAPT_LOW,
    APPLY,
    DONE,
    CANCEL
  };
  static Step step = Step::START;

  static float V1_mV = NAN, V2_mV = NAN;
  static float T1_C = NAN, T2_C = NAN;

  auto title = []() { lcd.printAt(0, 0, "Calibrar O2"); };
  auto askHigh = [&]() { title(); lcd.printAt(0, 1, "Alta T   OK"); };
  auto askLow = [&]() { title(); lcd.printAt(0, 1, "Baja T   OK"); };
  auto busy = [&](const char* m) { title(); lcd.printAt(0, 1, m); };
  auto showErr = [](const char* a, const char* b = "") { lcd.splash(a, b, 900); };

  auto insertionSort = [](float* arr, uint16_t n){
    for (uint16_t i = 1; i < n; ++i) {
      float key = arr[i];
      int j = (int)i - 1;
      while (j >= 0 && arr[j] > key) {
        arr[j+1] = arr[j];
        --j;
      }
      arr[j+1] = key;
    }
  };

  auto medianVoltADS = [&](uint16_t N) -> float {
    static constexpr uint16_t MAX_S = 128;
    if (N == 0) N = 1;
    if (N > MAX_S) N = MAX_S;

    float buf[MAX_S];
    uint16_t k = 0;

    for (uint16_t i = 0; i < N; ++i) {
      float v = NAN;
      if (ads.readSingle(1, v) && isfinite(v)) {
        buf[k++] = v;
      }
      delay(4);
    }

    if (k < 3) return NAN;

    insertionSort(buf, k);
    if (k & 1) {
      return buf[k/2];
    }
    uint16_t r = k/2;
    return 0.5f * (buf[r-1] + buf[r]);
  };

  switch (step) {
    case Step::START: {
      ads.setAveraging(samples);
      lcd.clear();
      askHigh();
      step = Step::WAIT_HIGH;
      return false;
    }

    case Step::WAIT_HIGH: {
      if (Buttons::BTN_ESC.value) { Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value)  { Buttons::BTN_OK.reset();  step = Step::CAPT_HIGH; return false; }
      return false;
    }

    case Step::CAPT_HIGH: {
      busy("Calibrando...");
      float v = medianVoltADS(samples);
      float tC = readThermo();
      bool tOk = isfinite(tC) && tC > -40.0f && tC < 125.0f;
      if (!tOk) tC = 25.0f;

      if (!isfinite(v)) {
        showErr("O2 cal", "Voltaje invalido");
        step = Step::DONE;
        return false;
      }

      V1_mV = v * 1000.0f;
      T1_C = tC;
      lcd.splash("Punto Alta OK", "", 700);
      step = Step::WAIT_LOW;
      return false;
    }

    case Step::WAIT_LOW: {
      askLow();
      if (Buttons::BTN_ESC.value) { Buttons::BTN_ESC.reset(); step = Step::CANCEL; return false; }
      if (Buttons::BTN_OK.value)  { Buttons::BTN_OK.reset();  step = Step::CAPT_LOW; return false; }
      return false;
    }

    case Step::CAPT_LOW: {
      busy("Calibrando...");
      float v = medianVoltADS(samples);
      float tC = readThermo();
      bool tOk = isfinite(tC) && tC > -40.0f && tC < 125.0f;
      if (!tOk) tC = 25.0f;

      if (!isfinite(v)) {
        showErr("O2 cal", "Voltaje invalido");
        step = Step::DONE;
        return false;
      }

      V2_mV = v * 1000.0f;
      T2_C = tC;
      lcd.splash("Punto Baja OK", "", 700);
      step = Step::APPLY;
      return false;
    }

    case Step::APPLY: {
      o2.setTwoPointCalibration(V1_mV, T1_C, V2_mV, T2_C);
      eeprom.setO2Cal(V1_mV, T1_C, V2_mV, T2_C);
      if (!eeprom.save()) {
        remoteManager.log(String("EEPROM save fallo: ") + eeprom.lastError());
        showErr("EEPROM", "Save fallo");
        step = Step::DONE;
        return false;
      }

      remoteManager.log(String("O2 cal aplicada: V1=") + String(V1_mV, 1) +
                        "mV T1=" + String(T1_C, 1) + "C V2=" +
                        String(V2_mV, 1) + "mV T2=" + String(T2_C, 1) + "C");
      char l2[17];
      snprintf(l2, sizeof(l2), "V1=%.0f V2=%.0f", V1_mV, V2_mV);
      lcd.splash("O2 calibrado", l2, 800);

      step = Step::DONE;
      return false;
    }

    case Step::CANCEL:
      remoteManager.log("O2 cal: CANCEL");
      showErr("Calibracion", "Cancelada");
      step = Step::DONE;
      return false;

    case Step::DONE:
    default:
      step = Step::START;
      return true;
  }
}

static void MenuDemoTick() {
  // --- prototipos externos que usa el menú ---
  extern ConfigStore eeprom;
  extern bool  runADSCalibration_0V_3p31V(uint8_t, uint8_t);
  extern bool  runPHCalibration_7_4(uint8_t samples);
  extern bool  runPHCalibration_4_7_10(uint16_t samples, bool piecewise);
  extern bool  runO2Calibration_1P(uint8_t samples);
  extern bool  runO2Calibration_2P(uint8_t samples);
  extern float readADC();
  extern float readPH();
  extern float readO2();
  extern float readThermo();
  extern bool  AutoModeTick();
  extern UartProto::UARTManager uart2;

  // Subpantallas externas
  extern bool ManualPumpsTick();
  extern bool ManualLevelsTick();

  // Wizards de configuración
  extern bool PumpTimeoutsWizard();
  extern bool PumpFillLearnWizard();

  // ----- Estado persistente -----
  enum class View : uint8_t {
    ROOT,
    MANUAL_MENU,
    MANUAL_PUMPS,
    MANUAL_LEVELS,
    AUTO,
    CONFIG,
    TEMP,
    CAL_ADS_MENU,
    CAL_ADS_READ,
    CAL_ADS_RUN,
    CAL_PH_MENU,
    CAL_PH_READ,        // <-- conservado
    CAL_PH_RUN_2P,      // <-- calibración 2 puntos
    CAL_PH_RUN_3P_PW,   // <-- calibración 3 puntos (piecewise)
    CAL_O2_MENU,
    CAL_O2_READ,
    CAL_O2_RUN_1P,
    CAL_O2_RUN_2P,
    CFG_TIMEOUTS,
    CFG_FILL
  };

  static bool init = false;
  static View view = View::ROOT;
  static bool autoFirst = true;

  // ----- Menús -----
  static const char *rootItems[] = {"Manual", "Automatico", "Configuracion"};
  static uint8_t rootCursor = 0;
  const uint8_t ROOT_N = sizeof(rootItems) / sizeof(rootItems[0]);

  static const char *manualItems[] = {"Bombas", "Sensores Nivel"};
  static uint8_t manualCursor = 0;
  const uint8_t MAN_N = sizeof(manualItems) / sizeof(manualItems[0]);

  static const char *cfgItems[] = {"pH", "O2", "Temperatura", "ADC", "Timeouts", "Fill (aprender)"};
  static uint8_t cfgCursor = 0;
  const uint8_t CFG_N = sizeof(cfgItems) / sizeof(cfgItems[0]);

  static const char *adsItems[] = {"Leer ADC", "Calibrar"};
  static uint8_t adsCursor = 0;
  const uint8_t ADS_N = sizeof(adsItems) / sizeof(adsItems[0]);

  // ---- Submenú pH: leer + 2 calibraciones ----
  static const char *phItems[] = {
    "Leer pH",
    "Cal 7-4 (2p)",
    "Cal 4-7-10 (PW)"
  };
  static uint8_t phCursor = 0;
  const uint8_t PH_N = sizeof(phItems) / sizeof(phItems[0]);

  // ---- Submenú O2: leer + calibración 2 puntos ----
  static const char *o2Items[] = {
    "Leer O2",
    "Cal O2 (1p)",
    "Cal O2 (2p)"
  };
  static uint8_t o2Cursor = 0;
  const uint8_t O2_N = sizeof(o2Items) / sizeof(o2Items[0]);

  // ---- Temperatura ----
  static int8_t tempOffset = 0;
  static float lastTemp = -1.0f;
  static unsigned long lastReadMs = 0;
  const unsigned long TEMP_READ_PERIOD = 500; // ms

  // ---- UI helpers ----
  auto renderRoot = [&]() {
    lcd.printAt(0, 0, "Menu");
    String line = ">" + String(rootItems[rootCursor]);
    lcd.printAt(0, 1, line);
  };
  auto renderManualMenu = [&]() {
    lcd.printAt(0, 0, "Manual");
    lcd.printAt(0, 1, ">" + String(manualItems[manualCursor]));
  };
  auto renderConfig = [&]() {
    lcd.printAt(0, 0, "Configuracion");
    lcd.printAt(0, 1, ">" + String(cfgItems[cfgCursor]));
  };
  auto renderTemp = [&]() {
    String l0 = "T:";
    if (lastTemp >= 0.0f) l0 += " " + String(lastTemp, 1) + " C";
    else l0 += " --.- C";
    lcd.printAt(0, 0, l0);
    char buf[17];
    snprintf(buf, sizeof(buf), "Off:%+d", (int)tempOffset);
    lcd.printAt(0, 1, buf);
  };
  auto renderADSMenu = [&]() {
    lcd.printAt(0, 0, "Calibrar ADC");
    lcd.printAt(0, 1, ">" + String(adsItems[adsCursor]));
  };
  auto renderPHMenu = [&]() {
    lcd.printAt(0, 0, "Calibrar pH");
    lcd.printAt(0, 1, ">" + String(phItems[phCursor]));
  };
  auto renderO2Menu = [&]() {
    lcd.printAt(0, 0, "Calibrar O2");
    lcd.printAt(0, 1, ">" + String(o2Items[o2Cursor]));
  };
  auto renderADSRead = [&]() {
    float v = readADC();
    float m, b; eeprom.getADC(m, b);
    char l0[17]; snprintf(l0, sizeof(l0), "ADC: %.3f V", v);
    char l1[17]; snprintf(l1, sizeof(l1), "m%.3f b%.3f", m, b);
    lcd.printAt(0, 0, l0);
    lcd.printAt(0, 1, l1);
  };
  auto renderPHRead = [&]() {
    float phv = readPH();
    char l0[17]; snprintf(l0, sizeof(l0), "pH: %.02f", phv);
    lcd.printAt(0, 0, l0);
    lcd.printAt(0, 1, "OK refrescar");
  };
  auto renderO2Read = [&]() {
    lcd.printAt(0, 0, "Leyendo");
    lcd.printAt(0, 1, " ");
    float o2v = readO2();
    char l0[17]; snprintf(l0, sizeof(l0), "O2: %.03f", o2v);
    lcd.printAt(0, 0, l0);
    lcd.printAt(0, 1, "OK refrescar");
  };

  // ---- Init ----
  if (!init) {
    lcd.clear();
    renderRoot();
    init = true;
    remoteManager.log("[MENU] Init -> ROOT");
  }

  // ---- Auto mode trigger por UART ----
  static bool prevAutoReq = false;
  bool autoReq = uart2.getAutoMeasureRequested();
  bool autoRun = uart2.getAutoRunning();
  if (view == View::ROOT) {
    if ((autoReq && !prevAutoReq) || (autoReq && !autoRun)) {
      view = View::AUTO; autoFirst = true;
      remoteManager.log("[MENU] DISPARO AUTO desde ROOT");
    }
  }
  prevAutoReq = autoReq;

  // ---- Botones ----
  Buttons::testButtons();
  enum class Btn : uint8_t { NONE, OK, ESC, UP, DOWN };
  auto readLatched = []() -> Btn {
    if (Buttons::BTN_OK.value)   { Buttons::BTN_OK.reset();   return Btn::OK; }
    if (Buttons::BTN_ESC.value)  { Buttons::BTN_ESC.reset();  return Btn::ESC; }
    if (Buttons::BTN_UP.value)   { Buttons::BTN_UP.reset();   return Btn::UP; }
    if (Buttons::BTN_DOWN.value) { Buttons::BTN_DOWN.reset(); return Btn::DOWN; }
    return Btn::NONE;
  };

  // ---- Refresco temperatura ----
  if (view == View::TEMP) {
    unsigned long now = millis();
    if (now - lastReadMs >= TEMP_READ_PERIOD) {
      lastReadMs = now;
      float t = readThermo();
      lastTemp = (t >= 0.0f && !isnan(t)) ? t : -1.0f;
      renderTemp();
    }
  }

  // ---- Estado de calibración (bloquea auto_measure por UART) ----
  const bool calibBusy =
    (view == View::CAL_ADS_MENU) ||
    (view == View::CAL_ADS_READ) ||
    (view == View::CAL_ADS_RUN)  ||
    (view == View::CAL_PH_MENU)  ||
    (view == View::CAL_PH_READ)  ||
    (view == View::CAL_PH_RUN_2P) ||
    (view == View::CAL_PH_RUN_3P_PW) ||
    (view == View::CAL_O2_MENU)  ||
    (view == View::CAL_O2_READ)  ||
    (view == View::CAL_O2_RUN_1P) ||
    (view == View::CAL_O2_RUN_2P);
  uart2.setBusy(calibBusy);

  // ---- FSM ----
  switch (view) {
  // ---------- ROOT ----------
  case View::ROOT: {
    switch (readLatched()) {
      case Btn::UP:
        rootCursor = (rootCursor == 0) ? 2 : rootCursor - 1;
        lcd.clear(); renderRoot(); break;
      case Btn::DOWN:
        rootCursor = (rootCursor + 1) % 3;
        lcd.clear(); renderRoot(); break;
      case Btn::OK:
        if (rootCursor == 0) { view = View::MANUAL_MENU; lcd.clear(); renderManualMenu(); }
        else if (rootCursor == 1) { view = View::AUTO; autoFirst = true; lcd.clear(); }
        else { view = View::CONFIG; lcd.clear(); renderConfig(); }
        break;
      default: break;
    }
  } break;

  // ---------- MANUAL ----------
  case View::MANUAL_MENU: {
    switch (readLatched()) {
      case Btn::UP:
        manualCursor = (manualCursor == 0) ? 1 : manualCursor - 1;
        lcd.clear(); renderManualMenu(); break;
      case Btn::DOWN:
        manualCursor = (manualCursor + 1) % 2;
        lcd.clear(); renderManualMenu(); break;
      case Btn::OK:
        lcd.clear();
        view = (manualCursor == 0) ? View::MANUAL_PUMPS : View::MANUAL_LEVELS;
        break;
      case Btn::ESC:
        view = View::ROOT; lcd.clear(); renderRoot(); break;
      default: break;
    }
  } break;

  case View::MANUAL_PUMPS:
    if (ManualPumpsTick()) { view = View::MANUAL_MENU; lcd.clear(); renderManualMenu(); }
    break;
  case View::MANUAL_LEVELS:
    if (ManualLevelsTick()) { view = View::MANUAL_MENU; lcd.clear(); renderManualMenu(); }
    break;

  // ---------- AUTO ----------
  case View::AUTO:
    if (autoFirst) { lcd.clear(); autoFirst = false; }
    uart2.setAutoRunning(true);
    if (AutoModeTick()) {
      view = View::ROOT; lcd.clear(); renderRoot();
      uart2.setAutoRunning(false);
      uart2.setAutoMeasureRequested(false);
    }
    break;

  // ---------- CONFIG ----------
  case View::CONFIG: {
    switch (readLatched()) {
      case Btn::UP:
        cfgCursor = (cfgCursor == 0) ? (CFG_N - 1) : cfgCursor - 1;
        lcd.clear(); renderConfig(); break;
      case Btn::DOWN:
        cfgCursor = (cfgCursor + 1) % CFG_N;
        lcd.clear(); renderConfig(); break;
      case Btn::OK:
        lcd.clear();
        if (cfgCursor == 0) view = View::CAL_PH_MENU, renderPHMenu();
        else if (cfgCursor == 1) view = View::CAL_O2_MENU, renderO2Menu();
        else if (cfgCursor == 2) view = View::TEMP, renderTemp();
        else if (cfgCursor == 3) view = View::CAL_ADS_MENU, renderADSMenu();
        else if (cfgCursor == 4) view = View::CFG_TIMEOUTS;
        else view = View::CFG_FILL;
        break;
      case Btn::ESC:
        view = View::ROOT; lcd.clear(); renderRoot(); break;
      default: break;
    }
  } break;

  // ---------- CALIBRACIÓN PH ----------
  case View::CAL_PH_MENU: {
    switch (readLatched()) {
      case Btn::UP:
        phCursor = (phCursor == 0) ? (PH_N - 1) : phCursor - 1;
        lcd.clear(); renderPHMenu(); break;
      case Btn::DOWN:
        phCursor = (phCursor + 1) % PH_N;
        lcd.clear(); renderPHMenu(); break;
      case Btn::OK:
        if (phCursor == 0) view = View::CAL_PH_READ, lcd.clear(), renderPHRead();
        else if (phCursor == 1) view = View::CAL_PH_RUN_2P, lcd.clear();
        else view = View::CAL_PH_RUN_3P_PW, lcd.clear();
        break;
      case Btn::ESC:
        view = View::CONFIG; lcd.clear(); renderConfig(); break;
      default: break;
    }
  } break;

  case View::CAL_PH_READ: {
    switch (readLatched()) {
      case Btn::OK: lcd.clear(); renderPHRead(); break;
      case Btn::ESC: view = View::CAL_PH_MENU; lcd.clear(); renderPHMenu(); break;
      default: break;
    }
  } break;

  case View::CAL_PH_RUN_2P:
    if (runPHCalibration_7_4(31)) {
      view = View::CAL_PH_MENU; lcd.clear(); renderPHMenu();
    }
    break;

  case View::CAL_PH_RUN_3P_PW:
    if (runPHCalibration_4_7_10(31, false)) {
      view = View::CAL_PH_MENU; lcd.clear(); renderPHMenu();
    }
    break;

  // ---------- CALIBRACION O2 ----------
  case View::CAL_O2_MENU: {
    switch (readLatched()) {
      case Btn::UP:
        o2Cursor = (o2Cursor == 0) ? (O2_N - 1) : o2Cursor - 1;
        lcd.clear(); renderO2Menu(); break;
      case Btn::DOWN:
        o2Cursor = (o2Cursor + 1) % O2_N;
        lcd.clear(); renderO2Menu(); break;
      case Btn::OK:
        if (o2Cursor == 0) view = View::CAL_O2_READ, lcd.clear(), renderO2Read();
        else if (o2Cursor == 1) view = View::CAL_O2_RUN_1P, lcd.clear();
        else view = View::CAL_O2_RUN_2P, lcd.clear();
        break;
      case Btn::ESC:
        view = View::CONFIG; lcd.clear(); renderConfig(); break;
      default: break;
    }
  } break;

  case View::CAL_O2_READ: {
    switch (readLatched()) {
      case Btn::OK: lcd.clear(); renderO2Read(); break;
      case Btn::ESC: view = View::CAL_O2_MENU; lcd.clear(); renderO2Menu(); break;
      default: break;
    }
  } break;

  case View::CAL_O2_RUN_1P:
    if (runO2Calibration_1P(31)) {
      view = View::CAL_O2_MENU; lcd.clear(); renderO2Menu();
    }
    break;

  case View::CAL_O2_RUN_2P:
    if (runO2Calibration_2P(31)) {
      view = View::CAL_O2_MENU; lcd.clear(); renderO2Menu();
    }
    break;

  // ---------- WIZARDS ----------
  case View::CFG_TIMEOUTS:
    if (PumpTimeoutsWizard()) { view = View::CONFIG; lcd.clear(); renderConfig(); }
    break;
  case View::CFG_FILL:
    if (PumpFillLearnWizard()) { view = View::CONFIG; lcd.clear(); renderConfig(); }
    break;

  // ---------- TEMP ----------
  case View::TEMP: {
    switch (readLatched()) {
      case Btn::UP: if (tempOffset < 10) ++tempOffset; lcd.clear(); renderTemp(); break;
      case Btn::DOWN: if (tempOffset > -10) --tempOffset; lcd.clear(); renderTemp(); break;
      case Btn::OK: lcd.splash("Temp offset", "Guardado", 600); view = View::CONFIG; lcd.clear(); renderConfig(); break;
      case Btn::ESC: view = View::CONFIG; lcd.clear(); renderConfig(); break;
      default: break;
    }
  } break;
  }
}
