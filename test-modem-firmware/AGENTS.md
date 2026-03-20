# AGENTS.md
## Proposito del repositorio
Firmware para ESP32 + SIMCom A7670SA-FASE (Arduino + TinyGSM). Incluye la app
principal y la libreria `modem-manager` con HTTP/MQTT/SSL y logging de trafico AT.

## Estructura rapida
- `src/` app principal (ESP32 + dashboard).
- `lib/modem-manager/` libreria propia del modem.
- `lib/` dependencias vendorizadas.
- `include/` headers globales (si aplica).
- `test/` tests para PlatformIO Test Runner (si se agregan).

## Alcance de cambios
- Preferir cambios en `src/` y `lib/modem-manager/`.
- Evitar editar librerias vendorizadas en `lib/` salvo necesidad real.
- Si se toca un vendor, documentar motivo y compatibilidad.

## Credenciales y datos sensibles
- No hardcodear tokens reales en commits.
- Mantener credenciales fuera del repo cuando sea posible.
- Si se usa ejemplo, usar valores de prueba claramente falsos.

## Comandos de build / lint / test
### PlatformIO (principal)
- Build: `pio run -e upesy_wrover`
- Upload: `pio run -e upesy_wrover -t upload`
- Monitor serial: `pio device monitor -b 115200`
- Clean: `pio run -t clean`

### Tests (PlatformIO)
- Ejecutar todos: `pio test -e upesy_wrover`
- Ejecutar un test (por nombre): `pio test -e upesy_wrover -f <test_name>`
- Ejecutar por patron: `pio test -e upesy_wrover -f test_*`

### Lint / formato
- No hay linter/formatter configurado en este repo.
- Si agregas uno, documenta el comando aqui y manten formato consistente.

## Reglas externas (Cursor/Copilot)
- No se encontraron reglas en `.cursor/rules/`, `.cursorrules` ni
  `.github/copilot-instructions.md`.

## Guias de estilo de codigo
### Formato y estructura
- Indentacion de 2 espacios.
- Llaves en la misma linea (`if (...) {`).
- Lineas largas: partir con `+`/`<<`/concatenacion clara y legible.
- Evitar macros complejas salvo necesidad (prefiere funciones).
- Mantener funciones cortas; dividir cuando mezclen varias responsabilidades.
- Agrupar helpers relacionados; evitar archivos monoliticos nuevos.

### Includes
- Orden sugerido:
  1) Arduino/SDK (`Arduino.h`, `WiFi.h`)
  2) Dependencias externas (`TinyGsmClient.h`)
  3) Headers locales (`modem_*.h`)
- Mantener guards `#ifndef ... #define ... #endif`.
- Evitar include circular; usar forward declarations donde aplique.

### Naming
- Clases en PascalCase: `ModemManager`, `ModemMqtt`.
- Metodos/funciones en lowerCamelCase: `ensureDataSession()`.
- Constantes de archivo con prefijo `k`: `kApn`, `kPinTx`.
- Enums con `enum class` y valores claros: `ModemState::DataReady`.
- Archivos y modulos en snake_case: `modem_data_session.cpp`.
- Booleanos con verbo: `isReady`, `hasIp`, `shouldRetry`.

### Tipos y memoria
- Usar tipos fijos (`uint32_t`, `int8_t`) para hardware/tiempos.
- Usar `String` y `const char*` como en el codigo existente.
- Evitar asignaciones dinamicas innecesarias en loops criticos.
- Evitar grandes buffers en stack si pueden vivir en static.
- Preferir `static` para buffers reutilizados en operaciones frecuentes.

### Errores y logs
- Retornar `bool` en operaciones que pueden fallar.
- En fallas: log util y setear `lastError` cuando aplique.
- Conservar respuesta raw si ayuda a diagnosticar (`AtResult.raw`).
- Prefijos de logs consistentes: `[core]` `[data]` `[http]` `[mqtt]`.
- Usar `logLine()`/`logValue()` como API principal.
- No saturar logs en loops; preferir log por periodo (ej. cada 5s).

### Concurrencia y tiempo
- Considerar URCs asincronas: `OK` no implica fin de operacion.
- No bloquear demasiado el loop principal; usar timeouts claros.
- Centralizar delays en helpers cuando se repiten.
- Evitar waits sin timeout; preferir polling con limites y logs claros.

### AT commands y prompts
- Para comandos con prompt `>` (topic/payload/subtopic):
  1) esperar `>`
  2) enviar contenido exacto
  3) esperar `OK`
- No encadenar comandos antes de confirmar la aceptacion.
- Si una URC indica error, cortar el flujo y registrar contexto.

### HTTP/SSL
- Mantener flujo CSSLCFG -> HTTPPARA -> HTTPACTION -> HTTPREAD.
- No cambiar chunk size sin validar estabilidad en campo.
- Mantener muting de TapStream en lecturas masivas.

### MQTT
- Esperar URCs especificas:
  - `+CMQTTCONNECT: ...`
  - `+CMQTTPUB: ...`
  - `+CMQTTSUB: ...`
  - `+CMQTTRX...`
- No reintentar acciones exitosas (evita duplicar start/connect/publish).
- Reintentos solo ante errores confirmados y con backoff.

## Reglas del modem A7670SA
- Forzar `ATV1` despues de init para evitar parsing ambiguo.
- Sesion de datos valida requiere:
  - `CGATT == 1`
  - IP valida en `CGPADDR`
  - `NETOPEN == 1`
- MQTT TLS:
  - configurar `CSSLCFG` + `enableSNI`
  - asociar SSL con `CMQTTSSLCFG`
  - esperar `+CMQTTCONNECT: 0,0`
- Ubidots:
  - broker `industrial.api.ubidots.com:8883`
  - token como credencial MQTT
  - topics esperados segun formato Ubidots.

## Validacion minima post-cambio
1. El modem inicializa (`begin()`).
2. Registra red LTE.
3. Obtiene IP valida.
4. `NETOPEN` llega a `1`.
5. MQTT connect funciona.
6. MQTT publish funciona.
7. Ubidots publish funciona.
8. Ubidots subscribe funciona.

## Estrategia recomendada de cambios
- Cambiar una sola capa a la vez.
- Probar con logs reales.
- Confirmar que no se rompio:
  - data session
  - MQTT
  - Ubidots
