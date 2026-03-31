#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace I2CSlaveManager {

    // Inicializa el ESP32-C3 como esclavo I2C
    void begin(uint8_t address);

    // Actualiza valores que el maestro puede leer
    void setFilteredDistance(float d);
    void setRawDistance(float d);

    // Solo para debug
    void printLastRequest();

} // namespace
