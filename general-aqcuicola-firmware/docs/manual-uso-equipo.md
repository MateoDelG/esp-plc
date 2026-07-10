# Manual de Uso del Equipo - General Acuicola

## 1) Objetivo y alcance

Este manual describe como operar el equipo en campo y como dar soporte tecnico.
Incluye uso diario, comandos disponibles, operacion remota (MQTT/SMS) y
resolucion de problemas frecuentes.

---

## 2) Guia rapida para operador (no tecnico)

### Encendido y acceso

1. Energiza el equipo y espera entre 30 y 60 segundos.
2. Si el equipo se conecta al WiFi de la planta, abre en navegador:
   - `http://192.168.1.180/`
3. Si no hay red disponible, el equipo crea un WiFi propio:
   - SSID: `Aquaculture control`
   - Clave: `12345678`
   - Luego abre: `http://192.168.4.1/`

### Que revisar en operacion normal

- Valores de pH, O2, temperatura y nivel actualizando cada pocos segundos.
- Estado de sopladores (`ON/OFF`) sin alertas de umbral.
- Consola web en estado `connected`.

### Acciones basicas permitidas

- Ajustar umbrales de sopladores y demora de aviso.
- Forzar consulta UART (`get_status`, `get_last`) desde botones del panel.
- Revisar y limpiar logs SD desde la seccion SD.

### Si algo falla (pasos simples)

- Si no abre el panel: reconectar a la red del equipo (AP) y usar `192.168.4.1`.
- Si hay datos en `--`: esperar 1 minuto y refrescar.
- Si sigue igual: reiniciar alimentacion del equipo.
- Si persiste: reportar a soporte tecnico con foto de la consola web.

---

## 3) Puesta en marcha tecnica

### Parametros de red y acceso web

- Modo STA con IP estatica:
  - IP: `192.168.1.180`
  - Gateway/DNS: `192.168.1.1`
- Fallback AP:
  - SSID: `Aquaculture control`
  - Password: `12345678`
  - IP AP: `192.168.4.1`
- Dashboard HTTP: puerto `80`
- Consola en tiempo real (WebSocket): puerto `81`

### Flujo de inicializacion (resumen)

1. Logger + watchdog.
2. Carga configuracion persistente (NVS Preferences).
3. Conexion WiFi (o AP fallback).
4. OTA local por ArduinoOTA.
5. Inicializacion Ubidots/modem y tareas de comunicacion.
6. Servicios de tiempo, telemetria, SD logger, ESP-NOW, UART y consola.

---

## 4) Operacion remota por MQTT y SMS (seccion principal)

## 4.1 MQTT (Ubidots)

### Conexion

- Host: `industrial.api.ubidots.com`
- Puerto: `8883`
- TLS: habilitado
- Device label por defecto: `aqcuicola-001`

### Topicos usados por el firmware

- Publicacion de datos del equipo:
  - `/v1.6/devices/<deviceLabel>`
- Recepcion de comandos por consola:
  - `/v1.6/devices/<deviceLabel>/console/lv`

### Comando remoto de OTA por MQTT

Enviar en la variable `console` el valor `201` para disparar OTA por modem.

Ejemplo de payload esperado por el firmware:

```json
{"value":201}
```

El firmware parsea el valor numerico del payload recibido en
`/console/lv` y ejecuta OTA cuando detecta `201`.

### Codigos de estado publicados por el equipo

- `200`: OTA por modem terminada correctamente (reinicio inminente).
- `299`: OTA por modem fallo.
- `999`: recibido comando SMS `RESET`, reinicio inminente.
- `202`: recibido comando SMS `UPDATE`, inicio de OTA por modem.

## 4.2 SMS

Los comandos SMS se procesan sin distinguir mayusculas/minusculas.

- `RESET`
  - Efecto: reinicia el ESP.
  - Antes del reinicio publica `999` en consola Ubidots.

- `UPDATE`
  - Efecto: inicia OTA por modem.
  - Antes de iniciar publica `202` en consola Ubidots.

### Buenas practicas para operacion remota

- Enviar un solo comando y esperar confirmacion/codigo.
- No enviar `RESET` durante una OTA en curso.
- Si se recibe `299`, validar red celular, sesion de datos y URL del binario OTA.

---

## 5) Comandos HTTP utiles (soporte tecnico)

Base URL local: `http://192.168.1.180` (o `http://192.168.4.1` en AP).

### Lectura de estado

- `GET /api/dashboard`
- `GET /api/analog`
- `GET /api/blowers`
- `GET /api/wifi`
- `GET /api/time`
- `GET /api/wdt`
- `GET /api/pcf/state`
- `GET /api/espnow/config`
- `GET /api/ubidots/interval`
- `GET /api/uart/auto`
- `GET /api/sd/logs?type=uart`
- `GET /api/sd/logs?type=level`

### Comandos de control

- UART:
  - `POST /api/uart/cmd?op=get_status`
  - `POST /api/uart/cmd?op=get_last`
  - `POST /api/uart/cmd?op=auto_measure`
- Analog enable mask:
  - `POST /api/analog/enable` body `{"enabledMask":3}`
- Blowers:
  - `POST /api/blowers` body `{"a0":0.55,"a1":0.55,"delaySec":10,"alarmEnabled":1}`
- WiFi:
  - `POST /api/wifi` body `{"ssid":"MI_RED","pass":"MI_CLAVE","autoReconnect":1}`
- Watchdog:
  - `POST /api/wdt` body `{"swSec":60,"hwSec":90}`
- Intervalo Ubidots:
  - `POST /api/ubidots/interval` body `{"intervalMin":5}`
- ESP-NOW:
  - `POST /api/espnow/config` body `{"tank":1,"mac":"AA:BB:CC:DD:EE:FF"}`
  - `POST /api/espnow/request` body `{"tank":1}`
  - `POST /api/espnow/auto` body `{"enabled":1,"intervalMin":5}`
- PCF8574:
  - `POST /api/pcf/do` body `{"pin":0,"value":1}`
- SD logs:
  - `POST /api/sd/clear`

Ejemplo rapido con `curl`:

```bash
curl -X POST "http://192.168.1.180/api/uart/cmd?op=get_last"
curl -X POST "http://192.168.1.180/api/ubidots/interval" -H "Content-Type: application/json" -d "{\"intervalMin\":5}"
```

---

## 6) Problemas comunes y soluciones

### 1) No conecta a WiFi de planta

Sintomas:
- No responde en `192.168.1.180`.
- Aparece red `Aquaculture control`.

Acciones:
1. Conectarse al AP del equipo y entrar a `http://192.168.4.1/`.
2. Revisar/corregir SSID y clave en seccion WiFi.
3. Verificar opcion `autoReconnect` activa.
4. Guardar y esperar reconexion.

### 2) Dashboard abre pero consola dice `disconnected`

Causa probable:
- Bloqueo del WebSocket (puerto 81) por red/firewall.

Acciones:
1. Probar desde misma subred del equipo.
2. Permitir trafico al puerto `81`.
3. Refrescar pagina y confirmar estado `connected`.

### 3) No sube datos a nube

Sintomas:
- Telemetria local ok, nube sin actualizar.

Acciones:
1. Verificar cobertura SIM y APN (`internet.comcel.com.co`).
2. Confirmar estado de modem y suscripcion de consola.
3. Revisar que intervalo Ubidots no sea excesivo.
4. Reiniciar equipo si el enlace MQTT quedo inestable.

### 4) OTA remota devuelve fallo (`299`)

Acciones:
1. Verificar sesion de datos del modem.
2. Verificar que la URL OTA configurada sea accesible por red celular.
3. Reintentar una sola vez.
4. Si persiste, actualizar por metodo local y revisar logs de modem.

### 5) SD logger no guarda

Sintomas:
- Seccion SD sin lineas nuevas.

Acciones:
1. Revisar tarjeta SD y formato.
2. Verificar inicializacion (`sd: ready`) en consola.
3. Evitar retirar SD con equipo energizado.

### 6) UART o ESP-NOW sin datos

Acciones:
1. Probar comando `get_status` por UI/API.
2. Revisar direccionamiento MAC en ESP-NOW.
3. Ejecutar escaneo I2C y validar perifericos.
4. Revisar cableado/pines del hardware asociado.

---

## 7) Comandos de soporte (firmware)

Ejecutar desde la carpeta del proyecto `general-aqcuicola-firmware`.

- Compilar:
  - `pio run -e esp-wrover-kit`
- Subir firmware:
  - `pio run -e esp-wrover-kit -t upload`
- Monitor serial:
  - `pio device monitor -b 115200`
- Limpieza build:
  - `pio run -t clean`
- Analisis opcional:
  - `pio check -e esp-wrover-kit`

---

## 8) Checklist post-intervencion

1. Panel responde por HTTP.
2. Consola WebSocket en `connected`.
3. Variables principales actualizan (pH/O2/temp/nivel).
4. Estado de sopladores coherente.
5. Si hubo trabajo remoto: comando MQTT/SMS confirmado con codigo esperado.
