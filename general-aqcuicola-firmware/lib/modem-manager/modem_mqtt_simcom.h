// SIMCom CMQTT implementation
#ifndef MODEM_MQTT_SIMCOM_H
#define MODEM_MQTT_SIMCOM_H

#include <Arduino.h>

#include "modem_mqtt_interface.h"

class ModemManager;
struct AtResult;

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

  friend class ModemMqtt;

 private:
  bool connectBroker(const char* host, uint16_t port, const char* clientId,
                     const char* user, const char* pass, bool useTls);
  void resetService();
  void markPublishFailure(const char* step);
  bool execAccq(const char* clientId, AtResult& out);
  bool performAccqRecovery(const char* clientId, bool full);
  bool queryAccqOccupied(uint8_t index, bool& occupied);
  void recordAccqFailure();
  void resetAccqFailures();
  bool performConnectStackRecovery(const char* clientId);
  void markNeedsModemRestart();
  void markNeedsEspRestart();
  bool needsModemRestart() const;
  bool needsEspRestart() const;
  void clearEscalation();

  enum class RxState : uint8_t {
    Idle = 0,
    StartSeen,
    TopicRead,
    PayloadRead,
  };

  ModemManager& modem_;
  bool started_ = false;
  bool acquired_ = false;
  bool connected_ = false;
  bool tlsConfigured_ = false;
  bool needsReset_ = false;
  uint8_t serverType_ = 0;
  uint8_t accqFailCount_ = 0;
  uint32_t accqFailStartMs_ = 0;
  uint32_t accqNextAttemptMs_ = 0;
  uint32_t accqBackoffMs_ = 2000;
  RxState rxState_ = RxState::Idle;
  uint16_t rxTopicLen_ = 0;
  uint16_t rxPayloadLen_ = 0;
  String rxTopic_;
  String rxPayload_;
  uint32_t rxStageStartMs_ = 0;
  bool needsModemRestart_ = false;
  bool needsEspRestart_ = false;
};

#endif
