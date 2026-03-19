#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include <modem_manager.h>

static const char kApn[] = "internet.comcel.com.co";
static const char kUser[] = "comcel";
static const char kPass[] = "comcel";

static const int8_t kPinTx = 26;
static const int8_t kPinRx = 27;
static const int8_t kPwrKey = 4;

#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 14
#define SD_CS 13

static const char kTestUrl[] =
    "https://raw.githubusercontent.com/MateoDelG/tests-ota-esp/refs/heads/master/"
    "test-ota.txt";
static const char kTestPath[] = "/test-ota.txt";
static const size_t kPreviewBytes = 128;

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
  nullptr,
};

static ModemManager modem(modemConfig);

static void logUsb(bool isTx, const String& line) {
  (void)isTx;
  Serial.println(String("[USB] ") + line);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD init failed");
    return;
  }
  Serial.println("SD init OK");

  if (!modem.begin()) {
    Serial.println("Modem begin failed");
    return;
  }
  if (!modem.ensureDataSession()) {
    Serial.println("Data session failed");
    return;
  }

  Serial.println("Downloading file...");
  if (!modem.http().downloadToFile(kTestUrl, kTestPath, 64)) {
    Serial.println("Download failed");
    return;
  }
  Serial.println("Download OK");

  int expectedSize = modem.http().lastHttpLength();
  if (expectedSize <= 0) {
    Serial.println("Verify failed: missing expected size");
    return;
  }
  modem.http().verifyFileOnSd(kTestPath, static_cast<size_t>(expectedSize),
                              kPreviewBytes, logUsb);
}

void loop() {}
