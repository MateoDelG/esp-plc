#ifndef WIFI_PORTAL_MANAGER_H
#define WIFI_PORTAL_MANAGER_H

#include <WiFiManager.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ArduinoOTA.h>
// #include <globals.h>


class WiFiPortalManager {
public:
    WiFiPortalManager(const char* apName = "ESP32-Config", const char* apPassword = "clave123", int apTriggerPin = -1);

    void begin();
    void loop();

    bool isConnected();
    bool isAPActive() const;
    IPAddress getLocalIP();
    IPAddress getAPIP();

    void setStoredCredentials(const char* ssid, const char* pass);
    bool connectWithCredentials(const char* ssid, const char* pass, uint32_t timeoutMs = 15000);
    void setAutoReconnect(bool enabled);

private:
    WiFiManager wm;
    const char* _apName;
    const char* _apPassword;
    int _apTriggerPin;
    bool _apModeEnabled = false;
    bool _apFallbackActive = false;
    bool _autoReconnect = true;
    uint32_t _lastRetryMs = 0;

    char _storedSsid[33] = {0};
    char _storedPass[65] = {0};

    bool hasStoredCredentials_() const;
    bool connectWithCredentials_(const char* ssid, const char* pass, uint32_t timeoutMs, bool keepAp);
    void startFallbackAp_();
};

class RemoteAccessManager {
public:
    using LogHook = void (*)(const String& message);

    RemoteAccessManager(const char* hostname = "BeansIOT_LAB");

    void begin();
    void handle();
    void log(const String& message);
    void setLogHook(LogHook hook);

private:
    const char* _hostname;

    WiFiServer _telnetServer;
    WiFiClient _telnetClient;

    LogHook _logHook = nullptr;

    void setupTelnet();
    void setupOTA();
    void handleTelnet();
};



#endif
