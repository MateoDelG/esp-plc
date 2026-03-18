# AGENTS.md

## Propósito del proyecto

Este proyecto implementa una librería y aplicación para **ESP32 + SIMCom A7670SA-FASE** usando **Arduino** y **TinyGSM**, con soporte para:

- inicialización y control del módem
- registro en red LTE
- apertura y validación de sesión de datos
- HTTP
- MQTT sobre TLS
- integración con HiveMQ Cloud
- integración con Ubidots
- publicación y suscripción MQTT
- logging detallado del tráfico AT para depuración

El objetivo principal es mantener una base **robusta, legible y extensible**, sin romper el comportamiento ya validado en campo.

---

## Estado funcional actual

Lo siguiente ya funciona y debe preservarse:

- `begin()` inicializa correctamente el módem
- el cambio de `ATV0` a `ATV1` evita problemas de parsing
- la validación de red y datos usando:
  - `AT+CGATT?`
  - `AT+CGPADDR=1`
  - `AT+NETOPEN?`
- `NETOPEN` ya se maneja correctamente
- HTTP ya funciona con `HTTPACTION`
- MQTT TLS ya funciona con HiveMQ Cloud
- Ubidots publish ya funciona
- Ubidots subscribe ya funciona
- las URCs tardías son reales y deben manejarse cuidadosamente
- el `TapStream` actual permite ver el tráfico AT y no debe romperse

---

## Reglas clave para cualquier cambio

### 1. No romper lo que ya funciona
Antes de modificar:
- revisar si el comportamiento actual ya fue validado por log real
- evitar “simplificaciones” que eliminen manejo de URCs tardías
- no asumir que `OK` implica que la operación ya terminó completamente

### 2. Tratar URCs como asíncronas
Muchas respuestas del módem llegan en dos fases:
1. `OK`
2. una URC posterior, por ejemplo:
   - `+NETOPEN: ...`
   - `+HTTPACTION: ...`
   - `+CMQTTCONNECT: ...`
   - `+CMQTTPUB: ...`
   - `+CMQTTSUB: ...`
   - `+CMQTTRX...`

Nunca asumir que `waitResponse()` capturó todo en una sola llamada.

### 3. No reintentar acciones exitosas
Evitar relanzar:
- `CMQTTSTART` si ya arrancó
- `CMQTTCONNECT` si ya conectó
- `CMQTTPUB` si ya publicó
- `NETOPEN` si ya está abierto

Muchas fallas previas vinieron de reintentos innecesarios después de un éxito que no fue reconocido correctamente por el código.

### 4. Mantener compatibilidad con Arduino
No introducir dependencias innecesarias.
El código debe seguir funcionando en entorno Arduino / ESP32.

---

## Arquitectura deseada

La librería debe evolucionar hacia una organización por responsabilidades:

- `ModemCore`
  - power
  - init
  - SIM
  - registro
  - señal
- `ModemDataSession`
  - APN
  - CGATT
  - CGPADDR
  - NETOPEN
- `ModemHttp`
  - HTTPINIT
  - HTTPACTION
  - HTTPREAD
  - HTTPTERM
- `ModemMqtt`
  - CMQTTSTART
  - CMQTTACCQ
  - CMQTTCONNECT
  - publish
  - subscribe
  - disconnect
- `ModemUbidots`
  - publish de variables
  - subscribe de variables
  - parseo de topics/payloads de Ubidots
- `ModemParsers`
  - funciones puras para parsear respuestas

Mientras esa separación no esté completa, mantener una estructura interna clara y minimizar duplicación.

---

## Convenciones de implementación

### Nombres
Preferir nombres orientados a intención:

- `ensureDataSession()`
- `hasDataSession()`
- `ensureNetOpen()`
- `httpGet()`
- `ensureMqttStarted()`
- `ensureMqttConnected()`

Evitar nombres ambiguos o demasiado ligados a una implementación vieja.

### Logs
Usar prefijos consistentes:

- `[core]`
- `[data]`
- `[http]`
- `[mqtt]`
- `[ubidots]`

Mantener `logLine()` y `logValue()` como interfaz principal.

### Timeouts
No quemar timeouts arbitrarios en muchos lugares.
Si se agregan nuevos, intentar centralizarlos o al menos documentarlos claramente.

### Errores
Cuando una función falle:
- dejar log útil
- conservar la respuesta raw si es valiosa
- no ocultar un `ERROR` del módem si ayuda a depurar

---

## Reglas específicas del módem A7670SA

### Modo de respuestas
El módem pasa por `ATV0` al inicio, pero el proyecto debe forzar `ATV1` después de inicializar para evitar problemas de parsing.

### Sesión de datos
La validez de la sesión de datos debe evaluarse con:
- `CGATT == 1`
- IP válida en `CGPADDR`
- `NETOPEN == 1`

No basta solo con tener IP.

### MQTT TLS
Para MQTT con TLS:
- configurar `CSSLCFG`
- usar `enableSNI`
- asociar contexto SSL con `CMQTTSSLCFG`
- no asumir que `CMQTTCONNECT` queda validado solo con `OK`
- esperar explícitamente `+CMQTTCONNECT: 0,0`

### Ubidots
Para Ubidots:
- broker: `industrial.api.ubidots.com`
- puerto: `8883`
- usar TLS
- el token se usa como credencial MQTT
- publish:
  - topic: `/v1.6/devices/<deviceLabel>`
  - payload JSON: `{"<variableLabel>":<value>}`
- subscribe:
  - topic esperado: `/v1.6/devices/<deviceLabel>/<variableLabel>/lv`

### Payloads con prompt `>`
Los comandos:
- `CMQTTTOPIC`
- `CMQTTPAYLOAD`
- `CMQTTSUBTOPIC`

requieren:
1. esperar prompt `>`
2. enviar el contenido exacto
3. esperar `OK`

No mezclar eso con el siguiente comando antes de validar la aceptación del payload.

---

## Cosas que NO se deben hacer

- No eliminar el `TapStream` sin reemplazo equivalente
- No cambiar a una abstracción excesivamente compleja
- No introducir frameworks nuevos
- No asumir que un `ERROR` siempre significa fallo fatal, si la URC real indica éxito
- No limpiar logs “molestos” si son útiles para depuración del módem
- No reestructurar todo en una sola pasada sin preservar compatibilidad funcional

---

## Qué validar después de cualquier cambio

### Validación mínima obligatoria
1. El módem inicializa
2. Registra red LTE
3. Obtiene IP
4. `NETOPEN` llega a `1`
5. MQTT connect sigue funcionando
6. Publish MQTT sigue funcionando
7. Ubidots publish sigue funcionando
8. Ubidots subscribe sigue funcionando

### Señales esperadas en logs
Buscar explícitamente:
- `+CREG: 0,1`
- `+CGATT: 1`
- `+CGPADDR: 1,...`
- `+NETOPEN: 1`
- `+CMQTTCONNECT: 0,0`
- `+CMQTTPUB: 0,0`
- `+CMQTTSUB: 0,0`
- `+CMQTTRXTOPIC`
- `+CMQTTRXPAYLOAD`

---

## Estrategia recomendada para cambios

Cuando se haga una modificación importante:
1. cambiar una sola parte a la vez
2. probar con logs reales
3. confirmar que no se dañó:
   - datos
   - MQTT
   - Ubidots
4. solo después avanzar a otra capa

---

