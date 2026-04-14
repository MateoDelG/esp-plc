#ifndef GLOBALS_H
#define GLOBALS_H
#include "WiFiPortalManager.h"
#include "LCD_manager.h"
#include "ADS1115_manager.h"
#include "DS18B20_manager.h"
#include "level_sensors_manager.h"

#define SW_DOWN 36
#define SW_UP 39
#define SW_OK 35
#define SW_ESC 34
#define SENSOR_LEVEL_O2 26
#define SENSOR_LEVEL_PH 27
#define SENSOR_LEVEL_H2O 33
#define SENSOR_LEVEL_KCL 32

#define BUZZER 5
#define PUMP_1_PIN 13
#define PUMP_2_PIN 12
#define PUMP_3_PIN 14 
#define PUMP_4_PIN 27
#define MIXER_PIN 26

#define TEMP_SENSOR 19

#define TELNET_HOSTNAME "ph-remote"

// Declaración, no creación
extern Lcd16x2 lcd;    
extern RemoteAccessManager remoteManager;
extern ADS1115Manager ads;
extern DS18B20Manager thermo;


#endif