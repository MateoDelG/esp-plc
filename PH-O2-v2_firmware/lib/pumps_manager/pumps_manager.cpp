#include "pumps_manager.h"

// ----------------------------
// Utilidades básicas
// ----------------------------
void PumpsManager::ensureWireBegun_() {
  if (!wireBegun_) {
    Wire.begin(sda_, scl_);
    Wire.setClock(i2cHz_);
    wireBegun_ = true;
  }
}

bool PumpsManager::write8(uint8_t value) {
  ensureWireBegun_();
  Wire.beginTransmission(addr_);
  Wire.write(value);
  bool ok = (Wire.endTransmission(true) == 0);
  if (ok) shadow_ = value; // cache del último físico escrito
  return ok;
}

uint8_t PumpsManager::read8() {
  ensureWireBegun_();
  uint8_t value = 0xFF;
  Wire.requestFrom((int)addr_, 1);
  if (Wire.available()) value = Wire.read();
  return value;
}

bool PumpsManager::ping() {
  ensureWireBegun_();
  Wire.beginTransmission(addr_);
  return (Wire.endTransmission(true) == 0);
}

void PumpsManager::printByteState(uint8_t value) {
  Serial.print(F("Estado 0b"));
  for (int i = 7; i >= 0; --i) Serial.print((value >> i) & 1);
  Serial.print(F(" (0x"));
  if (value < 16) Serial.print('0');
  Serial.print(value, HEX);
  Serial.println(F(")"));
}

// ----------------------------
// Inicialización
// ----------------------------
bool PumpsManager::begin(uint8_t addr, int sda, int scl, uint32_t i2cHz, bool activeHigh) {
  addr_       = addr;
  sda_        = sda;
  scl_        = scl;
  i2cHz_      = i2cHz;
  activeHigh_ = activeHigh;

  ensureWireBegun_();

  // Estado lógico inicial: todo OFF
  for (uint8_t i = 0; i < 8; ++i) states_[i] = false;

  // Estado físico inicial recomendado: 0xFF (libera pines)
  // Luego se re-construye el shadow_ según states_ y activeHigh_.
  if (!write8(0xFF) && !ping()) return false;

  return rebuildAndWriteShadow_();
}

// ----------------------------
// Control por canal
// ----------------------------
void PumpsManager::on(PumpId id)  { set(id, true);  }
void PumpsManager::off(PumpId id) { set(id, false); }

void PumpsManager::set(PumpId id, bool enable) {
  uint8_t b = bitOf(id);
  if (b > 7) return;
  states_[b] = enable;
  rebuildAndWriteShadow_();
}

bool PumpsManager::isOn(PumpId id) const {
  uint8_t b = bitOf(id);
  if (b > 7) return false;
  return states_[b];
}

void PumpsManager::allOff() {
  for (uint8_t i = 0; i < 8; ++i) states_[i] = false;
  rebuildAndWriteShadow_();
}

void PumpsManager::setActiveHigh(bool activeHigh) {
  activeHigh_ = activeHigh;
  // Re-aplica el shadow según la nueva política
  rebuildAndWriteShadow_();
}

// ----------------------------
// Internas
// ----------------------------
// Construye el byte a escribir en PCF a partir de los estados lógicos.
//  - Para cada canal: decide si el bit va en 1 o 0 según activeHigh_.
//  - Evita I2C si el byte no cambió.
bool PumpsManager::rebuildAndWriteShadow_() {
  uint8_t next = 0;
  for (int bit = 7; bit >= 0; --bit) {
    next <<= 1;
    next |= (bitValueFor(states_[bit]) & 1);
  }
  // 'next' quedó como b7..b0 (MSB primero); equivalente a armar:
  // next = (b7<<7)|(b6<<6)|...|(b0<<0)

  if (next == shadow_) return true; // nada que escribir
  return write8(next);
}
