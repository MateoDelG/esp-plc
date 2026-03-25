// SIMCom CMQTT implementation
#ifndef MODEM_MQTT_SIMCOM_H
#define MODEM_MQTT_SIMCOM_H

#include <Arduino.h>

#include "modem_mqtt_interface.h"

class ModemManager;

class SimcomMqttClient : public IModemMqtt {
 public:
  explicit SimcomMqttClient(ModemManager& modem);

  bool ensureStarted() override;
  bool acquire(const char* clientId) override;
  bool connect(const char* host, uint16_t port, const char* clientId,
               const char* user, const char* pass, bool useTls) override;
  bool ensureConnected(const char* host, uint16_t port, const char* clientId,
                       uint8_t retries, uint32_t retryDelayMs,
                       const char* user, const char* pass,
                       bool useTls) override;
  bool publish(const char* topic, const char* payload, int qos,
               bool retain) override;
  bool subscribe(const char* topic, int qos) override;
  bool pollIncoming(String& topicOut, String& payloadOut) override;
  void disconnect() override;

  bool isConnected() const override { return connected_; }

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
