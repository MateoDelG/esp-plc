#include "comms/ubidots/ubidots_payload_builder.h"

#include <ArduinoJson.h>

String UbidotsPayloadBuilder::build(const TelemetryPacket& data) {
  StaticJsonDocument<512> doc;
  doc["level-tank1"] = data.levelTank1;
  doc["level-tank2"] = data.levelTank2;
  doc["o2-tank1"] = data.o2Tank1;
  doc["o2-tank2"] = data.o2Tank2;
  doc["ph-tank1"] = data.phTank1;
  doc["ph-tank2"] = data.phTank2;
  doc["temp-tank1"] = data.tempTank1;
  doc["temp-tank2"] = data.tempTank2;
  doc["state-blowers"] = data.stateBlowers;

  String payload;
  serializeJson(doc, payload);
  return payload;
}
