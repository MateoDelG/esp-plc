#ifndef MODEM_MANAGER_CONFIG_H
#define MODEM_MANAGER_CONFIG_H

// TinyGSM modem selection for SIMCOM A7672X (A7670SA)
#define TINY_GSM_MODEM_A7672X
// #define TINY_GSM_MODEM_SIM7600

// Optional TinyGSM buffer size
#ifndef TINY_GSM_RX_BUFFER
#define TINY_GSM_RX_BUFFER 1024
#endif

// Uncomment to enable TinyGSM internal debug
// #define TINY_GSM_DEBUG Serial

// Uncomment to dump raw AT commands via StreamDebugger
// #define DUMP_AT_COMMANDS

#endif
