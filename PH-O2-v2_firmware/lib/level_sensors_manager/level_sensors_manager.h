#pragma once
#include <Arduino.h>

/*
  level_sensors_manager
  ---------------------
  - Inicializa 4 sensores digitales de nivel con pull-ups EXTERNAS.
  - Lógica: O2/pH/KCL/H2O activos en HIGH (inversión aplicada en software).
  - Pines por defecto:
      O2  -> 26
      pH  -> 27
      KCL -> 32
      H2O -> 33
*/

enum class LevelSensorId : uint8_t {
  O2 = 0,
  PH,
  KCL,
  H2O,
  COUNT
};

class LevelSensorsManager {
public:
  LevelSensorsManager() = default;

  /*
    begin(...)
    - Configura los pines como INPUT (pull-up externas).
    - Retorna true si los pines son válidos (> = 0).
  */
  bool begin(int pinO2 = 26, int pinPH = 27, int pinKCL = 32, int pinH2O = 33);

  // Lecturas lógicas (true = activo/HIGH, false = inactivo/LOW)
  bool o2()  const;
  bool ph()  const;
  bool kcl() const;
  bool h2o() const;

  // Lectura por ID (útil para bucles)
  bool read(LevelSensorId id) const;

  // Pines actuales
  inline int pinO2()  const { return pinO2_;  }
  inline int pinPH()  const { return pinPH_;  }
  inline int pinKCL() const { return pinKCL_; }
  inline int pinH2O() const { return pinH2O_; }

private:
  int pinO2_  = 26;
  int pinPH_  = 27;
  int pinKCL_ = 32;
  int pinH2O_ = 33;

  static inline bool validPin_(int pin) { return pin >= 0; }
  static inline bool readHigh_(int pin) { return digitalRead(pin) == LOW; }
};
