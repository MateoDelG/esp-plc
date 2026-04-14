#include "WiFiPortalManager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <string.h>

static const char* kFallbackApName = "pH O2 control";
static const char* kFallbackApPass = "12345678";
static const uint32_t kRetryIntervalMs = 30000;


WiFiPortalManager::WiFiPortalManager(const char* apName, const char* apPassword, int apTriggerPin)
    : _apName(apName), _apPassword(apPassword), _apTriggerPin(apTriggerPin) {}

bool WiFiPortalManager::hasStoredCredentials_() const {
    return _storedSsid[0] != '\0';
}

void WiFiPortalManager::setStoredCredentials(const char* ssid, const char* pass) {
    if (!ssid) ssid = "";
    if (!pass) pass = "";
    strncpy(_storedSsid, ssid, sizeof(_storedSsid) - 1);
    _storedSsid[sizeof(_storedSsid) - 1] = '\0';
    strncpy(_storedPass, pass, sizeof(_storedPass) - 1);
    _storedPass[sizeof(_storedPass) - 1] = '\0';
}

bool WiFiPortalManager::connectWithCredentials(const char* ssid, const char* pass, uint32_t timeoutMs) {
    setStoredCredentials(ssid, pass);
    return connectWithCredentials_(_storedSsid, _storedPass, timeoutMs, false);
}

bool WiFiPortalManager::connectWithCredentials_(const char* ssid, const char* pass, uint32_t timeoutMs, bool keepAp) {
    if (!ssid || ssid[0] == '\0') return false;

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
    if (keepAp) {
        IPAddress localIp(192, 168, 1, 170);
        IPAddress gateway(192, 168, 1, 170);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(localIp, gateway, subnet);
    }
    WiFi.begin(ssid, pass);

    const unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

void WiFiPortalManager::setAutoReconnect(bool enabled) {
    _autoReconnect = enabled;
}

bool WiFiPortalManager::isAPActive() const {
    return _apModeEnabled || _apFallbackActive;
}

void WiFiPortalManager::startFallbackAp_() {
    _apFallbackActive = true;
    _lastRetryMs = millis();
    WiFi.mode(WIFI_AP);
    IPAddress localIp(192, 168, 1, 170);
    IPAddress gateway(192, 168, 1, 170);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(localIp, gateway, subnet);
    WiFi.softAP(kFallbackApName, kFallbackApPass);
    Serial.print("📡 AP fallback activo en IP ");
    Serial.println(WiFi.softAPIP());
}

    void WiFiPortalManager::begin() {
        // pinMode(_apTriggerPin, INPUT_PULLUP);  // Se asume pulsador con pull-up
        // if (_apTriggerPin != -1) {
        // }
    
        // Si el pin está en LOW: solo activar modo AP y servir el portal
        if (_apTriggerPin == -1 || digitalRead(_apTriggerPin) == LOW) {
            // lcd.clear();
            // lcd.centerPrint(0, "Iniciando AP");
            // lcd.centerPrint(1, _apName);
           
            _apModeEnabled = true;

            WiFi.mode(WIFI_AP);  // Solo AP
            IPAddress localIp(192, 168, 1, 170);
            IPAddress gateway(192, 168, 1, 170);
            IPAddress subnet(255, 255, 255, 0);
            WiFi.softAPConfig(localIp, gateway, subnet);
            WiFi.softAP(_apName, _apPassword);
            Serial.print("📡 Modo configuración: AP activo en IP ");
            Serial.println(WiFi.softAPIP());
    
            return;
        }
    
        // Si el pin está en HIGH: intentar conectar normalmente
        Serial.println("WiFi: inicio STA");
        WiFi.mode(WIFI_STA);
        // Configuración IP fija (defaults)
        IPAddress localIp(192, 168, 1, 170);
        IPAddress gateway(192, 168, 1, 1);
        IPAddress subnet(255, 255, 255, 0);
        IPAddress dns(192, 168, 1, 1);
        WiFi.config(localIp, gateway, subnet, dns);

        bool connected = false;
        if (hasStoredCredentials_()) {
            Serial.println("WiFi: intentando credenciales guardadas");
            connected = connectWithCredentials_(_storedSsid, _storedPass, 15000, false);
        }

        if (connected) {
            Serial.println("✅ Conectado a red WiFi: " + WiFi.SSID());
    
            if (MDNS.begin("esp32")) {
                Serial.println("🔍 mDNS activo: http://esp32.local");
            } else {
                Serial.println("⚠️ Error iniciando mDNS");
            }
    
        } else {
            Serial.println("⚠️ No se conectó a ninguna red");
            startFallbackAp_();
        }
    }
    

void WiFiPortalManager::loop() {
    wm.process();

    if (_apFallbackActive && _autoReconnect && hasStoredCredentials_()) {
        const unsigned long now = millis();
        if ((now - _lastRetryMs) >= kRetryIntervalMs) {
            _lastRetryMs = now;
            Serial.println("🔄 Reintentando STA...");
            if (connectWithCredentials_(_storedSsid, _storedPass, 15000, true)) {
                Serial.println("✅ STA reconectado, AP apagado");
                WiFi.softAPdisconnect(true);
                _apFallbackActive = false;
            }
        }
    }
}

bool WiFiPortalManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

IPAddress WiFiPortalManager::getLocalIP() {
    return WiFi.localIP();
}

IPAddress WiFiPortalManager::getAPIP() {
    return WiFi.softAPIP();
}


//Remote access manager implementation

RemoteAccessManager::RemoteAccessManager(const char* hostname)
    : _hostname(hostname), _telnetServer(23) {}

void RemoteAccessManager::begin() {
    setupTelnet();
    setupOTA();
}

void RemoteAccessManager::handle() {
    ArduinoOTA.handle();
    handleTelnet();
}

void RemoteAccessManager::log(const String& message) {
    Serial.println(message);
    if (_logHook) {
        _logHook(message);
    }
    if (_telnetClient && _telnetClient.connected()) {
        _telnetClient.println(message);
    }
}

void RemoteAccessManager::setLogHook(LogHook hook) {
    _logHook = hook;
}

void RemoteAccessManager::setupTelnet() {
    _telnetServer.begin();
    _telnetServer.setNoDelay(true);
    Serial.println("📡 Telnet iniciado en puerto 23");
}

void RemoteAccessManager::handleTelnet() {
    if (_telnetServer.hasClient()) {
        if (_telnetClient && _telnetClient.connected()) {
            WiFiClient newClient = _telnetServer.available();
            newClient.println("❌ Solo se permite una conexión Telnet.");
            newClient.stop();
        } else {
            _telnetClient = _telnetServer.available();
            _telnetClient.println("✅ Conexión Telnet aceptada.");
        }
    }

    if (_telnetClient && _telnetClient.available()) {
        String command = _telnetClient.readStringUntil('\n');
        command.trim();
        if (command == "reboot") {
            _telnetClient.println("♻️ Reiniciando...");
            delay(1000);
            ESP.restart();
        }
    }
}

void RemoteAccessManager::setupOTA() {
    ArduinoOTA.setHostname(_hostname);
    ArduinoOTA.onStart([]() {
        Serial.println("🔄 OTA iniciada...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("✅ OTA completada");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("❌ Error OTA: %u\n", error);
    });
    ArduinoOTA.begin();
    Serial.println("🚀 OTA habilitado");
}
