#include <Arduino.h>
#include <globals.h>

// Definición única del objeto global
RemoteAccessManager remoteManager(TELNET_HOSTNAME);
Lcd16x2 lcd(0x27, 16, 2);
ADS1115Manager ads(0x48);
DS18B20Manager thermo(TEMP_SENSOR, 12);
