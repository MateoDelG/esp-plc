#pragma once

#include <Arduino.h>

#include "modem_types.h"

class Logger;

ModemConfig makeModemConfig(Logger& logger);
