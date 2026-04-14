#include "level_sensors_manager.h"

bool LevelSensorsManager::begin(int pinO2, int pinPH, int pinKCL, int pinH2O) {
  pinO2_  = pinO2;
  pinPH_  = pinPH;
  pinKCL_ = pinKCL;
  pinH2O_ = pinH2O;

  // Configuración de pines como ENTRADA (pull-up externas en hardware)
  if (validPin_(pinO2_))  pinMode(pinO2_,  INPUT);
  if (validPin_(pinPH_))  pinMode(pinPH_,  INPUT);
  if (validPin_(pinKCL_)) pinMode(pinKCL_, INPUT);
  if (validPin_(pinH2O_)) pinMode(pinH2O_, INPUT);

  // Consideramos válido si al menos todos los pines tienen índice no negativo
  return validPin_(pinO2_) && validPin_(pinPH_) && validPin_(pinKCL_) && validPin_(pinH2O_);
}

// --- Lecturas específicas ---
bool LevelSensorsManager::o2()  const { return !readHigh_(pinO2_); }
bool LevelSensorsManager::ph()  const { return !readHigh_(pinPH_); }
bool LevelSensorsManager::kcl() const { return !readHigh_(pinKCL_); }
bool LevelSensorsManager::h2o() const { return !readHigh_(pinH2O_); }

// --- Lectura genérica por ID ---
bool LevelSensorsManager::read(LevelSensorId id) const {
  switch (id) {
    case LevelSensorId::O2:  return o2();
    case LevelSensorId::PH:  return ph();
    case LevelSensorId::KCL: return kcl();
    case LevelSensorId::H2O: return h2o();
    default:                 return false;
  }

}
