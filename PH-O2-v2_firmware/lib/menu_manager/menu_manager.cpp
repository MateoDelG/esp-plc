#include "menu_manager.h"
#include "globals.h"

namespace Buttons {

  // Instancia global de botón DOWN
    Button BTN_DOWN(SW_DOWN);
    Button BTN_UP(SW_UP);
    Button BTN_OK(SW_OK);
    Button BTN_ESC(SW_ESC);


  void testBuzzer(int buzzerPin, int durationMs) {
    pinMode(buzzerPin, OUTPUT);
    digitalWrite(buzzerPin, HIGH);
    delay(durationMs);
    digitalWrite(buzzerPin, LOW);
  }

  void testButtons() {
    const int duration = 50;
    const unsigned long debounceMs = 50;
    unsigned long now = millis();

    // Actualiza: al detectar LOW estable, queda latcheado (value=1) y no cambia más
    BTN_DOWN.update(now, debounceMs);
    BTN_UP.update(now, debounceMs);
    BTN_OK.update(now, debounceMs);
    BTN_ESC.update(now, debounceMs);

    // Sonar una sola vez por cada latch (no vuelve a sonar hasta reset())
    if (BTN_DOWN.consumeEdge()) {
      testBuzzer(BUZZER, duration);
      remoteManager.log("SW_DOWN pressed (latched)");
    }
    if(BTN_UP.consumeEdge()) {
      testBuzzer(BUZZER, duration);
      remoteManager.log("SW_UP pressed (latched)");
    }
    if(BTN_OK.consumeEdge()) {
      testBuzzer(BUZZER, duration);
      remoteManager.log("SW_OK pressed (latched)");
    }
    if(BTN_ESC.consumeEdge()) {
      testBuzzer(BUZZER, duration);
      remoteManager.log("SW_ESC pressed (latched)");
    }

    // Ejemplo de uso del latch:
    // if (BTN_DOWN.value) { /* acción sostenida hasta reset() */ }

    // Para liberar manualmente cuando quieras:
    // BTN_DOWN.reset();
  }

} // namespace Buttons
