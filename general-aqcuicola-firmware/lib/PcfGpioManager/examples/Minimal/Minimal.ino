#include <Arduino.h>
#include <Wire.h>
#include "PcfGpioManager.h"

PcfGpioManager gpio;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  gpio.addInputExpander(0x20);   // entradas 0..7
  gpio.addOutputExpander(0x26);  // salidas 0..7

  gpio.begin();
}

void loop() {
  int in0 = gpio.digitalReadInput(0);
  if (in0 == 1) {
    gpio.digitalWriteOutput(0, HIGH);
  } else if (in0 == 0) {
    gpio.digitalWriteOutput(0, LOW);
  }
  delay(200);
}
