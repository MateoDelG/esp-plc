#include <Arduino.h>

// === PINES UART2 (ajusta a tu hardware) ===
constexpr int RX_PIN = 16;   // RX del maestro (recibe desde TX del esclavo)
constexpr int TX_PIN = 17;   // TX del maestro (va al RX del esclavo)

// Envía un JSON por Serial2 y muestra el TX en Serial USB
void sendJson(const char* msg) {
  Serial2.println(msg);   // importante: termina en \r\n (como Serial.println)
  Serial.print("[TX] ");
  Serial.println(msg);
}

// Opcional: manda una secuencia de prueba (para la tecla '4')
void sendDemoSequence() {
  sendJson("{\"op\":\"get_status\"}");
  delay(200);
  sendJson("{\"op\":\"auto_measure\"}");
  delay(200);
  sendJson("{\"op\":\"get_last\"}");
}

void printMenu() {
  Serial.println();
  Serial.println(F("=== Comandos Maestro → Esclavo ==="));
  Serial.println(F("1) get_status"));
  Serial.println(F("2) auto_measure"));
  Serial.println(F("3) get_last"));
  Serial.println(F("4) demo: status → auto → last"));
  Serial.println(F("(Presiona la tecla y ENTER si tu monitor lo requiere)"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);                      // USB
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);  // UART al esclavo

  delay(500);
  printMenu();

  // Limpia buffers por si hay basura
  while (Serial2.available()) Serial2.read();
  while (Serial.available()) Serial.read();
}

void loop() {
  // 1) Lee teclas del USB y manda el comando al esclavo por Serial2
  if (Serial.available()) {
    int ch = Serial.read();

    // Ignora CR/LF/espacios
    if (ch == '\r' || ch == '\n' || ch == ' ') {
      // no-op
    } else {
      switch (ch) {
        case '1':
          sendJson("{\"op\":\"get_status\"}");
          break;
        case '2':
          sendJson("{\"op\":\"auto_measure\"}");
          break;
        case '3':
          sendJson("{\"op\":\"get_last\"}");
          break;
        case '4':
          sendDemoSequence();
          break;
        default:
          Serial.print("Tecla no asignada: ");
          Serial.write(ch);
          Serial.println();
          printMenu();
          break;
      }
    }
  }

  // 2) Lee respuestas del esclavo por Serial2 y muéstralas en USB
  while (Serial2.available()) {
    String line = Serial2.readStringUntil('\n'); // la librería del esclavo envía println(...)
    if (line.length() > 0) {
      // quita \r al final si llegó CRLF
      if (line.endsWith("\r")) line.remove(line.length() - 1);
      Serial.print("[RX] ");
      Serial.println(line);
    }
  }

  // No bloquees la CPU
  delay(5);
}
