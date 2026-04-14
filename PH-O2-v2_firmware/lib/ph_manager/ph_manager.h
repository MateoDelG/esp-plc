#ifndef PH_MANAGER_H
#define PH_MANAGER_H

#include <Arduino.h>
#include <math.h>
#include "ADS1115_manager.h"

// Forward decl opcional (para el helper que carga desde EEPROM v7)
class ConfigStore;

class PHManager {
public:
  explicit PHManager(ADS1115Manager* ads,
                     uint8_t ads_channel,
                     uint8_t avgSamples = 8);

  bool begin();

  // Lectura con compensación de temperatura:
  //  - Aplica pendiente reescalada: a_T = a_cal * (T_K / Tcal_K)
  //  - Mantiene el offset (b_cal) del modelo elegido (2pt/3pt).
  bool readPH(float tempC, float& ph, float* volts = nullptr);

  // ===== Calibración 2 Puntos =====
  // Define una recta única pH = a*V + b (a temperatura tCalC).
  void setTwoPointCalibration(float pH1, float V1, float pH2, float V2, float tCalC);

  // Acceso directo a los coeficientes del "modelo activo" (recta base a Tcal)
  void setCalibrationCoeffs(float a_cal, float b_cal, float tCalC);
  void getCalibrationCoeffs(float& a_cal, float& b_cal, float& tCalC) const;

  // ===== Calibración 3 Puntos =====
  // 1) LS: mínimos cuadrados lineal con los tres puntos (por defecto).
  // 2) Piecewise: dos tramos (4–7 y 7–10), cada uno con su propia recta.
  //
  // Si piecewise=false => LS; si true => Piecewise.
  bool setThreePointCalibration(float V4, float V7, float V10, float tCalC, bool piecewise = false);

  // Helpers de disponibilidad interna
  bool hasTwoPointModel()    const { return model_ == CalModel::TWO_PT; }
  bool hasThreePointModel()  const { return model_ == CalModel::THREE_PT_LS || model_ == CalModel::THREE_PT_PW; }
  bool isPiecewise3pt()      const { return model_ == CalModel::THREE_PT_PW; }

  // ===== Utilidades =====
  void setAveraging(uint8_t n); // n>=1
  void setChannel(uint8_t ch);  // 0..3

  float lastPH()        const { return last_ph_;    }
  float lastVolts()     const { return last_volts_; }
  const char* lastError() const { return last_error_; }

  // Cargar automáticamente de EEPROM v7 (prefiere 3pt si disponible).
  // No es obligatorio usarla, pero te simplifica la integración.
  bool applyEEPROMCalibration(const ConfigStore& eeprom);

private:
  ADS1115Manager* ads_ = nullptr;
  uint8_t ch_ = 0;
  uint8_t avg_ = 8;

  // === Modelos de calibración soportados ===
  enum class CalModel : uint8_t {
    NONE,
    TWO_PT,          // recta única
    THREE_PT_LS,     // recta única por mínimos cuadrados (V4, V7, V10)
    THREE_PT_PW      // piecewise: [4-7] y [7-10]
  };

  CalModel model_ = CalModel::NONE;

  // Recta base a Tcal (se usa para TWO_PT y THREE_PT_LS)
  float a_cal_ = -3.5f;   // pendiente (pH/V) a Tcal
  float b_cal_ = 7.0f;    // offset (pH) a Tcal
  float tcal_c_ = 25.0f;  // °C a la que se calibró

  // Piecewise (3pt)
  // Recta 1 (tramo 4-7) y recta 2 (tramo 7-10), definidas a Tcal
  float a1_pw_ = NAN, b1_pw_ = NAN;
  float a2_pw_ = NAN, b2_pw_ = NAN;
  // Umbrales de voltaje para decidir tramo (en V)
  float v_th_lo_ = NAN;  // ~ (V4+V7)/2
  float v_th_hi_ = NAN;  // ~ (V7+V10)/2
  // Guardamos los puntos por si quieres depurar
  float v4_ = NAN, v7_ = NAN, v10_ = NAN;

  // Últimos resultados
  float last_ph_ = NAN;
  float last_volts_ = NAN;
  char  last_error_[64] = {0};

  // Helpers
  void  setError_(const char* msg);
  bool  readAveragedVolts_(float& volts);
  float kelvin_(float c) const { return c + 273.15f; }

  // Construcción de modelos
  bool build3ptLeastSquares_(float V4, float V7, float V10, float tCalC);
  bool build3ptPiecewise_(float V4, float V7, float V10, float tCalC);

  // Evalúa un modelo recta (a,b) con compensación de temperatura (escala pendiente)
  bool evalLinearAt_(float volts, float tempC, float a_base, float b_base, float& ph_out);
};

#endif // PH_MANAGER_H
