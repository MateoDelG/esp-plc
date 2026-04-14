#pragma once
#include "menu_manager.h"
#include "globals.h"

namespace Buttons {

  // ====== Estructura de botón ======
  struct Button {
    uint8_t pin;
    bool prev = HIGH;               // última lectura cruda estable
    unsigned long lastMs = 0;       // último cambio estable (debounce)
    uint8_t value = 0;              // 1 = LATCHEADO (no cambia al soltar)
    uint8_t edge  = 0;              // 1 = hubo “clic” (flanco estable a LOW)

    explicit Button(uint8_t p) : pin(p) {}

    // Nota: si usas pines 34–39 del ESP32, no tienen pull-up interno → usa INPUT y pull-up externo
    void begin() { pinMode(pin, INPUT_PULLUP); }

    void update(unsigned long now, unsigned long debounceMs = 120) {
      // Si ya está latcheado, ignoramos cualquier transición hasta reset()
      if (value) return;

      bool r = digitalRead(pin);
      if (r != prev && (now - lastMs) > debounceMs) {
        lastMs = now;
        prev = r;
        if (r == LOW) {       // flanco a LOW (estable)
          value = 1;          // LATCH: queda en 1 hasta reset()
          edge  = 1;          // evento de "clic"
        }
        // Si r == HIGH (soltar), NO tocamos 'value' (sigue en 0 si no estaba latcheado)
      }
    }

    // Lee y limpia el “evento” de clic (no afecta el LATCH 'value')
    bool consumeEdge() {
      if (edge) { edge = 0; return true; }
      return false;
    }

    // Limpia el LATCH manualmente y se rearma a partir del nivel actual
    void reset() {
      value = 0;
      edge  = 0;
      prev = digitalRead(pin);
      lastMs = millis();
    }
  };

  // Instancia de ejemplo
    extern Button BTN_DOWN;
    extern Button BTN_UP;
    extern Button BTN_OK;
    extern Button BTN_ESC;

  // Prototipos
  void testBuzzer(int buzzerPin, int durationMs);
  void testButtons();

} // namespace Buttons
