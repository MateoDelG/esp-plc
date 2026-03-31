#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Preferences.h>
#include <ctype.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "UltrasonicA02YYUW.h"
#include "DS18B20_manager.h"
#include "Globals.h"
#include "Storage.h"
#include "I2CSlaveManager.h"



// ----------------------------
// Pines
// ----------------------------
#define PIN_LEVEL_MIN   6   // salida nivel bajo
#define PIN_LEVEL_MAX   7   // salida nivel alto

// Bits de estado del sensor (los mismos que usaste en la librería)
#define ERR_CHECKSUM      0x01  // bit0
#define ERR_OUT_OF_RANGE  0x02  // bit1
#define ERR_NO_DATA       0x04  // bit2
#define ERR_FILTER_NAN    0x08  // bit3
#define ERR_PKT_FORMAT    0x10  // bit4

static uint8_t g_lastSensorStatus = 0xFF;  // valor imposible inicial para forzar primer print


// Sensor UART
#define RX_PIN 3
#define TX_PIN 4
UltrasonicA02YYUW sensor(Serial1, RX_PIN, TX_PIN);
// Sensor temperatura DS18B20
#define TEMP_SENSOR_PIN 10
DS18B20Manager thermo(TEMP_SENSOR_PIN, 12);


// Estado de configuración actualmente aplicada
static float g_kalMeaApplied = 3.0f;
static float g_kalEstApplied = 8.0f;
static float g_kalQApplied   = 0.08f;
static int   g_samplePeriodApplied = 1000;   // ms
static float g_zeroOffsetApplied = 0.0f;

static float g_minLevelApplied = 0.0f;
static float g_maxLevelApplied = 100.0f;
static uint8_t g_i2cAddressApplied = 0x30;

// Tiempo para muestreo
static unsigned long g_lastSampleMs = 0;

// RTOS
static SemaphoreHandle_t g_globalsMutex = nullptr;
static const uint32_t SENSOR_PERIOD_MS = 1000;
static const uint32_t CONTROL_PERIOD_MS = 1000;
static const uint32_t I2C_PERIOD_MS = 1000;
static const uint32_t LOG_HEARTBEAT_MS = 5000;
static const uint32_t LOG_POLL_MS = 200;

static bool g_i2cStarted = false;
static bool g_espNowInitialized = false;
static uint32_t g_espNowSeq = 0;
static uint8_t g_espNowChannel = 6;
static uint8_t g_espNowNextChannel = 6;
static bool g_espNowChannelPending = false;
static Preferences g_espNowPrefs;

static WebServer g_webServer(80);
static WebSocketsServer g_wsServer(81);
static bool g_webPortalStarted = false;
static String g_apName;
static String g_staMac;
static uint32_t g_lastWsSendMs = 0;

// Prototipos
void setupOutputs();
void setupDistance();
void setupSlaveI2C();
void i2cUpdate();
void readDistance();
void updateLevelOutputs();
void monitorConfigChanges();
void printSensorStatusIfChanged();
void initThermo();
float readThermo();
float getLevelCm();
void taskSensors(void *pvParameters);
void taskControl(void *pvParameters);
void taskI2C(void *pvParameters);
void taskStorage(void *pvParameters);
void taskLogger(void *pvParameters);
void taskEspNow(void *pvParameters);
void taskWebPortal(void *pvParameters);
void logHeartbeat();
String macSuffix();
String formatMac(const uint8_t *mac);
bool isMacValid(const uint8_t *mac);
bool parseMacString(const String &input, uint8_t mac[6]);
String getPeerMacString();
void startWebPortal();
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len);
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status);
bool ensureEspNowInitialized();
bool configureEspNowPeer(const uint8_t *mac);
bool ensurePeerForMac(const uint8_t *mac);
bool isValidChannel(uint8_t ch);
void requestEspNowChannel(uint8_t ch);
void sendEspNowText(const uint8_t *dest, const char *text);
void sendEspNowStatus(const uint8_t *dest);
void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void sendWsStatus();

static void lockGlobals() {
  if (g_globalsMutex) {
    xSemaphoreTake(g_globalsMutex, portMAX_DELAY);
  }
}

static void unlockGlobals() {
  if (g_globalsMutex) {
    xSemaphoreGive(g_globalsMutex);
  }
}



void setup() {
  Serial.begin(115200);
  delay(5000);

  g_globalsMutex = xSemaphoreCreateMutex();

  Storage::begin();

  WiFi.mode(WIFI_AP_STA);

  {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primary, &second);
    Serial.printf("[RX] WiFi.channel=%u, esp_wifi_get_channel=%u\n", WiFi.channel(), primary);
    esp_wifi_set_ps(WIFI_PS_NONE);
    Serial.println("[RX] PS: WIFI_PS_NONE");
  }

  g_espNowPrefs.begin("espnow", false);
  g_espNowChannel = g_espNowPrefs.getUChar("ch", 6);
  g_espNowNextChannel = g_espNowChannel;

  ConfigData cfg;
  if (Storage::loadConfig(cfg)) {
    lockGlobals();
    Globals::setMinLevel(cfg.minLevel);
    Globals::setMaxLevel(cfg.maxLevel);
    Globals::setSamplePeriod(cfg.samplePeriod);
    Globals::setKalMea(cfg.kalMea);
    Globals::setKalEst(cfg.kalEst);
    Globals::setKalQ(cfg.kalQ);
    Globals::setI2CAddress(cfg.i2cAddress);
    Globals::setEspNowPeerMac(cfg.espNowPeerMac);
    Globals::setZeroOffset(cfg.zeroOffsetCm);
    Globals::setEspNowEnabled(true);
    unlockGlobals();
    Serial.printf("[ESPNOW] Enabled: %d\n", 1);
    Serial.printf("[ESPNOW] Peer MAC guardada: %s\n", formatMac(cfg.espNowPeerMac).c_str());
  } else {
    lockGlobals();
    Globals::setEspNowEnabled(true);
    unlockGlobals();
    Serial.printf("[ESPNOW] Enabled: %d\n", 1);
    Serial.println("[ESPNOW] Peer MAC guardada: --");
  }

  // Salidas
  setupOutputs();
  // Inicializar sensor
  setupDistance();
  // Inicializar sensor temperatura
  initThermo();

  xTaskCreate(taskSensors, "TaskSensors", 4096, nullptr, 3, nullptr);
  xTaskCreate(taskControl, "TaskControl", 3072, nullptr, 2, nullptr);
  xTaskCreate(taskI2C, "TaskI2C", 3072, nullptr, 2, nullptr);
  xTaskCreate(taskStorage, "TaskStorage", 4096, nullptr, 1, nullptr);
  xTaskCreate(taskLogger, "TaskLogger", 4096, nullptr, 1, nullptr);
  xTaskCreate(taskEspNow, "TaskEspNow", 4096, nullptr, 1, nullptr);
  xTaskCreate(taskWebPortal, "TaskWebPortal", 6144, nullptr, 1, nullptr);

  Serial.println("\n[MAIN] Sistema iniciado (FreeRTOS).");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ------------------------------------------
// Configuración de salidas digitales
// ------------------------------------------
void setupOutputs() {
  pinMode(PIN_LEVEL_MIN, OUTPUT);
  pinMode(PIN_LEVEL_MAX, OUTPUT);
  digitalWrite(PIN_LEVEL_MIN, LOW);
  digitalWrite(PIN_LEVEL_MAX, LOW);
}
// ------------------------------------------
// Configuración del esclavo I2C
// ------------------------------------------
void setupSlaveI2C() {
  uint8_t addr = 0x30;
  lockGlobals();
  addr = Globals::getI2CAddress();
  unlockGlobals();
  I2CSlaveManager::begin(addr);
  Serial.println("[MAIN] Dispositivo iniciado como esclavo I2C");
}

void i2cUpdate() {
  float fr = NAN;
  float rr = NAN;
  lockGlobals();
  fr = Globals::getDistanceFiltered();
  rr = Globals::getDistanceRaw();
  unlockGlobals();

  I2CSlaveManager::setFilteredDistance(fr);
  I2CSlaveManager::setRawDistance(rr);

  // debug opcional:
  Serial.printf("[I2CUpdate] Filt=%.2f  Raw=%.2f\n", fr, rr);
}

// ------------------------------------------
// Configuración inicial del sensor y Kalman
// ------------------------------------------
void setupDistance() {
  sensor.begin(9600);

  // Leer valores iniciales desde Globals (por si ya vienen de EEPROM)
  g_kalMeaApplied       = Globals::getKalMea();
  g_kalEstApplied       = Globals::getKalEst();
  g_kalQApplied         = Globals::getKalQ();
  g_samplePeriodApplied = Globals::getSamplePeriod();
  g_minLevelApplied     = Globals::getMinLevel();
  g_maxLevelApplied     = Globals::getMaxLevel();
  g_zeroOffsetApplied   = Globals::getZeroOffset();

  if (g_samplePeriodApplied <= 0) g_samplePeriodApplied = 1000;

  // Aplicar a sensor
  sensor.setMeasurementError(g_kalMeaApplied);
  sensor.setEstimateError(g_kalEstApplied);
  sensor.setProcessNoise(g_kalQApplied);

  Serial.println("[MAIN] Sensor A02 listo.");
  Serial.printf("[MAIN] Kalman: mea=%.3f est=%.3f q=%.4f, sample=%d ms\n",
                g_kalMeaApplied, g_kalEstApplied, g_kalQApplied, g_samplePeriodApplied);
}

// ------------------------------------------
// Lectura del sensor + envío a Globals
// ------------------------------------------
void readDistance() {
  sensor.update();

  if (sensor.isValid()) {
    float raw  = sensor.getDistanceRaw();
    float filt = sensor.getDistance();

    lockGlobals();
    Globals::setDistanceRaw(raw);
    Globals::setDistanceFiltered(filt);
    unlockGlobals();

    // Debug si quieres
    // Serial.printf(">Crudo: %.2f  | Filtrado: %.2f\n", raw, filt);
  }
}

void initThermo() {
  uint8_t n = thermo.begin();
  Serial.println("DS18B20 encontrados: " + String(n));
}

float readThermo() {
  if (thermo.sensorCount() == 0) {
    return NAN;
  }

  if (!thermo.isPending()) {
    thermo.startConversion(0);
    return NAN;
  }

  if (!thermo.isReady()) {
    return NAN;
  }

  float c = thermo.readCAsync(0);
  if (isnan(c)) return NAN;

  lockGlobals();
  Globals::setTemperature(c);
  unlockGlobals();
  return c;
}

float getLevelCm() {
  float filt = NAN;
  float offset = 0.0f;
  lockGlobals();
  filt = Globals::getDistanceFiltered();
  offset = Globals::getZeroOffset();
  unlockGlobals();

  if (isnan(filt)) return NAN;
  float level = offset - filt;
  if (level < 0.0f) level = 0.0f;
  if (level > offset) level = offset;
  return level;
}


// ------------------------------------------
// Activar salidas según niveles configurados
// ------------------------------------------
void updateLevelOutputs() {
  float d = getLevelCm();
  if (isnan(d)) return;

  float minLevel = 0.0f;
  float maxLevel = 0.0f;
  lockGlobals();
  minLevel = Globals::getMinLevel();
  maxLevel = Globals::getMaxLevel();
  unlockGlobals();

  // Nivel mínimo superado (distancia pequeña → tanque lleno)
  digitalWrite(PIN_LEVEL_MIN, d <= minLevel ? HIGH : LOW);

  // Nivel máximo superado (distancia grande → tanque vacío)
  digitalWrite(PIN_LEVEL_MAX, d >= maxLevel ? HIGH : LOW);
}

// ------------------------------------------
// Monitorea Globals y aplica cambios al sensor
// ------------------------------------------
void monitorConfigChanges() {
  bool changed = false;

  float km = 0.0f;
  float ke = 0.0f;
  float kq = 0.0f;
  int sp = 0;
  float minL = 0.0f;
  float maxL = 0.0f;
  uint8_t ia = 0;
  float zero = 0.0f;

  lockGlobals();
  km = Globals::getKalMea();
  ke = Globals::getKalEst();
  kq = Globals::getKalQ();
  sp = Globals::getSamplePeriod();
  minL = Globals::getMinLevel();
  maxL = Globals::getMaxLevel();
  ia = Globals::getI2CAddress();
  zero = Globals::getZeroOffset();
  unlockGlobals();

  // ------------------ Kalman: measurement error ------------------
  if (km != g_kalMeaApplied) {
    g_kalMeaApplied = km;
    sensor.setMeasurementError(km);
    Serial.printf("[CFG] Nuevo kalMea = %.3f\n", km);
    changed = true;
  }

  // ------------------ Kalman: estimate error ------------------
  if (ke != g_kalEstApplied) {
    g_kalEstApplied = ke;
    sensor.setEstimateError(ke);
    Serial.printf("[CFG] Nuevo kalEst = %.3f\n", ke);
    changed = true;
  }

  // ------------------ Kalman: process noise ------------------
  if (kq != g_kalQApplied) {
    g_kalQApplied = kq;
    sensor.setProcessNoise(kq);
    Serial.printf("[CFG] Nuevo kalQ = %.4f\n", kq);
    changed = true;
  }

  // ------------------ Periodo de muestreo ------------------
  if (sp != g_samplePeriodApplied && sp > 0) {
    g_samplePeriodApplied = sp;
    Serial.printf("[CFG] Nuevo samplePeriod = %d ms\n", sp);
    changed = true;
  }

  // ------------------ Niveles ------------------
  if (minL != g_minLevelApplied) {
    g_minLevelApplied = minL;
    Serial.printf("[CFG] Nuevo nivel MIN = %.2f cm\n", minL);
    changed = true;
  }

  if (maxL != g_maxLevelApplied) {
    g_maxLevelApplied = maxL;
    Serial.printf("[CFG] Nuevo nivel MAX = %.2f cm\n", maxL);
    changed = true;
  }

  // ------------------ Dirección I2C ------------------
  if (ia != g_i2cAddressApplied) {
    g_i2cAddressApplied = ia;
    Serial.printf("[CFG] Nueva dirección I2C = %u (0x%02X)\n", ia, ia);
    changed = true;
  }

  // ------------------ Offset cero ------------------
  if (zero != g_zeroOffsetApplied) {
    g_zeroOffsetApplied = zero;
    Serial.printf("[CFG] Nuevo offset 0 cm = %.2f\n", zero);
    changed = true;
  }

  // ------------------ Guardar en NVS si algo cambió ------------------
  if (changed) {
    ConfigData cfg;
    lockGlobals();
    cfg.minLevel     = Globals::getMinLevel();
    cfg.maxLevel     = Globals::getMaxLevel();
    cfg.samplePeriod = Globals::getSamplePeriod();
    cfg.kalMea       = Globals::getKalMea();
    cfg.kalEst       = Globals::getKalEst();
    cfg.kalQ         = Globals::getKalQ();
    cfg.i2cAddress   = Globals::getI2CAddress();
    Globals::getEspNowPeerMac(cfg.espNowPeerMac);
    cfg.espNowEnabled = Globals::isEspNowEnabled();
    cfg.zeroOffsetCm = Globals::getZeroOffset();
    unlockGlobals();

    Storage::saveConfig(cfg);
    Serial.println("[CFG] Config guardada en NVS");
  }
}

void printSensorStatusIfChanged() {
  uint8_t st = 0;
  lockGlobals();
  st = Globals::getSensorStatus();
  unlockGlobals();

  // Solo imprime si cambió el estado
  if (st == g_lastSensorStatus) return;
  g_lastSensorStatus = st;

  Serial.print("[SENSOR] Status = 0x");
  Serial.printf("%02X -> ", st);

  if (st == 0) {
    Serial.println("OK (sin errores)");
    return;
  }

  // Decodificar bits
  if (st & ERR_CHECKSUM)     Serial.print("CHECKSUM_ERROR ");
  if (st & ERR_OUT_OF_RANGE) Serial.print("OUT_OF_RANGE ");
  if (st & ERR_NO_DATA)      Serial.print("NO_DATA ");
  if (st & ERR_FILTER_NAN)   Serial.print("FILTER_NAN ");
  if (st & ERR_PKT_FORMAT)   Serial.print("PACKET_FORMAT_ERROR ");

  Serial.println();
}

void logHeartbeat() {
  float raw = NAN;
  float filt = NAN;
  float temp = NAN;
  uint8_t status = 0;
  bool i2cEnabled = false;

  lockGlobals();
  raw = Globals::getDistanceRaw();
  filt = Globals::getDistanceFiltered();
  temp = Globals::getTemperature();
  status = Globals::getSensorStatus();
  i2cEnabled = Globals::isI2CEnabled();
  unlockGlobals();

  Serial.printf("[HB] raw=%.2f filt=%.2f temp=%.2f status=0x%02X i2c=%d\n",
                raw, filt, temp, status, i2cEnabled ? 1 : 0);
}

void taskSensors(void *pvParameters) {
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    readDistance();
    readThermo();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void taskControl(void *pvParameters) {
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    updateLevelOutputs();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
  }
}

void taskI2C(void *pvParameters) {
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    bool enabled = false;
    lockGlobals();
    enabled = Globals::isI2CEnabled();
    unlockGlobals();

    if (enabled) {
      if (!g_i2cStarted) {
        setupSlaveI2C();
        g_i2cStarted = true;
      }
      i2cUpdate();
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(I2C_PERIOD_MS));
  }
}

void taskStorage(void *pvParameters) {
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    monitorConfigChanges();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
  }
}

void taskLogger(void *pvParameters) {
  TickType_t lastHeartbeat = xTaskGetTickCount();

  for (;;) {
    printSensorStatusIfChanged();

    TickType_t now = xTaskGetTickCount();
    if (now - lastHeartbeat >= pdMS_TO_TICKS(LOG_HEARTBEAT_MS)) {
      lastHeartbeat = now;
      logHeartbeat();
    }

    vTaskDelay(pdMS_TO_TICKS(LOG_POLL_MS));
  }
}

void taskEspNow(void *pvParameters) {
  (void)pvParameters;

  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (g_espNowChannelPending) {
      g_espNowChannelPending = false;
      g_espNowChannel = g_espNowNextChannel;
      esp_now_deinit();
      g_espNowInitialized = false;
      Serial.printf("[ESPNOW] Canal actualizado a %u\n", g_espNowChannel);
    }

    if (!ensureEspNowInitialized()) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000));
  }
}

String macSuffix() {
  uint8_t mac[6] = {0};
  WiFi.softAPmacAddress(mac);
  char buf[7];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  return String(buf);
}

String formatMac(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool isMacValid(const uint8_t *mac) {
  bool allZero = true;
  bool allFF = true;

  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0x00) allZero = false;
    if (mac[i] != 0xFF) allFF = false;
  }

  return !(allZero || allFF);
}

bool parseMacString(const String &input, uint8_t mac[6]) {
  String s = input;
  s.trim();
  s.toUpperCase();
  s.replace(":", "");
  s.replace("-", "");

  if (s.length() != 12) return false;

  for (int i = 0; i < 6; i++) {
    char c1 = s.charAt(i * 2);
    char c2 = s.charAt(i * 2 + 1);
    if (!isxdigit(c1) || !isxdigit(c2)) return false;

    char buf[3] = {c1, c2, 0};
    mac[i] = static_cast<uint8_t>(strtoul(buf, nullptr, 16));
  }

  return true;
}

String getPeerMacString() {
  uint8_t mac[6] = {0};
  lockGlobals();
  Globals::getEspNowPeerMac(mac);
  unlockGlobals();

  if (!isMacValid(mac)) {
    return String("--");
  }

  return formatMac(mac);
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0 || mac == nullptr || data == nullptr) return;

  if (!ensurePeerForMac(mac)) {
    return;
  }

  char buf[250];
  int copyLen = len;
  if (copyLen > static_cast<int>(sizeof(buf) - 1)) {
    copyLen = sizeof(buf) - 1;
  }
  memcpy(buf, data, copyLen);
  buf[copyLen] = 0;

  String msg(buf);
  msg.trim();
  msg.toUpperCase();

  Serial.printf("[ESPNOW] RX %s: %s\n", formatMac(mac).c_str(), msg.c_str());

  if (msg == "PING") {
    sendEspNowText(mac, "PONG");
    return;
  }

  if (msg == "GET_STATUS") {
    sendEspNowStatus(mac);
    Serial.printf("[ESPNOW] Enviando status a %s\n", formatMac(mac).c_str());
  }
}

void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (mac == nullptr) return;
  if (status == ESP_NOW_SEND_SUCCESS) return;

  Serial.printf("[ESPNOW] Error envio a %s (status=%d)\n", formatMac(mac).c_str(), status);
}

bool ensureEspNowInitialized() {
  if (g_espNowInitialized) return true;

  {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primary, &second);
    Serial.printf("[RX] Before esp_now_init, channel=%u\n", primary);
  }

  esp_wifi_set_channel(g_espNowChannel, WIFI_SECOND_CHAN_NONE);
  esp_err_t res = esp_now_init();
  if (res != ESP_OK) {
    Serial.printf("[ESPNOW] Error init (%d)\n", res);
    return false;
  }

  {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primary, &second);
    Serial.printf("[RX] After esp_now_init, channel=%u\n", primary);
  }

  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_register_send_cb(onEspNowSent);
  g_espNowInitialized = true;
  Serial.printf("[ESPNOW] Inicializado. MAC STA: %s, canal %u\n",
                WiFi.macAddress().c_str(), g_espNowChannel);
  return true;
}

bool configureEspNowPeer(const uint8_t *mac) {
  if (!isMacValid(mac)) {
    return false;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = g_espNowChannel;
  peerInfo.encrypt = false;

  esp_err_t res = esp_now_add_peer(&peerInfo);
  if (res != ESP_OK && res != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("[ESPNOW] Error add peer (%d)\n", res);
    return false;
  }
  return true;
}

bool ensurePeerForMac(const uint8_t *mac) {
  if (!isMacValid(mac)) return false;
  if (esp_now_is_peer_exist(mac)) return true;

  bool ok = configureEspNowPeer(mac);
  if (ok) {
    Serial.printf("[ESPNOW] Peer agregado: %s\n", formatMac(mac).c_str());
  }
  return ok;
}

bool isValidChannel(uint8_t ch) {
  return ch >= 1 && ch <= 13;
}

void requestEspNowChannel(uint8_t ch) {
  if (!isValidChannel(ch)) return;
  g_espNowNextChannel = ch;
  g_espNowChannelPending = true;
  g_espNowPrefs.putUChar("ch", ch);
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  (void)payload;
  (void)length;
  if (type == WStype_CONNECTED) {
    IPAddress ip = g_wsServer.remoteIP(num);
    Serial.printf("[WS] Cliente conectado: %s\n", ip.toString().c_str());
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[WS] Cliente desconectado: %u\n", num);
  }
}

void sendWsStatus() {
    float level = NAN;
    float temp = NAN;
    uint8_t status = 0;
    bool espEnabled = false;
    String peerStr;

    level = getLevelCm();
    lockGlobals();
    temp = Globals::getTemperature();
    status = Globals::getSensorStatus();
    espEnabled = Globals::isEspNowEnabled();
    unlockGlobals();
    peerStr = getPeerMacString();

  String json = "{";
  json += "\"level\":" + String(level) + ",";
  json += "\"temp\":" + String(temp) + ",";
  json += "\"status\":" + String(status) + ",";
  json += "\"esp_enabled\":" + String(espEnabled ? 1 : 0) + ",";
    json += "\"esp_peer\":\"" + peerStr + "\"";
    json += "}";

    g_wsServer.broadcastTXT(json);
}

void sendEspNowText(const uint8_t *dest, const char *text) {
  if (dest == nullptr || text == nullptr) return;
  if (!ensurePeerForMac(dest)) return;
  esp_now_send(dest, reinterpret_cast<const uint8_t *>(text), strlen(text));
}

void sendEspNowStatus(const uint8_t *dest) {
  if (dest == nullptr) return;
  if (!ensurePeerForMac(dest)) return;

  float level = NAN;
  float temp = NAN;
  uint8_t status = 0;
  bool i2cEnabled = false;

  lockGlobals();
    level = getLevelCm();
  temp = Globals::getTemperature();
  status = Globals::getSensorStatus();
  i2cEnabled = Globals::isI2CEnabled();
  unlockGlobals();

  String deviceId = g_apName.length() > 0 ? g_apName : String("SensorNivel");
  String macStr = WiFi.macAddress();
  String json = "{";
  json += "\"type\":\"status\",";
  json += "\"ver\":1,";
  json += "\"device\":{\"mac\":\"" + macStr + "\",\"id\":\"" + deviceId + "\"},";
  json += "\"ts_ms\":" + String(millis()) + ",";
  json += "\"seq\":" + String(g_espNowSeq++) + ",";
  json += "\"data\":{\"level_cm\":" + String(level, 2)
       + ",\"temp_c\":" + String(temp, 2)
       + ",\"status\":" + String(status)
       + ",\"i2c\":" + String(i2cEnabled ? 1 : 0) + "}";
  json += "}";

  esp_now_send(dest, reinterpret_cast<const uint8_t *>(json.c_str()), json.length());
}

void startWebPortal() {
  IPAddress apIP(192, 168, 1, 160);
  IPAddress apGateway(192, 168, 1, 160);
  IPAddress apSubnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);

  g_apName = "SensorNivel-" + macSuffix();
  WiFi.softAP(g_apName.c_str(), "12345678");
  delay(200);

  g_staMac = WiFi.macAddress();

  IPAddress currentIP = WiFi.softAPIP();
  Serial.print("[WEB] AP iniciado: ");
  Serial.println(g_apName);
  Serial.print("[WEB] IP AP: ");
  Serial.println(currentIP);

  g_webServer.on("/", HTTP_GET, []() {
    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Sensor Level - Portal</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --bg:#0f1115; --card:#171a21; --text:#e8eaf0; --muted:#9aa3b2; --accent:#2f7dff; --border:#2a303d; }
    body { font-family: Arial, sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 20px; }
    .wrap { max-width: 520px; margin: 0 auto; }
    h2 { margin: 0 0 16px 0; }
    .card { background: var(--card); padding: 16px; border-radius: 10px; border: 1px solid var(--border); box-shadow: 0 8px 24px rgba(0,0,0,0.3); margin-bottom: 14px; }
    .label { color: var(--muted); font-size: 0.9em; }
    .value { font-size: 1.1em; font-weight: 600; margin-top: 4px; }
    input { padding: 10px; width: 100%; box-sizing: border-box; font-size: 1em; margin-top: 6px; color: var(--text); background: #0f1218; border: 1px solid var(--border); border-radius: 6px; }
    input::placeholder { color: #6f7786; }
    button { padding: 10px; width: 100%; font-size: 1em; border: none; border-radius: 6px; background: var(--accent); color: #fff; margin-top: 8px; }
    button:disabled { opacity: 0.7; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    @media (max-width: 520px) { .grid { grid-template-columns: 1fr; } }
  </style>
</head>
<body>
  <div class="wrap">
    <h2>Sensor Dashboard</h2>

    <div class="card">
      <div class="label">STA MAC (ESP-NOW)</div>
      <div class="value" id="apMac">__STA_MAC__</div>
      <div class="label" style="margin-top:10px;">SSID</div>
      <div class="value" id="apSsid">__AP_SSID__</div>
    </div>

    <div class="card">
      <div class="label">Live Readings</div>
      <div class="grid" style="margin-top:10px;">
        <div>
          <div class="label">Level (cm)</div>
          <div class="value" id="level">--</div>
        </div>
        <div>
          <div class="label">Temperature (°C)</div>
          <div class="value" id="temp">--</div>
        </div>
        <div>
          <div class="label">Status</div>
          <div class="value" id="status">--</div>
        </div>
        <div>
          <div class="label">ESP-NOW</div>
          <div class="value" id="espState">Active</div>
        </div>
      </div>
    </div>

    <div class="card">
      <div class="label">Live Level</div>
      <canvas id="chartLevel" width="400" height="200" style="width:100%; margin-top:10px;"></canvas>
      <p id="levelLive" style="font-size:1.2em; margin-top:8px;">-- cm</p>
    </div>

    <div class="card">
      <div class="label">ESP-NOW Settings</div>
      <div class="label" style="margin-top:8px;">Target MAC</div>
      <input id="espMac" type="text" placeholder="AA:BB:CC:DD:EE:FF" value="__PEER_MAC__" onfocus="onMacFocus()" onblur="onMacBlur()">
      <button id="saveEspBtn" onclick="saveEspNow()">Save MAC</button>
      <div class="label" id="espSaveMsg" style="margin-top:8px;"></div>
      <div class="label" style="margin-top:10px;">Configured MAC</div>
      <div class="value" id="espPeer">__PEER_MAC__</div>

      <div class="label" style="margin-top:14px;">Channel</div>
      <input id="espChannel" type="number" min="1" max="13" placeholder="6">
      <button id="saveChBtn" onclick="saveEspChannel()">Save Channel</button>
      <div class="label" id="espChMsg" style="margin-top:8px;"></div>
      <div class="label" style="margin-top:10px;">Current channel</div>
      <div class="value" id="espChValue">--</div>
    </div>

    <div class="card">
      <div class="label">Kalman Filter</div>
      <div class="label" style="margin-top:8px;">Measurement Noise (Mea)</div>
      <input id="kalMea" type="number" step="0.01" placeholder="3.0">
      <div class="label" style="margin-top:8px;">Estimate Error (Est)</div>
      <input id="kalEst" type="number" step="0.01" placeholder="8.0">
      <div class="label" style="margin-top:8px;">Process Noise (Q)</div>
      <input id="kalQ" type="number" step="0.001" placeholder="0.08">
      <button id="saveKalmanBtn" onclick="saveKalman()">Save Kalman</button>
      <div class="label" id="kalmanMsg" style="margin-top:8px;"></div>
    </div>

    <div class="card">
      <div class="label">Zero Reference</div>
      <div class="label" style="margin-top:8px;">Current Offset (cm)</div>
      <input id="zeroOffset" type="number" step="0.01" placeholder="0.00">
      <button id="saveZeroBtn" onclick="saveZeroOffset()">Save Offset</button>
      <button id="calZeroBtn" onclick="calibrateZero()">Calibrate Zero</button>
      <div class="label" id="zeroMsg" style="margin-top:8px;"></div>
      <div class="label" style="margin-top:10px;">Current Level</div>
      <div class="value" id="zeroLevel">-- cm</div>
    </div>
  </div>

  <script>
    let isEditingMac = false;
    let isSavingMac = false;
    let ws = null;
    let wsRetryMs = 1000;
    let dataLevel = [];
    const maxPoints = 60;
    let smoothMinLevel = null;
    let smoothMaxLevel = null;
    let targetLevel = null;
    let displayLevel = null;
    let lastFrameMs = 0;

    function onMacFocus() {
      isEditingMac = true;
    }

    function onMacBlur() {
      isEditingMac = false;
    }

    function setText(id, value) {
      const el = document.getElementById(id);
      if (!el) return;
      if (el.innerText !== value) el.innerText = value;
    }

    function drawLevelChart() {
      const canvas = document.getElementById('chartLevel');
      if (!canvas || dataLevel.length < 2) return;

      const ctx = canvas.getContext('2d');
      const w = canvas.width;
      const h = canvas.height;

      let realMin = dataLevel[0];
      let realMax = dataLevel[0];
      for (let i = 1; i < dataLevel.length; i++) {
        const v = dataLevel[i];
        if (v < realMin) realMin = v;
        if (v > realMax) realMax = v;
      }

      let span = realMax - realMin;
      const margin = span * 0.1;
      realMin -= margin;
      realMax += margin;

      if (realMin < 0) realMin = 0;
      span = realMax - realMin;
      const minSpan = 5;
      if (span < minSpan) {
        const mid = (realMin + realMax) / 2;
        realMin = mid - minSpan / 2;
        realMax = mid + minSpan / 2;
        if (realMin < 0) {
          realMin = 0;
          realMax = minSpan;
        }
        span = minSpan;
      }

      if (smoothMinLevel === null) {
        smoothMinLevel = realMin;
        smoothMaxLevel = realMax;
      } else {
        const step = span * 0.1;
        if (realMin < smoothMinLevel - step) smoothMinLevel -= step;
        else if (realMin > smoothMinLevel + step) smoothMinLevel += step;
        else smoothMinLevel = realMin;

        if (realMax < smoothMaxLevel - step) smoothMaxLevel -= step;
        else if (realMax > smoothMaxLevel + step) smoothMaxLevel += step;
        else smoothMaxLevel = realMax;
      }

      if (smoothMinLevel < 0) smoothMinLevel = 0;
      let range = smoothMaxLevel - smoothMinLevel;
      if (range < minSpan) {
        smoothMaxLevel = smoothMinLevel + minSpan;
        range = minSpan;
      }

      ctx.clearRect(0, 0, w, h);
      ctx.fillStyle = '#11151c';
      ctx.fillRect(0, 0, w, h);
      ctx.strokeStyle = '#2a303d';
      ctx.lineWidth = 1;
      ctx.font = '12px Arial';
      ctx.fillStyle = '#9aa3b2';

      const gridLines = 4;
      for (let i = 0; i <= gridLines; i++) {
        const gy = (i / gridLines) * h;
        let value = smoothMaxLevel - (i / gridLines) * range;
        if (value < 0) value = 0;
        ctx.beginPath();
        ctx.moveTo(0, gy);
        ctx.lineTo(w, gy);
        ctx.stroke();
        ctx.fillText(value.toFixed(1) + ' cm', 5, gy - 3);
      }

      ctx.beginPath();
      ctx.lineWidth = 2;
      ctx.strokeStyle = '#2f7dff';
      for (let i = 0; i < dataLevel.length; i++) {
        const x = (i / (maxPoints - 1)) * w;
        const y = h - ((dataLevel[i] - smoothMinLevel) / range) * h;
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();

      const lastX = ((dataLevel.length - 1) / (maxPoints - 1)) * w;
      const lastY = h - ((dataLevel[dataLevel.length - 1] - smoothMinLevel) / range) * h;
      ctx.fillStyle = '#ff9d2d';
      ctx.beginPath();
      ctx.arc(lastX, lastY, 4, 0, 2 * Math.PI);
      ctx.fill();
    }

    function animateLevel(ts) {
      if (!lastFrameMs) lastFrameMs = ts;
      const dt = (ts - lastFrameMs) / 1000;
      lastFrameMs = ts;

      if (targetLevel !== null) {
        if (displayLevel === null) {
          displayLevel = targetLevel;
        } else {
          const k = 6.0;
          displayLevel = displayLevel + (targetLevel - displayLevel) * (1 - Math.exp(-k * dt));
        }
      }

      drawLevelChart();

      requestAnimationFrame(animateLevel);
    }

    function applyStatus(j) {
      const levelText = isNaN(j.level) ? '--' : j.level.toFixed(2);
      setText('level', levelText);
      if (!isNaN(j.level)) {
        targetLevel = j.level;
        if (displayLevel === null) displayLevel = j.level;
      }
      if (displayLevel !== null) {
        setText('levelLive', displayLevel.toFixed(2) + ' cm');
      } else {
        setText('levelLive', '-- cm');
      }
      setText('temp', isNaN(j.temp) ? '--' : j.temp.toFixed(2));
      setText('status', String(j.status));
      setText('espState', j.esp_enabled ? 'Active' : 'Inactive');
      setText('espPeer', j.esp_peer);

      if (!isNaN(j.level)) {
        dataLevel.push(j.level);
        if (dataLevel.length > maxPoints) dataLevel.shift();
      }

      const input = document.getElementById('espMac');
      if (!isEditingMac && !isSavingMac && input) {
        input.value = j.esp_peer === '--' ? '' : j.esp_peer;
      }
    }

    function fetchStatusOnce() {
      fetch('/status')
        .then(r => r.json())
        .then(j => applyStatus(j))
        .catch(() => {});
    }

    function connectWs() {
      const host = window.location.hostname;
      ws = new WebSocket('ws://' + host + ':81/');

      ws.onopen = () => {
        wsRetryMs = 1000;
      };

      ws.onmessage = (evt) => {
        try {
          const j = JSON.parse(evt.data);
          applyStatus(j);
        } catch (_) {}
      };

      ws.onclose = () => {
        ws = null;
        setTimeout(connectWs, wsRetryMs);
        wsRetryMs = Math.min(wsRetryMs * 2, 5000);
      };

      ws.onerror = () => {
        if (ws) ws.close();
      };
    }

    function saveEspNow() {
      const btn = document.getElementById('saveEspBtn');
      const msg = document.getElementById('espSaveMsg');
      const mac = document.getElementById('espMac').value.trim();
      isSavingMac = true;
      btn.disabled = true;
      msg.innerText = 'Saving...';

      fetch('/save_espnow?mac=' + encodeURIComponent(mac))
        .then(async r => {
          const text = await r.text();
          if (r.ok) {
            msg.innerText = 'Saved';
          } else {
            msg.innerText = text || 'Invalid MAC address';
          }
          fetchStatusOnce();
        })
        .catch(() => {
          msg.innerText = 'Save failed';
        })
        .finally(() => {
          isSavingMac = false;
          btn.disabled = false;
          setTimeout(() => {
            if (!isEditingMac) {
              msg.innerText = '';
            }
          }, 1500);
        });
    }

    function fetchEspNowConfig() {
      fetch('/api/espnow/config')
        .then(r => r.json())
        .then(j => {
          const chValue = document.getElementById('espChValue');
          if (chValue) chValue.innerText = j.channel;
          const chInput = document.getElementById('espChannel');
          if (chInput && document.activeElement !== chInput) {
            chInput.value = j.channel;
          }
        })
        .catch(() => {});
    }

    function saveEspChannel() {
      const btn = document.getElementById('saveChBtn');
      const msg = document.getElementById('espChMsg');
      const ch = document.getElementById('espChannel').value.trim();
      btn.disabled = true;
      msg.innerText = 'Saving...';

      fetch('/api/espnow/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({channel: Number(ch)})
      })
        .then(async r => {
          const text = await r.text();
          if (r.ok) {
            msg.innerText = 'Saved';
          } else {
            msg.innerText = text || 'Invalid channel';
          }
          fetchEspNowConfig();
        })
        .catch(() => {
          msg.innerText = 'Save failed';
        })
        .finally(() => {
          btn.disabled = false;
          setTimeout(() => { msg.innerText = ''; }, 1500);
        });
    }

    function fetchKalmanConfig() {
      fetch('/api/kalman')
        .then(r => r.json())
        .then(j => {
          const mea = document.getElementById('kalMea');
          const est = document.getElementById('kalEst');
          const q = document.getElementById('kalQ');
          if (mea && document.activeElement !== mea) mea.value = j.kalMea;
          if (est && document.activeElement !== est) est.value = j.kalEst;
          if (q && document.activeElement !== q) q.value = j.kalQ;
        })
        .catch(() => {});
    }

    function fetchZeroConfig() {
      fetch('/api/zero')
        .then(r => r.json())
        .then(j => {
          const off = document.getElementById('zeroOffset');
          if (off && document.activeElement !== off) off.value = j.offset;
          const level = isNaN(j.level) ? '--' : j.level.toFixed(2) + ' cm';
          setText('zeroLevel', level);
        })
        .catch(() => {});
    }

    function saveZeroOffset() {
      const btn = document.getElementById('saveZeroBtn');
      const msg = document.getElementById('zeroMsg');
      const val = document.getElementById('zeroOffset').value.trim();
      btn.disabled = true;
      msg.innerText = 'Saving...';

      fetch('/api/zero', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({offset: Number(val)})
      })
        .then(async r => {
          const text = await r.text();
          msg.innerText = r.ok ? 'Saved' : (text || 'Invalid offset');
          fetchZeroConfig();
        })
        .catch(() => {
          msg.innerText = 'Save failed';
        })
        .finally(() => {
          btn.disabled = false;
          setTimeout(() => { msg.innerText = ''; }, 1500);
        });
    }

    function calibrateZero() {
      const btn = document.getElementById('calZeroBtn');
      const msg = document.getElementById('zeroMsg');
      btn.disabled = true;
      msg.innerText = 'Calibrating...';

      fetch('/api/zero', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({calibrate: true})
      })
        .then(async r => {
          const text = await r.text();
          msg.innerText = r.ok ? 'Calibrated' : (text || 'Invalid reading');
          fetchZeroConfig();
        })
        .catch(() => {
          msg.innerText = 'Calibration failed';
        })
        .finally(() => {
          btn.disabled = false;
          setTimeout(() => { msg.innerText = ''; }, 1500);
        });
    }

    function saveKalman() {
      const btn = document.getElementById('saveKalmanBtn');
      const msg = document.getElementById('kalmanMsg');
      const kalMea = document.getElementById('kalMea').value.trim();
      const kalEst = document.getElementById('kalEst').value.trim();
      const kalQ = document.getElementById('kalQ').value.trim();
      btn.disabled = true;
      msg.innerText = 'Saving...';

      fetch('/api/kalman', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          kalMea: Number(kalMea),
          kalEst: Number(kalEst),
          kalQ: Number(kalQ)
        })
      })
        .then(async r => {
          const text = await r.text();
          if (r.ok) {
            msg.innerText = 'Saved';
          } else {
            msg.innerText = text || 'Invalid values';
          }
          fetchKalmanConfig();
        })
        .catch(() => {
          msg.innerText = 'Save failed';
        })
        .finally(() => {
          btn.disabled = false;
          setTimeout(() => { msg.innerText = ''; }, 1500);
        });
    }

    connectWs();
    fetchStatusOnce();
    fetchEspNowConfig();
    fetchKalmanConfig();
    fetchZeroConfig();
    requestAnimationFrame(animateLevel);
  </script>
</body>
</html>
)HTML";

    html.replace("__STA_MAC__", g_staMac);
    html.replace("__AP_SSID__", g_apName);
    html.replace("__PEER_MAC__", getPeerMacString());

    g_webServer.send(200, "text/html", html);
  });

  g_webServer.on("/status", HTTP_GET, []() {
    float level = NAN;
    float temp = NAN;
    uint8_t status = 0;
    bool espEnabled = false;
    String peerStr;

    level = getLevelCm();
    lockGlobals();
    temp = Globals::getTemperature();
    status = Globals::getSensorStatus();
    espEnabled = Globals::isEspNowEnabled();
    unlockGlobals();
    peerStr = getPeerMacString();

    String json = "{";
    json += "\"level\":" + String(level) + ",";
    json += "\"temp\":" + String(temp) + ",";
    json += "\"status\":" + String(status) + ",";
    json += "\"esp_enabled\":" + String(espEnabled ? 1 : 0) + ",";
    json += "\"esp_peer\":\"" + peerStr + "\"";
    json += "}";

    g_webServer.send(200, "application/json", json);
  });

  g_webServer.on("/api/espnow/config", HTTP_GET, []() {
    String json = "{";
    json += "\"localMac\":\"" + WiFi.macAddress() + "\",";
    json += "\"channel\":" + String(g_espNowChannel);
    json += "}";
    g_webServer.send(200, "application/json", json);
  });

  g_webServer.on("/api/espnow/config", HTTP_POST, []() {
    if (!g_webServer.hasArg("plain")) {
      g_webServer.send(400, "text/plain", "Body required");
      return;
    }

    String body = g_webServer.arg("plain");
    int idx = body.indexOf("\"channel\"");
    if (idx < 0) {
      g_webServer.send(400, "text/plain", "Channel required");
      return;
    }

    int colon = body.indexOf(':', idx);
    if (colon < 0) {
      g_webServer.send(400, "text/plain", "Invalid channel");
      return;
    }

    String num;
    for (int i = colon + 1; i < body.length(); i++) {
      char c = body.charAt(i);
      if (c >= '0' && c <= '9') {
        num += c;
      } else if (!num.isEmpty()) {
        break;
      }
    }

    int ch = num.toInt();
    if (!isValidChannel(static_cast<uint8_t>(ch))) {
      g_webServer.send(400, "text/plain", "Invalid channel");
      return;
    }

    requestEspNowChannel(static_cast<uint8_t>(ch));
    g_webServer.send(200, "text/plain", "OK");
  });

  g_webServer.on("/api/kalman", HTTP_GET, []() {
    float kalMea = 0.0f;
    float kalEst = 0.0f;
    float kalQ = 0.0f;
    lockGlobals();
    kalMea = Globals::getKalMea();
    kalEst = Globals::getKalEst();
    kalQ = Globals::getKalQ();
    unlockGlobals();

    String json = "{";
    json += "\"kalMea\":" + String(kalMea, 3) + ",";
    json += "\"kalEst\":" + String(kalEst, 3) + ",";
    json += "\"kalQ\":" + String(kalQ, 4);
    json += "}";
    g_webServer.send(200, "application/json", json);
  });

  g_webServer.on("/api/kalman", HTTP_POST, []() {
    if (!g_webServer.hasArg("plain")) {
      g_webServer.send(400, "text/plain", "Body required");
      return;
    }

    String body = g_webServer.arg("plain");
    int meaIdx = body.indexOf("\"kalMea\"");
    int estIdx = body.indexOf("\"kalEst\"");
    int qIdx = body.indexOf("\"kalQ\"");
    if (meaIdx < 0 || estIdx < 0 || qIdx < 0) {
      g_webServer.send(400, "text/plain", "Parameters required");
      return;
    }

    auto readNumber = [&body](int idx) -> float {
      int colon = body.indexOf(':', idx);
      if (colon < 0) return NAN;
      String num;
      for (int i = colon + 1; i < body.length(); i++) {
        char c = body.charAt(i);
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' ) {
          num += c;
        } else if (!num.isEmpty()) {
          break;
        }
      }
      return num.toFloat();
    };

    float kalMea = readNumber(meaIdx);
    float kalEst = readNumber(estIdx);
    float kalQ = readNumber(qIdx);

    if (!isfinite(kalMea) || !isfinite(kalEst) || !isfinite(kalQ) ||
        kalMea <= 0.0f || kalEst <= 0.0f || kalQ < 0.0f) {
      g_webServer.send(400, "text/plain", "Invalid values");
      return;
    }

    lockGlobals();
    Globals::setKalMea(kalMea);
    Globals::setKalEst(kalEst);
    Globals::setKalQ(kalQ);
    unlockGlobals();

    g_webServer.send(200, "text/plain", "OK");
  });

  g_webServer.on("/api/zero", HTTP_GET, []() {
    float offset = 0.0f;
    float filt = NAN;
    float level = NAN;
    lockGlobals();
    offset = Globals::getZeroOffset();
    filt = Globals::getDistanceFiltered();
    unlockGlobals();
    level = getLevelCm();

    String json = "{";
    json += "\"offset\":" + String(offset, 2) + ",";
    json += "\"filtered\":" + String(filt, 2) + ",";
    json += "\"level\":" + String(level, 2);
    json += "}";
    g_webServer.send(200, "application/json", json);
  });

  g_webServer.on("/api/zero", HTTP_POST, []() {
    if (!g_webServer.hasArg("plain")) {
      g_webServer.send(400, "text/plain", "Body required");
      return;
    }

    String body = g_webServer.arg("plain");
    bool calibrate = body.indexOf("\"calibrate\"") >= 0;
    float offset = NAN;

    if (calibrate) {
      float filt = NAN;
      lockGlobals();
      filt = Globals::getDistanceFiltered();
      unlockGlobals();
      if (isnan(filt)) {
        g_webServer.send(400, "text/plain", "Invalid reading");
        return;
      }
      offset = filt;
    } else {
      int idx = body.indexOf("\"offset\"");
      if (idx < 0) {
        g_webServer.send(400, "text/plain", "Offset required");
        return;
      }

      int colon = body.indexOf(':', idx);
      if (colon < 0) {
        g_webServer.send(400, "text/plain", "Invalid offset");
        return;
      }

      String num;
      for (int i = colon + 1; i < body.length(); i++) {
        char c = body.charAt(i);
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' ) {
          num += c;
        } else if (!num.isEmpty()) {
          break;
        }
      }
      offset = num.toFloat();
    }

    if (!isfinite(offset) || offset < 0.0f || offset > 5000.0f) {
      g_webServer.send(400, "text/plain", "Invalid offset");
      return;
    }

    lockGlobals();
    Globals::setZeroOffset(offset);
    unlockGlobals();
    g_webServer.send(200, "text/plain", "OK");
  });

  g_webServer.on("/save_espnow", HTTP_GET, []() {
    if (!g_webServer.hasArg("mac")) {
      g_webServer.send(400, "text/plain", "MAC required");
      return;
    }

    String macStr = g_webServer.arg("mac");
    uint8_t mac[6] = {0};
    if (!parseMacString(macStr, mac) || !isMacValid(mac)) {
      g_webServer.send(400, "text/plain", "Invalid MAC");
      return;
    }

    lockGlobals();
    Globals::setEspNowPeerMac(mac);
    Globals::setEspNowEnabled(true);
    unlockGlobals();

    ConfigData cfg;
    lockGlobals();
    cfg.minLevel     = Globals::getMinLevel();
    cfg.maxLevel     = Globals::getMaxLevel();
    cfg.samplePeriod = Globals::getSamplePeriod();
    cfg.kalMea       = Globals::getKalMea();
    cfg.kalEst       = Globals::getKalEst();
    cfg.kalQ         = Globals::getKalQ();
    cfg.i2cAddress   = Globals::getI2CAddress();
    Globals::getEspNowPeerMac(cfg.espNowPeerMac);
    cfg.espNowEnabled = Globals::isEspNowEnabled();
    cfg.zeroOffsetCm = Globals::getZeroOffset();
    unlockGlobals();

    Storage::saveConfig(cfg);
    Serial.printf("[ESPNOW] MAC destino actualizada: %s\n", formatMac(mac).c_str());
    g_webServer.send(200, "text/plain", "OK");
  });

  g_webServer.onNotFound([]() {
    g_webServer.sendHeader("Location", "/", true);
    g_webServer.send(302, "text/plain", "");
  });

  g_webServer.begin();
  g_wsServer.begin();
  g_wsServer.onEvent(onWsEvent);
  g_webPortalStarted = true;
}

void taskWebPortal(void *pvParameters) {
  (void)pvParameters;

  if (!g_webPortalStarted) {
    startWebPortal();
  }

  for (;;) {
    g_webServer.handleClient();
    g_wsServer.loop();

    uint32_t nowMs = millis();
    if (nowMs - g_lastWsSendMs >= 250) {
      g_lastWsSendMs = nowMs;
      sendWsStatus();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
