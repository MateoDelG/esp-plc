#include "modem_mqtt.h"

#include "modem_manager.h"

ModemMqtt::ModemMqtt(ModemManager& modem)
    : modem_(modem), simcom_(modem), impl_(&simcom_) {}

bool ModemMqtt::ensureStarted() { return impl_->ensureStarted(); }

bool ModemMqtt::acquire(const char* clientId) { return impl_->acquire(clientId); }

bool ModemMqtt::connect(const char* host, uint16_t port, const char* clientId,
                        const char* user, const char* pass, bool useTls) {
  return impl_->connect(host, port, clientId, user, pass, useTls);
}

bool ModemMqtt::ensureConnected(const char* host, uint16_t port,
                                const char* clientId, uint8_t retries,
                                uint32_t retryDelayMs, const char* user,
                                const char* pass, bool useTls) {
  return impl_->ensureConnected(host, port, clientId, retries, retryDelayMs,
                                user, pass, useTls);
}

bool ModemMqtt::publish(const char* topic, const char* payload, int qos,
                        bool retain) {
  return impl_->publish(topic, payload, qos, retain);
}

bool ModemMqtt::publishText(const char* topic, const char* text, int qos,
                            bool retain) {
  return publish(topic, text, qos, retain);
}

bool ModemMqtt::publishJson(const char* topic, const char* json, int qos,
                            bool retain) {
  return publish(topic, json, qos, retain);
}

bool ModemMqtt::publishFloat(const char* topic, float value, int qos,
                             bool retain, uint8_t decimals) {
  String payload(value, static_cast<unsigned int>(decimals));
  return publish(topic, payload.c_str(), qos, retain);
}

bool ModemMqtt::subscribe(const char* topic, int qos) {
  return impl_->subscribe(topic, qos);
}

bool ModemMqtt::subscribeTopic(const char* topic, int qos) {
  return subscribe(topic, qos);
}

bool ModemMqtt::pollIncoming(String& topicOut, String& payloadOut) {
  return impl_->pollIncoming(topicOut, payloadOut);
}

void ModemMqtt::disconnect() { impl_->disconnect(); }
