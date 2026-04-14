#include "LCD_manager.h"


Lcd16x2::Lcd16x2(uint8_t i2cAddress, uint8_t cols, uint8_t rows)
: _lcd(i2cAddress, cols, rows), _cols(cols), _rows(rows) {}

bool Lcd16x2::begin() {
  // Para librerías donde .begin() no existe o falla, usamos init() explícito
  _lcd.init();
  _lcd.backlight();
  _lcd.clear();
  return true;
}

bool Lcd16x2::begin(TwoWire& bus, int sda, int scl) {
  // Si te interesa forzar pines en ESP32
  if (sda >= 0 && scl >= 0) bus.begin(sda, scl);
  else bus.begin();
  return begin();
}

void Lcd16x2::clear() {
  _lcd.clear();
}

void Lcd16x2::setBacklight(bool on) {
  if (on) _lcd.backlight();
  else _lcd.noBacklight();
}

void Lcd16x2::_writeSpaces(uint8_t n) {
  for (uint8_t i = 0; i < n; ++i) _lcd.write(' ');
}

void Lcd16x2::printAt(uint8_t col, uint8_t row, const String& text) {
  if (row >= _rows) return;

  // limpia la línea correctamente
  _lcd.setCursor(0, row);
  _writeSpaces(_cols);

  // escribe el texto desde col
  _lcd.setCursor(col < _cols ? col : 0, row);
  String s = text;
  if (col < _cols) {
    uint8_t maxLen = _cols - col;
    if (s.length() > maxLen) s = s.substring(0, maxLen);
    _lcd.print(s);
  }
}

void Lcd16x2::centerPrint(uint8_t row, const String& text) {
  if (row >= _rows) return;
  String s = text;
  if (s.length() > _cols) s = s.substring(0, _cols);
  int start = (_cols - s.length()) / 2;
  printAt(start < 0 ? 0 : (uint8_t)start, row, s);
}

void Lcd16x2::splash(const String& line1, const String& line2, unsigned long ms) {
  clear();
  centerPrint(0, line1);
  if (_rows > 1) centerPrint(1, line2);
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    update();  // mantiene efectos activos si los hubiera
    yield();   // cede tiempo (útil en ESP)
  }
}

void Lcd16x2::createChar(uint8_t idx, const uint8_t bitmap[8]) {
  if (idx > 7) return;
  _lcd.createChar(idx, (uint8_t*)bitmap);
}

void Lcd16x2::progressBar(uint8_t row, uint8_t percent) {
  if (row >= _rows) return;
  if (percent > 100) percent = 100;
  uint8_t filled = map(percent, 0, 100, 0, _cols);
  _lcd.setCursor(0, row);
  for (uint8_t i = 0; i < _cols; ++i) {
    _lcd.write(i < filled ? (uint8_t)255 : (uint8_t)' ');
  }
}

void Lcd16x2::startMarquee(uint8_t row, const String& text, uint16_t speedMs) {
  if (row >= _rows) return;
  _marqueeRow = row;
  _marqueeText = text;
  _marqueeSpeed = speedMs;
  _marqueePos = 0;
  _marqueeActive = true;

  if ((int)_marqueeText.length() <= _cols) {
    printAt(0, _marqueeRow, _fitToWidth(_marqueeText));
    _marqueeActive = false;
  } else {
    String win = _marqueeText.substring(0, _cols);
    printAt(0, _marqueeRow, win);
    _marqueeLast = millis();
  }
}

void Lcd16x2::stopMarquee() { _marqueeActive = false; }

void Lcd16x2::update() {
  if (!_marqueeActive) return;
  unsigned long now = millis();
  if (now - _marqueeLast < _marqueeSpeed) return;
  _marqueeLast = now;

  String padded = _marqueeText + _padLeft(_cols);
  _marqueePos = (_marqueePos + 1) % (padded.length());
  int end = _marqueePos + _cols;
  String win;
  if (end <= (int)padded.length()) {
    win = padded.substring(_marqueePos, end);
  } else {
    int first = padded.length() - _marqueePos;
    win = padded.substring(_marqueePos) + padded.substring(0, _cols - first);
  }
  printAt(0, _marqueeRow, win);
}

String Lcd16x2::_fitToWidth(const String& in) const {
  if ((int)in.length() <= _cols) {
    String out = in;
    // pad manual
    for (int i = out.length(); i < _cols; ++i) out += ' ';
    return out;
  }
  return in.substring(0, _cols);
}

String Lcd16x2::_padLeft(uint8_t n) const {
  if (n == 0) return String();
  return String(' ', n);
}