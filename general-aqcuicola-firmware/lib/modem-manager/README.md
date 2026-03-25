# modem-manager

Libreria para ESP32 + SIMCom A7670SA-FASE (Arduino) basada en TinyGSM.
Se enfoca en un flujo robusto con URCs tardias y soporte para HTTP y MQTT TLS
generico, manteniendo logs detallados del trafico AT.

## Proposito

- Inicializar y controlar el modem A7670SA-FASE.
- Registrar red LTE, abrir sesion de datos y validar IP.
- Ejecutar HTTP con URCs tardias.
- Ejecutar MQTT TLS con URCs tardias.
- Soportar cualquier broker MQTT (Ubidots, HiveMQ, etc).

## Arquitectura de modulos

- `modem_manager`
  - Fachada principal y API publica.
- `modem_at`
  - Utilidades AT y espera de URCs.
- `modem_urc`
  - Almacen de URCs (UrcStore).
- `modem_parsers`
  - Parsers puros de respuestas.
- `modem_core`
  - Power, init, SIM, red, info basica.
- `modem_data_session`
  - APN, CGATT, CGPADDR, NETOPEN.
- `modem_http`
  - HTTPINIT, HTTPACTION, HTTPREAD.
- `modem_mqtt`
  - CMQTTSTART/ACCQ/CONNECT/PUB/SUB/DISCONNECT.
- `modem_tap_stream`
  - Tap de trafico AT para logs.
- `modem_types`
  - Tipos y configuracion base.

## Flujo de inicializacion

1. `begin()`
2. Power on + serial
3. `ATV1` y `CMEE=2`
4. `waitForNetwork()`

## Flujo de sesion de datos

1. `ensureDataSession()`
2. `CGATT` + `CGPADDR`
3. `NETOPEN` + URC `+NETOPEN: 1`

## Flujo HTTP

1. `HTTPINIT`
2. `HTTPPARA="URL"`
3. `HTTPACTION=0`
4. URC `+HTTPACTION: <status>,<len>`
5. `HTTPREAD`
6. `HTTPTERM`

## Flujo MQTT TLS

1. `CMQTTSTART`
2. `CMQTTACCQ`
3. `CSSLCFG` + `CMQTTSSLCFG`
4. `CMQTTCONNECT` (URC `+CMQTTCONNECT: 0,0`)
5. `CMQTTTOPIC` + `CMQTTPAYLOAD`
6. `CMQTTPUB` (URC `+CMQTTPUB: 0,0`)
7. `CMQTTSUBTOPIC` + `CMQTTSUB` (URC `+CMQTTSUB: 0,0`)
8. `CMQTTDISC/REL/STOP`

## Ejemplo MQTT (Ubidots via API generica)

```cpp
ModemManager modem(modemConfig);

if (!modem.begin()) return;
if (!modem.ensureDataSession()) return;

const char* host = "industrial.api.ubidots.com";
const uint16_t port = 8883;
const char* token = "TOKEN";
const char* clientId = "esp001";

if (!modem.mqtt().connect(host, port, clientId, token, token, true)) return;

String topic = "/v1.6/devices/device";
String payload = "{\"temperature\":24.5}";
modem.mqtt().publish(topic.c_str(), payload.c_str(), 1, false);

String subTopic = "/v1.6/devices/device/test-out/lv";
modem.mqtt().subscribe(subTopic.c_str(), 1);

for (;;) {
  String rxTopic;
  String value;
  if (modem.mqtt().pollIncoming(rxTopic, value) && rxTopic == subTopic) {
    Serial.println(value);
  }
  delay(300);
}
```

## URCs tardias

El modem puede responder con `OK` primero y luego enviar URCs mas tarde.
Por eso:

- Las operaciones criticas esperan URC especifica.
- Los timeouts estan centralizados en `modem_at`.
- `UrcStore` almacena URCs para consumo posterior.

## Logging y TapStream

- `ModemConfig.enableLogs` habilita logs.
- `ModemTapStream` permite ver trafico AT (TX/RX).
- Si una linea es muy larga, se marca como `[truncated]`.

## Ejemplo minimo (boot + data)

```cpp
#include <Arduino.h>
#include <modem_manager.h>

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
  nullptr,
};

ModemManager modem(modemConfig);

void setup() {
  Serial.begin(115200);

  if (!modem.begin()) return;
  if (!modem.ensureDataSession()) return;
}

void loop() {}
```

## Notas

- Tipo de modem en `modem_manager_config.h`.
- Para A7670SA, usar `TINY_GSM_MODEM_A7672X`.
