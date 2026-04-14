#ifndef LCD_MANAGER_H
#define LCD_MANAGER_H
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

class Lcd16x2 {
public:
  // Dirección típica 0x27 u 0x3F; por defecto 16x2
  Lcd16x2(uint8_t i2cAddress = 0x27, uint8_t cols = 16, uint8_t rows = 2);

  // Inicializa el LCD usando lcd.init(); asume que Wire ya está iniciado
  bool begin();

  // Alternativa: iniciar I2C con pines (útil en ESP32). Si sda/scl < 0, usa por defecto
  bool begin(TwoWire& bus, int sda = -1, int scl = -1);

  // Utilidades básicas
  void clear();
  void setBacklight(bool on);
  void printAt(uint8_t col, uint8_t row, const String& text);
  void centerPrint(uint8_t row, const String& text);
  void splash(const String& line1, const String& line2, unsigned long ms = 1500);

  // Caracteres personalizados (idx 0–7)
  void createChar(uint8_t idx, const uint8_t bitmap[8]);

  // Barra de progreso (0–100)
  void progressBar(uint8_t row, uint8_t percent);

  // ----- Marquee no bloqueante -----
  void startMarquee(uint8_t row, const String& text, uint16_t speedMs = 250);
  void stopMarquee();
  bool marqueeActive() const { return _marqueeActive; }
  void update(); // llamar en loop()

  // Getters
  uint8_t cols() const { return _cols; }
  uint8_t rows() const { return _rows; }

private:
  LiquidCrystal_I2C _lcd;
  uint8_t _cols, _rows;

  // helpers
  String _fitToWidth(const String& in) const; // recorta/pad a ancho
  String _padLeft(uint8_t n) const;
  void _writeSpaces(uint8_t n);

  // estado marquee
  bool _marqueeActive = false;
  String _marqueeText;
  uint8_t _marqueeRow = 0;
  int _marqueePos = 0;
  uint16_t _marqueeSpeed = 250;
  unsigned long _marqueeLast = 0;
};

#endif // LCD_MANAGER_H
