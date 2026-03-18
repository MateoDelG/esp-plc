#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "dashboard.h"
#include "modem_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char kApn[] = "internet.comcel.com.co";
static const char kUser[] = "comcel";
static const char kPass[] = "comcel";

static const int8_t kPinTx = 26;
static const int8_t kPinRx = 27;
static const int8_t kPwrKey = 4;

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

static ModemManager modem(modemConfig);

static String usbLine;

static EventGroupHandle_t modemEvents;
static const EventBits_t BIT_MODEM_READY = 1 << 0;
static const EventBits_t BIT_DATA_READY = 1 << 1;
static const EventBits_t BIT_RUN_TEST = 1 << 2;
static const EventBits_t BIT_CANCEL_TEST = 1 << 3;

enum class TestType : uint8_t {
  None = 0,
  Http,
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

static void logModem(const String& line) {
  dashboard.pushLine(DashboardSource::Modem, line);
}

static void publishUbidotsTest() {
  logUsb("Ubidots publish...");
  if (modem.ubidots().publishValue(kUbiToken, kUbiDevice, "test-in", 25.4f)) {
    logUsb("Ubidots OK");
  } else {
    logUsb("Ubidots failed");
  }
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

  if (cmd == "run_http") {
    logUsb("Dashboard: run HTTP test");
    requestTest(TestType::Http);
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

static void ubidotsTask(void* pv) {
  (void)pv;

  xEventGroupWaitBits(modemEvents, BIT_DATA_READY, pdFALSE, pdTRUE,
                      portMAX_DELAY);
  vTaskDelete(nullptr);
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
      if (!cancelRequested && modem.http().get("http://example.com/", 64)) {
        logUsb("HTTP OK");
      } else if (cancelRequested) {
        logUsb("HTTP test cancelled");
      } else {
        logUsb("HTTP failed");
      }
    } else if (currentTest == TestType::Mqtt) {
      String payload =
          String("{\"device\":\"esp001\",\"msg\":\"hello\",\"ts\":") +
          millis() + "}";
      logUsb("MQTT connect...");
      if (!cancelRequested &&
          modem.mqtt().ensureConnected(
              "b282c2526e92497b9e5d5741f7483e22.s1.eu.hivemq.cloud", 8883,
              "esp001", 3, 2000, "testesp001", "Testesp001+")) {
        logUsb("MQTT connected");
        if (!cancelRequested &&
            modem.mqtt().publishJson("esp001", payload.c_str(), 1, false)) {
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
      if (!cancelRequested &&
          modem.ubidots().publishValue(kUbiToken, kUbiDevice, "test-in", value,
                                       true)) {
        logUsb("Ubidots OK");
      } else if (cancelRequested) {
        logUsb("Ubidots publish cancelled");
      } else {
        logUsb("Ubidots failed");
      }
    } else if (currentTest == TestType::UbidotsSubscribe) {
      logUsb("Ubidots subscribe...");
      if (!cancelRequested && modem.ubidots().connect(kUbiToken, "esp001")) {
        if (!cancelRequested &&
            modem.ubidots().subscribeVariable(kUbiDevice, kUbiVariable)) {
          logUsb("Ubidots subscribe ok");
        } else if (cancelRequested) {
          logUsb("Ubidots subscribe cancelled");
        } else {
          logUsb("Ubidots subscribe failed");
        }

        while (!cancelRequested) {
          String value;
          modem.ubidots().pollVariable(kUbiDevice, kUbiVariable, value);
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
  setupWifi();
  randomSeed(millis());

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
  dashboard.loop();

  readSerialLines(Serial, usbLine, DashboardSource::Usb);
}
