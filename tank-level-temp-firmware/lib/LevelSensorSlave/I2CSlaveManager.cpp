#include "I2CSlaveManager.h"
#include "Globals.h"

namespace I2CSlaveManager {

static float filteredDist = 0.0f;
static float rawDist      = 0.0f;

// último registro pedido por el maestro
static volatile uint8_t lastRequestedRegister = 0;

// prototipos internos
static void onReceive(int len);
static void onRequest();

void begin(uint8_t address) {
    // ESP32-C3 como esclavo I2C
    Wire.begin((int)address);
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);

    Serial.printf("[I2C SLAVE] Iniciado en dirección 0x%02X\n", address);
}

void setFilteredDistance(float d) {
    filteredDist = d;
}

void setRawDistance(float d) {
    rawDist = d;
}

void printLastRequest() {
    Serial.printf("[I2C SLAVE] Master pidió registro 0x%02X\n", lastRequestedRegister);
}

// ======================================================
//  onReceive: el maestro nos envía datos
//  Protocolo:
//   - Si solo envía 1 byte: es el "registro" que se
//     quiere leer luego en onRequest().
//   - Si envía 1 + 4 bytes y el registro es 0x08 o 0x0C,
//     interpretamos esos 4 bytes como float y
//     actualizamos min/max en Globals.
// ======================================================
static void onReceive(int len) {
    if (len <= 0) return;

    uint8_t reg = Wire.read();
    lastRequestedRegister = reg;
    len--;

    // Si no hay más datos, es solo selección de registro
    if (len <= 0) return;

    // Si hay >=4 bytes y el registro corresponde a niveles,
    // interpretamos como float en IEEE754, little-endian.
    if ((reg == 0x08 || reg == 0x0C) && len >= 4) {
        union { float f; uint8_t b[4]; } u;
        for (int i = 0; i < 4 && len > 0; ++i, --len) {
            u.b[i] = Wire.read();
        }

        if (reg == 0x08) {
            Globals::setMinLevel(u.f);
            Serial.printf("[I2C SLAVE] setMinLevel via I2C = %.2f cm\n", u.f);
        } else { // 0x0C
            Globals::setMaxLevel(u.f);
            Serial.printf("[I2C SLAVE] setMaxLevel via I2C = %.2f cm\n", u.f);
        }

        // descartar bytes extra si el maestro mandó más de la cuenta
        while (len-- > 0) {
            (void)Wire.read();
        }
    } else {
        // No es un registro de escritura soportado → vaciar el buffer
        while (len-- > 0) {
            (void)Wire.read();
        }
    }
}

// ======================================================
//  onRequest: el maestro hace un "read" al esclavo
//  En función de lastRequestedRegister devolvemos datos.
// ======================================================
static void onRequest() {
    switch (lastRequestedRegister) {

    // Distancia filtrada (float)
    case 0x00: {
        union { float f; uint8_t b[4]; } u;
        u.f = Globals::getDistanceFiltered();
        Serial.printf("[I2C SLAVE] Enviando filtrado=%.2f\n", u.f);
        Wire.write(u.b, 4);
        break;
    }

    // Distancia cruda (float)
    case 0x04: {
        union { float f; uint8_t b[4]; } u;
        u.f = Globals::getDistanceRaw();
        Serial.printf("[I2C SLAVE] Enviando raw=%.2f\n", u.f);
        Wire.write(u.b, 4);
        break;
    }

    // Nivel mínimo (float)
    case 0x08: {
        union { float f; uint8_t b[4]; } u;
        u.f = Globals::getMinLevel();
        Serial.printf("[I2C SLAVE] Enviando nivel minimo=%.2f\n", u.f);
        Wire.write(u.b, 4);
        break;
    }

    // Nivel máximo (float)
    case 0x0C: {
        union { float f; uint8_t b[4]; } u;
        u.f = Globals::getMaxLevel();
        Serial.printf("[I2C SLAVE] Enviando nivel maximo=%.2f\n", u.f);
        Wire.write(u.b, 4);
        break;
    }

    // *** NUEVO: Temperatura (float, °C) ***
    case 0x10: {
        union { float f; uint8_t b[4]; } u;
        u.f = Globals::getTemperature();   // asegúrate de tener esta función en Globals
        Serial.printf("[I2C SLAVE] Enviando temperatura=%.2f °C\n", u.f);
        Wire.write(u.b, 4);
        break;
    }

    // Status / versión / flags de error
    case 0x20:
        Wire.write(Globals::getSensorStatus());
        break;

    // Registro no soportado
    default:
        Wire.write((uint8_t)0xEE);
        break;
    }
}

} // namespace
