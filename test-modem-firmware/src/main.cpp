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
static const uint32_t kSdSpiClock = 4000000;
static const uint32_t kSdFlushThreshold = 32768;

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

static bool sdQuickWriteProbe(const char* path) {
  logUsb("[sd] quick probe start");
  SD.remove(path);

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    logUsb("[sd] quick probe open fail");
    return false;
  }
  logUsb("[sd] quick probe open ok");

  size_t written = file.write(reinterpret_cast<const uint8_t*>("test"), 4);
  if (written != 4) {
    logUsb("[sd] quick probe write fail");
    file.close();
    SD.remove(path);
    return false;
  }
  logUsb("[sd] quick probe write ok");

  file.flush();
  logUsb("[sd] quick probe flush ok");
  file.close();
  logUsb("[sd] quick probe close ok");

  bool cleanupOk = SD.remove(path);
  logUsb(String("[sd] quick probe cleanup ") + (cleanupOk ? "ok" : "fail"));
  return true;
}

static bool sdStrictReadbackProbe(const char* path) {
  File file = SD.open(path, FILE_READ);
  if (!file) {
    logUsb("[sd] strict probe reopen read fail");
    return false;
  }

  size_t size = file.size();
  logUsb(String("[sd] strict probe size: ") + size);
  if (size == 0) {
    file.close();
    return false;
  }

  char buffer[5] = {0};
  size_t readCount = file.readBytes(buffer, 4);
  bool readbackOk = (readCount == 4 && strncmp(buffer, "test", 4) == 0);
  logUsb(String("[sd] strict probe readback ") + (readbackOk ? "ok" : "fail"));
  file.close();
  return readbackOk;
}

static void purgeOtaArtifacts() {
  logUsb("[sd] purge ota artifacts");
  const char* kArtifacts[] = {
      "/fw.part",
      "/firmware.bin.tmp",
      "/firmware.bak",
      "/sdtest.tmp",
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
  if (!sdQuickWriteProbe("/sdtest.tmp")) {
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
  SD.end();
  logUsb("[sd] recovery spi end");
  SPI.end();
  delay(150);
  SPI.begin(kSdSclk, kSdMiso, kSdMosi, kSdCs);
  logUsb("[sd] recovery spi begin");
  delay(100);
  if (SD.begin(kSdCs, SPI, kSdSpiClock)) {
    logUsb("[sd] recovery sd begin ok");
    delay(100);
    purgeOtaArtifacts();
    if (!sdQuickWriteProbe("/sdtest.tmp")) {
      logUsb("[sd] recovery probe retry");
      if (sdQuickWriteProbe("/sdtest.tmp")) {
        logUsb("[sd] recovery ok");
        return true;
      }
    } else {
      logUsb("[sd] recovery ok");
      return true;
    }
  } else {
    logUsb("[sd] recovery sd begin fail");
  }
  logUsb("[sd] recovery failed");
  return false;
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
      if (cancelRequested) {
        logUsb("HTTP download cancelled");
      } else if (modem.http().downloadToFile(kHttpDownloadUrl,
                                             kHttpDownloadPath,
                                             kHttpDownloadChunkSize,
                                             logUsbSink, recoverSd,
                                             kSdFlushThreshold)) {
        logUsb("HTTP download OK");
      } else {
        logUsb("HTTP download failed");
      }
    } else if (currentTest == TestType::OtaInstall) {
      logUsb("OTA install from SD...");
      if (cancelRequested) {
        logUsb("OTA install cancelled");
      } else if (!sdQuickWriteProbe("/sdtest.tmp")) {
        logUsb("[sd] quick probe failed");
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
  SPI.begin(kSdSclk, kSdMiso, kSdMosi, kSdCs);
  if (SD.begin(kSdCs, SPI, kSdSpiClock)) {
    logUsb("SD init OK");
  } else {
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
