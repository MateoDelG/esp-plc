# modem-manager

Lightweight TinyGSM wrapper for SIMCOM A7670SA on ESP32.

## Example (boot + network info)

```cpp
#include <Arduino.h>
#include <modem_manager.h>

using namespace ModemManager;

static const char kApn[] = "internet.comcel.com.co";
static const char kUser[] = "comcel";
static const char kPass[] = "comcel";

static const int8_t kPinTx = 26;
static const int8_t kPinRx = 27;
static const int8_t kPwrKey = 4;

ModemConfig modemConfig = {
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
};

ModemManager modem(modemConfig);

void setup() {
  Serial.begin(115200);

  if (!modem.begin()) {
    Serial.println("Modem init failed");
    return;
  }

  ModemInfo info = modem.getNetworkInfo();
  Serial.println("=== Modem info ===");
  Serial.println(info.modemName);
  Serial.println(info.modemInfo);
  Serial.println(info.imei);
  Serial.println(info.iccid);
  Serial.println(info.operatorName);
  Serial.println(info.localIp);
  Serial.println(info.cpsi);
  Serial.print("CSQ: ");
  Serial.print(info.signalQuality);
  Serial.print(" => ");
  Serial.print(info.signalPercent);
  Serial.println("%");

  if (modem.connectGprs()) {
    Serial.println("GPRS connected");
  } else {
    Serial.println("GPRS connect failed");
  }
}

void loop() {}
```

## Notes

- Modem type is set in `modem_manager_config.h`.
- For A7670SA, default is `TINY_GSM_MODEM_A7672X`.
- Logs are controlled by `ModemConfig.enableLogs`.
