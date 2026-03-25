// MQTT abstraction interface
#ifndef MODEM_MQTT_INTERFACE_H
#define MODEM_MQTT_INTERFACE_H

#include <Arduino.h>

class IModemMqtt {
 public:
  virtual ~IModemMqtt() = default;

  virtual bool ensureStarted() = 0;
  virtual bool acquire(const char* clientId) = 0;
  virtual bool connect(const char* host, uint16_t port, const char* clientId,
                       const char* user, const char* pass, bool useTls) = 0;
  virtual bool ensureConnected(const char* host, uint16_t port,
                               const char* clientId, uint8_t retries,
                               uint32_t retryDelayMs, const char* user,
                               const char* pass, bool useTls) = 0;
  virtual bool publish(const char* topic, const char* payload, int qos,
                       bool retain) = 0;
  virtual bool subscribe(const char* topic, int qos) = 0;
  virtual bool pollIncoming(String& topicOut, String& payloadOut) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;
};

#endif
