#include <Arduino.h>
#include "app/app_controller.h"

static AppController app;

void setup() {
  app.begin();
}

void loop() {
  app.update();
}
