#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>

#include "dashboard.h"
#include "modem_manager.h"
#include "ota_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char kApn[] = "internet.comcel.com.co";
static const char kUser[] = "comcel";
static const char kPass[] = "comcel";

static const int8_t kPinTx = 26;
static const int8_t kPinRx = 27;
static const int8_t kPwrKey = 4;

static const int8_t kSdMiso = 2;
static const int8_t kSdMosi = 15;
static const int8_t kSdSclk = 14;
static const int8_t kSdCs = 13;

static const char kHttpTestUrl[] = "http://example.com/";
static const char kHttpDownloadUrl[] =
    "https://raw.githubusercontent.com/MateoDelG/tests-ota-esp/master/firmware.bin";
static const char kHttpDownloadPath[] = "/firmware.bin";
static const uint16_t kHttpDownloadChunkSize = 1024; //512
static const uint32_t kSdSpiClockHz = 1000000;
static const uint32_t kSdFlushThreshold = 0;
static const uint32_t kSdProbeSizeLight = 16384;
static const uint32_t kSdProbeSizeStress = 1048576;
static const size_t kSdProbeBlock = 4096;
static const uint8_t kSdInitRetries = 3;
static const uint16_t kSdInitPreDelayMs = 150;
static const uint16_t kSdInitPostSpiDelayMs = 150;
static const uint16_t kSdInitRetryDelayMs = 400;
static const uint16_t kSdShutdownDelayMs = 300;

static const char kMqttHost[] =
    "b282c2526e92497b9e5d5741f7483e22.s1.eu.hivemq.cloud";
static const uint16_t kMqttPort = 8883;
static const char kMqttClientId[] = "esp001";
static const char kMqttUser[] = "testesp001";
static const char kMqttPass[] = "Testesp001+";
static const char kMqttTopic[] = "esp001";

static WebServer server(80);
static WebSocketsServer ws(81);
static Dashboard dashboard;

static ModemConfig modemConfig = {
  Serial1,
  Serial,
  {kPinTx, kPinRx, kPwrKey, -1},
  {kApn, kUser, kPass},
  "",
  115200,
  5000,
  60000,
  3,
  true,
  [](bool isTx, const String& line) {
    String payload = String(isTx ? "> " : "< ") + line;
    dashboard.pushLine(DashboardSource::Modem, payload);
  },
};

static void logUsb(const String& line);

static ModemManager modem(modemConfig);
static OtaManager ota(logUsb);

static String usbLine;

static Preferences otaPrefs;
static bool otaSdReady = true;
static bool sdMounted = false;
static bool sdValidated = false;
static bool sdReadyForOta = false;

enum class SdProbeMode : uint8_t {
  None = 0,
  Light,
  Stress,
};

static const char kOtaPrefNs[] = "ota";
static const char kOtaPrefState[] = "download_state";
static const char kOtaPrefExpected[] = "expected_size";
static const char kOtaPrefWritten[] = "written_bytes";

enum OtaDownloadState : uint8_t {
  OtaStateIdle = 0,
  OtaStateDownloading = 1,
  OtaStateValidating = 2,
  OtaStateReady = 3,
};

static EventGroupHandle_t modemEvents;
static const EventBits_t BIT_MODEM_READY = 1 << 0;
static const EventBits_t BIT_DATA_READY = 1 << 1;
static const EventBits_t BIT_RUN_TEST = 1 << 2;
static const EventBits_t BIT_CANCEL_TEST = 1 << 3;

enum class TestType : uint8_t {
  None = 0,
  Http,
  HttpDownload,
  OtaInstall,
  Mqtt,
  UbidotsPublish,
  UbidotsSubscribe,
};

static volatile TestType currentTest = TestType::None;
static volatile TestType queuedTest = TestType::None;
static volatile bool cancelRequested = false;

static const char kUbiToken[] = "BBUS-orL2zH4XNEKC0880tXUcuxdTWpX5R8";
static const char kUbiDevice[] = "aqcuicola-001";
static const char kUbiVariable[] = "test-out";
static const char kUbiHost[] = "industrial.api.ubidots.com";
static const uint16_t kUbiPort = 8883;
static const char kUbiTopicPub[] = "/v1.6/devices/aqcuicola-001";
static const char kUbiTopicSub[] = "/v1.6/devices/aqcuicola-001/test-out/lv";

static void setupWifi() {
  const char* ssid = "Delga";
  const char* pass = "Delga1213";

  IPAddress localIP(192, 168, 1, 180);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (strlen(ssid) == 0) {
    Serial.println("WiFi credentials not set");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.config(localIP, gateway, subnet);
  WiFi.begin(ssid, pass);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection timeout");
  }
}

static void logUsb(const String& line) {
  Serial.println(line);
  dashboard.pushLine(DashboardSource::Usb, line);
}

static void logUsbSink(bool isTx, const String& line) {
  (void)isTx;
  logUsb(line);
}

static void setSdState(bool mounted, bool validated) {
  sdMounted = mounted;
  sdValidated = validated;
  sdReadyForOta = mounted && validated;
}

static bool sdWriteProbe(const char* path, uint32_t probeSize,
                         const char* label) {
  logUsb(String("[sd] ") + label + " probe start");
  if (SD.exists(path)) {
    logUsb(String("[sd] ") + label + " probe stale temp detected");
    if (!SD.remove(path)) {
      logUsb(String("[sd] ") + label + " probe stale temp remove failed");
      logUsb("[sd] residual sdprobe.tmp detected; SD marked unhealthy");
      return false;
    }
    if (SD.exists(path)) {
      logUsb("[sd] residual sdprobe.tmp detected; SD marked unhealthy");
      return false;
    }
    logUsb(String("[sd] ") + label + " probe stale temp removed");
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    logUsb(String("[sd] ") + label + " probe open fail");
    return false;
  }
  logUsb(String("[sd] ") + label + " probe open ok");
  logUsb(String("[sd] ") + label + " probe size: " + probeSize);
  logUsb(String("[sd] ") + label + " probe block: " + kSdProbeBlock);
  logUsb(String("[sd] ") + label + " probe flush: final only");

  static uint8_t buffer[kSdProbeBlock];
  for (size_t i = 0; i < kSdProbeBlock; ++i) {
    buffer[i] = static_cast<uint8_t>(0xA5 ^ (i & 0xFF));
  }

  size_t remaining = probeSize;
  size_t totalWritten = 0;
  while (remaining > 0) {
    size_t chunk = remaining > kSdProbeBlock ? kSdProbeBlock : remaining;
    size_t written = file.write(buffer, chunk);
    if (written != chunk) {
      logUsb(String("[sd] ") + label +
             " probe write fail, wrote=" + written + ", expected=" + chunk +
             ", total=" + totalWritten);
      file.close();
      return false;
    }
    totalWritten += written;
    remaining -= written;
  }

  logUsb(String("[sd] ") + label + " probe final flush");
  file.flush();
  logUsb(String("[sd] ") + label + " probe final flush ok");
  logUsb(String("[sd] ") + label + " probe close");
  file.close();

  File check = SD.open(path, FILE_READ);
  if (!check) {
    logUsb(String("[sd] ") + label + " probe reopen fail");
    return false;
  }
  size_t size = check.size();
  logUsb(String("[sd] ") + label + " probe size read: " + size);
  if (size != probeSize) {
    check.close();
    logUsb(String("[sd] ") + label + " probe size mismatch");
    return false;
  }

  uint8_t readback[8] = {0};
  size_t readCount = check.readBytes(reinterpret_cast<char*>(readback),
                                     sizeof(readback));
  check.close();
  bool readOk = (readCount == sizeof(readback));
  if (readOk) {
    for (size_t i = 0; i < sizeof(readback); ++i) {
      uint8_t expected = static_cast<uint8_t>(0xA5 ^ (i & 0xFF));
      if (readback[i] != expected) {
        readOk = false;
        break;
      }
    }
  }
  logUsb(String("[sd] ") + label +
         " probe readback " + (readOk ? "ok" : "fail"));

  if (!readOk) {
    return false;
  }
  if (!SD.remove(path)) {
    logUsb(String("[sd] ") + label + " probe cleanup remove failed");
    logUsb("[sd] residual sdprobe.tmp detected; SD marked unhealthy");
    return false;
  }
  if (SD.exists(path)) {
    logUsb("[sd] residual sdprobe.tmp detected; SD marked unhealthy");
    return false;
  }
  logUsb(String("[sd] ") + label + " probe cleanup ok");
  return true;
}

static bool sdLightWriteProbe(const char* path) {
  return sdWriteProbe(path, kSdProbeSizeLight, "light");
}

static bool sdStressWriteProbe(const char* path) {
  return sdWriteProbe(path, kSdProbeSizeStress, "stress");
}

static bool initSdWithRetry(const char* context, SdProbeMode probeMode) {
  for (uint8_t attempt = 1; attempt <= kSdInitRetries; ++attempt) {
    logUsb(String("[sd] init ") + context + " attempt " + attempt + "/" +
           kSdInitRetries);
    SD.end();
    SPI.end();
    delay(kSdInitPreDelayMs);
    SPI.begin(kSdSclk, kSdMiso, kSdMosi, kSdCs);
    logUsb(String("[sd] spi clock: ") + kSdSpiClockHz + " Hz");
    delay(kSdInitPostSpiDelayMs);
    if (!SD.begin(kSdCs, SPI, kSdSpiClockHz)) {
      logUsb("[sd] init mount failed");
      setSdState(false, false);
      if (attempt < kSdInitRetries) {
        delay(kSdInitRetryDelayMs);
      }
      continue;
    }
    logUsb("[sd] init mount ok");
    setSdState(true, false);
    if (probeMode == SdProbeMode::None) {
      return true;
    }
    bool probeOk = false;
    if (probeMode == SdProbeMode::Light) {
      probeOk = sdLightWriteProbe("/sdprobe.tmp");
    } else {
      probeOk = sdStressWriteProbe("/sdprobe.tmp");
    }
    if (!probeOk) {
      logUsb("[sd] init probe failed");
      logUsb("[sd] mounted but not validated");
      setSdState(true, false);
      if (attempt < kSdInitRetries) {
        delay(kSdInitRetryDelayMs);
      }
      continue;
    }
    logUsb("[sd] init probe ok");
    logUsb("[sd] ready for ota");
    setSdState(true, true);
    return true;
  }
  logUsb("[sd] init failed after retries");
  setSdState(false, false);
  SD.end();
  SPI.end();
  return false;
}

static void finalizeSdAfterOta() {
  logUsb("[sd] finalize after ota copy");
  SD.end();
  setSdState(false, false);
  delay(kSdShutdownDelayMs);
  logUsb("[sd] finalize done");
}

static void purgeOtaArtifacts() {
  logUsb("[sd] purge ota artifacts");
  const char* kArtifacts[] = {
      "/fw.part",
      "/firmware.bin.tmp",
      "/firmware.bak",
      "/sdprobe.tmp",
  };

  for (const char* path : kArtifacts) {
    if (SD.remove(path)) {
      logUsb(String("[sd] remove ") + path);
    }
  }

  logUsb("[sd] purge ota artifacts done");
}

static void setOtaRecoveryPending(bool value) {
  otaPrefs.begin("ota", false);
  otaPrefs.putBool("recovery_pending", value);
  otaPrefs.end();
}

static bool isOtaRecoveryPending() {
  otaPrefs.begin("ota", true);
  bool value = otaPrefs.getBool("recovery_pending", false);
  otaPrefs.end();
  return value;
}

static void setOtaDownloadState(uint8_t state, uint32_t expected,
                                uint32_t written) {
  otaPrefs.begin(kOtaPrefNs, false);
  otaPrefs.putUChar(kOtaPrefState, state);
  otaPrefs.putUInt(kOtaPrefExpected, expected);
  otaPrefs.putUInt(kOtaPrefWritten, written);
  otaPrefs.end();
}

static uint8_t getOtaDownloadState(uint32_t* expected,
                                   uint32_t* written) {
  otaPrefs.begin(kOtaPrefNs, true);
  uint8_t state = otaPrefs.getUChar(kOtaPrefState, OtaStateIdle);
  if (expected) {
    *expected = otaPrefs.getUInt(kOtaPrefExpected, 0);
  }
  if (written) {
    *written = otaPrefs.getUInt(kOtaPrefWritten, 0);
  }
  otaPrefs.end();
  return state;
}

static bool recoverSd();

static void listSdRoot() {
  if (!sdLightWriteProbe("/sdprobe.tmp")) {
    if (!recoverSd()) {
      logUsb("SD not ready");
      return;
    }
  }

  File root = SD.open("/");
  if (!root) {
    logUsb("SD open root failed");
    return;
  }

  logUsb("SD list:");
  File entry = root.openNextFile();
  if (!entry) {
    logUsb("(empty)");
  }
  while (entry) {
    String line = entry.name();
    if (entry.isDirectory()) {
      line += "/";
    } else {
      line += " ";
      line += String(entry.size());
      line += " bytes";
    }
    logUsb(line);
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
}

static bool recoverSd() {
  logUsb("[sd] recovery start");
  bool ok = initSdWithRetry("recovery", SdProbeMode::Stress);
  if (ok) {
    purgeOtaArtifacts();
    logUsb("[sd] recovery ok");
  } else {
    logUsb("[sd] recovery failed");
  }
  return ok;
}

static void logModem(const String& line) {
  dashboard.pushLine(DashboardSource::Modem, line);
}

static void publishUbidotsTest() {
  logUsb("Ubidots publish...");
  String topic = kUbiTopicPub;
  String payload = String("{\"") + "test-in" + "\":" + String(25.4f, 2) +
                   "}";
  if (modem.mqtt().connect(kUbiHost, kUbiPort, kMqttClientId, kUbiToken,
                           kUbiToken,
                           true) &&
      modem.mqtt().publish(topic.c_str(), payload.c_str(), 1, false)) {
    logUsb("Ubidots OK");
  } else {
    logUsb("Ubidots failed");
  }
  modem.mqtt().disconnect();
}

static void setTestState(bool running) {
  logUsb(String("Test state: ") + (running ? "running" : "idle"));
}

static void requestTest(TestType test) {
  if (!modemEvents) {
    return;
  }

  if (currentTest != TestType::None) {
    queuedTest = test;
    logUsb("Test queued");
    return;
  }

  queuedTest = test;
  xEventGroupSetBits(modemEvents, BIT_RUN_TEST);
}

static void handleDashboardCommand(const String& cmd) {
  if (!modemEvents) {
    return;
  }

  if (cmd.startsWith("at:") || cmd.startsWith("AT") || cmd.startsWith("at")) {
    String raw = cmd;
    if (raw.startsWith("at:")) {
      raw.remove(0, 3);
    }
    raw.trim();

    bool hadAtPrefix = false;
    if (raw.startsWith("AT")) {
      raw.remove(0, 2);
      raw.trim();
      hadAtPrefix = true;
    }

    if (!hadAtPrefix && raw.length() > 0 && !raw.startsWith("+")) {
      logUsb("AT command must start with AT or +");
      return;
    }
    if (raw.length() > 200) {
      logUsb("AT command too long");
      return;
    }

    String display = hadAtPrefix ? String("AT") + raw : String("AT") + raw;
    logUsb(String("AT> ") + display);
    AtResult res = modem.at().exec(5000L, raw.c_str());
    if (res.raw.length() > 0) {
      logUsb(String("AT< ") + res.raw);
    }
    if (!res.ok || res.error) {
      logUsb("AT error");
    }
    return;
  }

  if (cmd == "run_http") {
    logUsb("Dashboard: run HTTP test");
    requestTest(TestType::Http);
  } else if (cmd == "run_http_download") {
    logUsb("Dashboard: download OTA file");
    requestTest(TestType::HttpDownload);
  } else if (cmd == "run_ota_install") {
    logUsb("Dashboard: install OTA from SD");
    requestTest(TestType::OtaInstall);
  } else if (cmd == "run_sd_list") {
    logUsb("Dashboard: list SD");
    listSdRoot();
  } else if (cmd == "run_mqtt") {
    logUsb("Dashboard: run MQTT test");
    requestTest(TestType::Mqtt);
  } else if (cmd == "run_ubidots_pub") {
    logUsb("Dashboard: run Ubidots publish");
    requestTest(TestType::UbidotsPublish);
  } else if (cmd == "run_ubidots_sub") {
    logUsb("Dashboard: run Ubidots subscribe");
    requestTest(TestType::UbidotsSubscribe);
  } else if (cmd == "cancel_test") {
    logUsb("Dashboard: cancel test");
    cancelRequested = true;
    queuedTest = TestType::None;
    xEventGroupSetBits(modemEvents, BIT_CANCEL_TEST);
  } else {
    logUsb(String("Dashboard: unknown command ") + cmd);
  }
}

static void readSerialLines(Stream& stream, String& buffer,
                            DashboardSource src) {
  while (stream.available()) {
    char c = static_cast<char>(stream.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (buffer.length() > 0) {
        dashboard.pushLine(src, buffer);
        buffer = "";
      }
      continue;
    }
    if (buffer.length() < 512) {
      buffer += c;
    }
  }
}

static void modemCoreTask(void* pv) {
  (void)pv;

  bool modemReady = false;

  for (int attempt = 0; attempt < 3; ++attempt) {
    logUsb(String("Starting modem (attempt ") + (attempt + 1) + "/3)");
    logUsb("Waiting for network (up to 60s)...");
    if (modem.begin()) {
      modemReady = true;
      break;
    }
    logUsb("Modem init failed, retrying...");
    vTaskDelay(pdMS_TO_TICKS(15000));
  }

  if (!modemReady) {
    logUsb("Modem init failed after retries");
    vTaskDelete(nullptr);
    return;
  }

  logUsb("Modem initialized");
  logUsb("Reading network information...");

  ModemInfo info = modem.getNetworkInfo();
  logUsb("=== Modem info ===");
  logUsb(info.modemName);
  logUsb(info.modemInfo);
  logUsb(info.imei);
  logUsb(info.iccid);
  logUsb(info.operatorName);
  logUsb(info.localIp);
  logModem(info.cpsi);
  logUsb(String("CSQ: ") + info.signalQuality + " => " + info.signalPercent +
         "%");

  logUsb("Opening data session...");
  if (modem.ensureDataSession()) {
    logUsb("Data session ready");
    xEventGroupSetBits(modemEvents, BIT_DATA_READY);
  } else {
    logUsb("Data session failed");
  }

  xEventGroupSetBits(modemEvents, BIT_MODEM_READY);
  vTaskDelete(nullptr);
}

static void httpTestTask(void* pv) {
  (void)pv;
  xEventGroupWaitBits(modemEvents, BIT_DATA_READY, pdFALSE, pdTRUE,
                      portMAX_DELAY);
  vTaskDelete(nullptr);
}

static void mqttHiveTask(void* pv) {
  (void)pv;
  xEventGroupWaitBits(modemEvents, BIT_DATA_READY, pdFALSE, pdTRUE,
                      portMAX_DELAY);
  vTaskDelete(nullptr);
}

static void testRunnerTask(void* pv) {
  (void)pv;
  xEventGroupWaitBits(modemEvents, BIT_DATA_READY, pdFALSE, pdTRUE,
                      portMAX_DELAY);

  for (;;) {
    xEventGroupWaitBits(modemEvents, BIT_RUN_TEST | BIT_CANCEL_TEST, pdTRUE,
                        pdFALSE, portMAX_DELAY);

    if (cancelRequested && currentTest == TestType::None) {
      logUsb("Cancel requested with no active test");
      cancelRequested = false;
      setTestState(false);
      continue;
    }

    if (currentTest != TestType::None || queuedTest == TestType::None) {
      continue;
    }

    currentTest = queuedTest;
    queuedTest = TestType::None;
    cancelRequested = false;
    setTestState(true);

    if (currentTest == TestType::Http) {
      logUsb("HTTP test (example.com)...");
      if (!cancelRequested && modem.http().get(kHttpTestUrl, 64)) {
        logUsb("HTTP OK");
      } else if (cancelRequested) {
        logUsb("HTTP test cancelled");
      } else {
        logUsb("HTTP failed");
      }
    } else if (currentTest == TestType::HttpDownload) {
      logUsb("HTTP download (OTA txt)...");
      bool canDownload = true;
      if (cancelRequested) {
        logUsb("HTTP download cancelled");
        canDownload = false;
      } else {
        if (!sdReadyForOta) {
          if (!sdMounted) {
            logUsb("[sd] not mounted");
            if (!recoverSd()) {
              logUsb("SD not ready for OTA");
              canDownload = false;
            }
          }
          if (!sdReadyForOta) {
            logUsb("[sd] mounted but not validated");
            if (!sdLightWriteProbe("/sdprobe.tmp")) {
              logUsb("[sd] validation failed");
              setSdState(sdMounted, false);
              logUsb("SD not ready for OTA");
              canDownload = false;
            }
            setSdState(true, true);
            logUsb("[sd] ready for ota");
          }
        }
      }
      if (!canDownload) {
        continue;
      }
      if (modem.http().downloadToFile(kHttpDownloadUrl,
                                             kHttpDownloadPath,
                                             kHttpDownloadChunkSize,
                                             logUsbSink, recoverSd,
                                             kSdFlushThreshold)) {
        logUsb("HTTP download OK");
        finalizeSdAfterOta();
      } else {
        logUsb("HTTP download failed");
      }
    } else if (currentTest == TestType::OtaInstall) {
      logUsb("OTA install from SD...");
      if (cancelRequested) {
        logUsb("OTA install cancelled");
      } else if (!sdLightWriteProbe("/sdprobe.tmp")) {
        logUsb("[sd] light probe failed");
        if (!recoverSd()) {
          logUsb("SD not ready");
          continue;
        }
      } else if (ota.installFromSd(kHttpDownloadPath)) {
        logUsb("OTA install OK");
      } else {
        logUsb("OTA install failed");
      }
    } else if (currentTest == TestType::Mqtt) {
      String payload =
          String("{\"device\":\"esp001\",\"msg\":\"hello\",\"ts\":") +
          millis() + "}";
      logUsb("MQTT connect...");
      if (!cancelRequested &&
          modem.mqtt().ensureConnected(kMqttHost, kMqttPort, kMqttClientId, 3,
                                       2000, kMqttUser, kMqttPass)) {
        logUsb("MQTT connected");
        if (!cancelRequested && modem.mqtt().publishJson(
                                    kMqttTopic, payload.c_str(), 1, false)) {
          logUsb("MQTT publish OK");
        } else if (cancelRequested) {
          logUsb("MQTT publish cancelled");
        } else {
          logUsb("MQTT publish failed");
        }
      } else if (cancelRequested) {
        logUsb("MQTT connect cancelled");
      } else {
        logUsb("MQTT connect failed");
      }
      modem.mqtt().disconnect();
    } else if (currentTest == TestType::UbidotsPublish) {
      logUsb("Ubidots publish...");
      float value = static_cast<float>(random(0, 101));
      String topic = kUbiTopicPub;
      String payload =
          String("{\"") + "test-in" + "\":" + String(value, 2) + "}";
      bool connected =
          modem.mqtt().connect(kUbiHost, kUbiPort, kMqttClientId, kUbiToken,
                               kUbiToken, true);
      if (!cancelRequested && connected &&
          modem.mqtt().publish(topic.c_str(), payload.c_str(), 1, false)) {
        logUsb("Ubidots OK");
      } else if (cancelRequested) {
        logUsb("Ubidots publish cancelled");
      } else {
        logUsb("Ubidots failed");
      }
      modem.mqtt().disconnect();
    } else if (currentTest == TestType::UbidotsSubscribe) {
      logUsb("Ubidots subscribe...");
      String topic = kUbiTopicSub;
      bool connected =
          modem.mqtt().connect(kUbiHost, kUbiPort, kMqttClientId, kUbiToken,
                               kUbiToken, true);
      if (!cancelRequested && connected) {
        if (!cancelRequested && modem.mqtt().subscribe(topic.c_str(), 1)) {
          logUsb("Ubidots subscribe ok");
        } else if (cancelRequested) {
          logUsb("Ubidots subscribe cancelled");
        } else {
          logUsb("Ubidots subscribe failed");
        }

        while (!cancelRequested) {
          String incomingTopic;
          String value;
          if (modem.mqtt().pollIncoming(incomingTopic, value) &&
              incomingTopic == topic) {
            logUsb(String("Ubidots value: ") + value);
          }
          vTaskDelay(pdMS_TO_TICKS(300));
        }
        modem.mqtt().disconnect();
      } else if (cancelRequested) {
        logUsb("Ubidots connect cancelled");
      } else {
        logUsb("Ubidots connect failed");
      }
    }

    currentTest = TestType::None;
    cancelRequested = false;
    setTestState(false);

    if (queuedTest != TestType::None) {
      xEventGroupSetBits(modemEvents, BIT_RUN_TEST);
    }
  }
}

void setup() {
  Serial.begin(115200);
  if (!initSdWithRetry("boot", SdProbeMode::Light)) {
    logUsb("SD init failed");
  }
  uint32_t expectedSize = 0;
  uint32_t writtenBytes = 0;
  uint8_t downloadState = getOtaDownloadState(&expectedSize, &writtenBytes);
  if (isOtaRecoveryPending() || downloadState == OtaStateDownloading ||
      downloadState == OtaStateValidating) {
    otaSdReady = recoverSd();
    setOtaRecoveryPending(false);
    setOtaDownloadState(OtaStateIdle, 0, 0);
  }
  setupWifi();
  randomSeed(millis());

  ota.begin();

  dashboard.begin(server, ws);
  dashboard.setCommandHandler(handleDashboardCommand);
  server.begin();
  ws.begin();

  modemEvents = xEventGroupCreate();

  xTaskCreatePinnedToCore(modemCoreTask, "modemCoreTask", 8192, nullptr, 2,
                          nullptr, 1);
  xTaskCreatePinnedToCore(testRunnerTask, "testRunnerTask", 12288, nullptr, 1,
                          nullptr, 1);
}

void loop() {
  server.handleClient();
  ota.handle();
  dashboard.loop();

  readSerialLines(Serial, usbLine, DashboardSource::Usb);
}
