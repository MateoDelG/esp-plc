#pragma once
#include <Arduino.h>
#include <Wire.h>

/*
  pumps_manager (fase 2)
  ----------------------
  - Encapsula PCF8574 con mapa fijo de pines para bombas.
  - Política de activación global "activeHigh":
      activeHigh = true  → ON lógico escribe bit=1
      activeHigh = false → ON lógico escribe bit=0  (típico con ULN2803/relés activo-bajo)
  - SIN inversión especial para MIXER.
  - Se mantiene acceso de 8 bits (read8/write8) y control por canal.
*/

enum class PumpId : uint8_t {
  KCL = 0,      // P0
  H2O,          // P1
  DRAIN,        // P2
  MIXER,        // P3
  SAMPLE1,      // P4
  SAMPLE2,      // P5
  SAMPLE3,      // P6
  SAMPLE4,      // P7
  COUNT         // 8
};

class PumpsManager {
public:
  PumpsManager() = default;

  // Inicializa el PCF8574. Deja el registro a 0xFF (liberado).
  bool begin(uint8_t addr, int sda, int scl, uint32_t i2cHz = 100000, bool activeHigh = true);

  // --- API de 8 bits (por si quieres diagnósticos rápidos)
  bool    write8(uint8_t value);     // escribe el byte completo
  uint8_t read8();                   // lee el byte actual del PCF
  bool    ping();                    // ping I2C

  // --- API por canal (módulo fijo)
  void on (PumpId id);               // enciende canal (lógico)
  void off(PumpId id);               // apaga canal  (lógico)
  void set(PumpId id, bool enable);  // set lógico
  bool isOn(PumpId id) const;        // último estado lógico

  // Helpers
  inline void kclOn()      { on (PumpId::KCL);     }
  inline void kclOff()     { off(PumpId::KCL);     }
  inline void h2oOn()      { on (PumpId::H2O);     }
  inline void h2oOff()     { off(PumpId::H2O);     }
  inline void drainOn()    { on (PumpId::DRAIN);   }
  inline void drainOff()   { off(PumpId::DRAIN);   }
  inline void mixerOn()    { on (PumpId::MIXER);   }
  inline void mixerOff()   { off(PumpId::MIXER);   }
  inline void sample1On()  { on (PumpId::SAMPLE1); }
  inline void sample1Off() { off(PumpId::SAMPLE1); }
  inline void sample2On()  { on (PumpId::SAMPLE2); }
  inline void sample2Off() { off(PumpId::SAMPLE2); }
  inline void sample3On()  { on (PumpId::SAMPLE3); }
  inline void sample3Off() { off(PumpId::SAMPLE3); }
  inline void sample4On()  { on (PumpId::SAMPLE4); }
  inline void sample4Off() { off(PumpId::SAMPLE4); }

  // Apaga todos los canales del módulo fijo (usa lógica activa global).
  void allOff();

  // Configura lógica en caliente y re-aplica el registro sombra.
  void setActiveHigh(bool activeHigh);

  // Info
  inline uint8_t address() const     { return addr_; }
  inline uint8_t lastWritten() const { return shadow_; }
  static void    printByteState(uint8_t value);

private:
  // --- Config/I2C
  uint8_t  addr_        = 0x20;
  int      sda_         = -1;
  int      scl_         = -1;
  uint32_t i2cHz_       = 100000;
  bool     wireBegun_   = false;

  // --- Lógica/estado
  bool     activeHigh_  = true;        // política global
  bool     states_[8]   = {false,false,false,false,false,false,false,false}; // ON lógico por canal
  uint8_t  shadow_      = 0xFF;        // último byte escrito al PCF (físico)

  // --- Internas
  void ensureWireBegun_();
  static inline uint8_t bitOf(PumpId id) { return static_cast<uint8_t>(id); }

  // Traduce ON lógico de un canal al valor del bit según activeHigh_
  inline uint8_t bitValueFor(bool on) const {
    // true  → 1 si activeHigh, 0 si activo-bajo
    // false → 0 si activeHigh, 1 si activo-bajo
    return on ? (activeHigh_ ? 1 : 0) : (activeHigh_ ? 0 : 1);
  }

  // Reconstruye el shadow_ a partir de states_ y lo escribe si cambió.
  bool rebuildAndWriteShadow_();
};
