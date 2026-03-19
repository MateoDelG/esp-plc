// MQTT operations (SIMCom CMQTT)
#ifndef MODEM_MQTT_H
#define MODEM_MQTT_H

#include <Arduino.h>

class ModemManager;

class ModemMqtt {
 public:
  explicit ModemMqtt(ModemManager& modem);

  bool ensureStarted();
  bool acquire(const char* clientId);
  bool connect(const char* host, uint16_t port, const char* clientId,
               const char* user = nullptr, const char* pass = nullptr,
               bool useTls = true);
  bool ensureConnected(const char* host, uint16_t port, const char* clientId,
                       uint8_t retries = 3, uint32_t retryDelayMs = 2000,
                       const char* user = nullptr, const char* pass = nullptr,
                       bool useTls = true);

  bool publish(const char* topic, const char* payload, int qos = 0,
               bool retain = false);
  bool publishText(const char* topic, const char* text, int qos = 0,
                   bool retain = false);
  bool publishJson(const char* topic, const char* json, int qos = 0,
                   bool retain = false);
  bool publishFloat(const char* topic, float value, int qos = 0,
                    bool retain = false, uint8_t decimals = 2);

  bool subscribe(const char* topic, int qos = 1);
  bool subscribeTopic(const char* topic, int qos = 1);
  bool pollIncoming(String& topicOut, String& payloadOut);

  void disconnect();

  bool isConnected() const { return connected_; }

 private:
  bool connectBroker(const char* host, uint16_t port, const char* user,
                     const char* pass, bool useTls);
  void resetService();

  ModemManager& modem_;
  bool started_ = false;
  bool acquired_ = false;
  bool connected_ = false;
  bool tlsConfigured_ = false;
  bool needsReset_ = false;
};

#endif
