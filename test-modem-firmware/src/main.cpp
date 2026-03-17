#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "dashboard.h"
#include "modem_manager.h"

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
static ModemMqtt mqtt(modem);

static String usbLine;
static volatile bool modemInUse = false;
static volatile bool modemTaskDone = false;

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
  if (mqtt.publishToUbidots("BBUS-orL2zH4XNEKC0880tXUcuxdTWpX5R8",
                            "aqcuicola-001", "test-in", 25.4f)) {
    logUsb("Ubidots OK");
  } else {
    logUsb("Ubidots failed");
  }
}

static void ubidotsTask(void* pv) {
  (void)pv;

  while (!modemTaskDone) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  bool publishedOnce = false;
  bool subscribed = false;

  for (;;) {
    if (!mqtt.isConnected()) {
      logUsb("Ubidots MQTT connect...");
      if (mqtt.connect("industrial.api.ubidots.com", 8883, "esp001", 3, 2000,
                       kUbiToken, kUbiToken)) {
        logUsb("Ubidots MQTT connected");
        subscribed = false;
        publishedOnce = false;
      } else {
        logUsb("Ubidots MQTT connect failed");
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }
    }

    if (!subscribed) {
      if (mqtt.subscribeToUbidots(kUbiDevice, kUbiVariable)) {
        logUsb("Ubidots subscribe ok");
        subscribed = true;
      } else {
        logUsb("Ubidots subscribe failed");
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
    }

    if (subscribed && !publishedOnce) {
      float value = static_cast<float>(random(0, 101));
      String topic = String("/v1.6/devices/") + kUbiDevice;
      String payload = String("{\"test-in\":") + String(value, 2) + "}";
      if (mqtt.publishJson(topic.c_str(), payload.c_str(), 1, false)) {
        logUsb(String("Ubidots publish ok: ") + payload);
      } else {
        logUsb("Ubidots publish failed");
      }
      publishedOnce = true;
    }

    mqtt.pollIncomingUbidots(kUbiDevice, kUbiVariable);
    vTaskDelay(pdMS_TO_TICKS(300));
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

static void modemTask(void* pv) {
  (void)pv;

  modemInUse = true;
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
    modemInUse = false;
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
  if (modem.connectGprs()) {
    logUsb("Data session ready");
  } else {
    logUsb("Data session failed");
  }


  // logUsb("HTTP test (example.com)...");
  // if (modem.httpGetNative("http://example.com/", 64)) {
  //   logUsb("HTTP OK");
  // } else {
  //   logUsb("HTTP failed");
  // }

  // String payload = String("{\"device\":\"esp001\",\"msg\":\"hello\",\"ts\":") +
  //                  millis() + "}";
  // logUsb("MQTT connect...");
  // if (mqtt.connect("b282c2526e92497b9e5d5741f7483e22.s1.eu.hivemq.cloud", 8883,
  //                  "esp001", 3, 2000, "testesp001", "Testesp001+")) {
  //   logUsb("MQTT connected");
  //   if (mqtt.publishJson("esp001", payload.c_str(), 1, false)) {
  //     logUsb("MQTT publish OK");
  //   } else {
  //     logUsb("MQTT publish failed");
  //   }
  //   mqtt.disconnect();
  // } else {
  //   logUsb("MQTT connect failed");
  // }

  publishUbidotsTest();

  modemInUse = false;
  modemTaskDone = true;
  vTaskDelete(nullptr);
}

void setup() {
  Serial.begin(115200);
  setupWifi();
  randomSeed(millis());

  dashboard.begin(server, ws);
  server.begin();
  ws.begin();

  xTaskCreatePinnedToCore(modemTask, "modemTask", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(ubidotsTask, "ubidotsTask", 8192, nullptr, 1, nullptr,
                          1);
}

void loop() {
  server.handleClient();
  dashboard.loop();

  readSerialLines(Serial, usbLine, DashboardSource::Usb);
}
