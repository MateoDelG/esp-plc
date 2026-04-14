#include "test_board.h"
#include <Arduino.h>
#include <globals.h>


namespace TestBoard {
void testPumps(int pump1Pin, int pump2Pin, int pump3Pin, int pump4Pin,
               int mixerPin) {
  digitalWrite(pump1Pin, HIGH);
  digitalWrite(pump2Pin, HIGH);
  digitalWrite(pump3Pin, HIGH);
  digitalWrite(pump4Pin, HIGH);
  digitalWrite(mixerPin, HIGH);
  delay(2000);
  digitalWrite(pump1Pin, LOW);
  digitalWrite(pump2Pin, LOW);
  digitalWrite(pump3Pin, LOW);
  digitalWrite(pump4Pin, LOW);
  digitalWrite(mixerPin, LOW);
  delay(2000);
}

void testBuzzer(int buzzerPin, int durationMs) {
  digitalWrite(buzzerPin, HIGH);
  delay(durationMs);
  digitalWrite(buzzerPin, LOW);
}


void testButtons() {
    // Estados anteriores
    static bool prevDown = HIGH;
    static bool prevUp   = HIGH;
    static bool prevOk   = HIGH;
    static bool prevEsc  = HIGH;
    
    // Últimos tiempos de activación (para debounce)
    static unsigned long lastDownMs = 0;
    static unsigned long lastUpMs   = 0;
    static unsigned long lastOkMs   = 0;
    static unsigned long lastEscMs  = 0;
    
    const int duration = 50;       // beep
    const int debounceMs = 250;      // ventana de rebote
    unsigned long now = millis();

    // SW_DOWN
    bool currDown = digitalRead(SW_DOWN);
    if (prevDown == HIGH && currDown == LOW && (now - lastDownMs > debounceMs)) {
        testBuzzer(BUZZER, duration);
        remoteManager.log("SW_DOWN pressed");
        lastDownMs = now;
    }
    prevDown = currDown;

    // SW_UP
    bool currUp = digitalRead(SW_UP);
    if (prevUp == HIGH && currUp == LOW && (now - lastUpMs > debounceMs)) {
        testBuzzer(BUZZER, duration);
        remoteManager.log("SW_UP pressed");
        lastUpMs = now;
    }
    prevUp = currUp;

    // SW_OK
    bool currOk = digitalRead(SW_OK);
    if (prevOk == HIGH && currOk == LOW && (now - lastOkMs > debounceMs)) {
        testBuzzer(BUZZER, duration);
        remoteManager.log("SW_OK pressed");
        lastOkMs = now;
    }
    prevOk = currOk;

    // SW_ESC
    bool currEsc = digitalRead(SW_ESC);
    if (prevEsc == HIGH && currEsc == LOW && (now - lastEscMs > debounceMs)) {
        testBuzzer(BUZZER, duration);
        remoteManager.log("SW_ESC pressed");
        lastEscMs = now;
    }
    prevEsc = currEsc;
}
} // namespace TestBoard