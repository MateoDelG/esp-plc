#include "Storage.h"
#include <Preferences.h>
#include <string.h>

namespace {
  Preferences prefs;
  const char *NAMESPACE = "sensorCfg";
  const uint32_t CFG_VERSION = 3;  // por si el día de mañana cambias la estructura
}

void Storage::begin() {
  // RW = false (segunda bandera) indica modo lectura/escritura
  prefs.begin(NAMESPACE, false);
}

bool Storage::loadConfig(ConfigData &cfg) {
  uint32_t ver = prefs.getUInt("ver", 0);
  if (ver == 0) {
    // No hay config previa
    return false;
  }

  cfg.minLevel     = prefs.getFloat("minL",  10.0f);
  cfg.maxLevel     = prefs.getFloat("maxL", 100.0f);
  cfg.samplePeriod = prefs.getInt  ("samp", 1000);

  cfg.kalMea       = prefs.getFloat("kMea",  3.0f);
  cfg.kalEst       = prefs.getFloat("kEst",  8.0f);
  cfg.kalQ         = prefs.getFloat("kQ",    0.08f);

  cfg.i2cAddress   = prefs.getUChar("i2c",  0x30);   // default 0x30

  if (ver >= 2) {
    cfg.espNowEnabled = prefs.getBool("enowEn", true);
    size_t read = prefs.getBytes("enowMac", cfg.espNowPeerMac, sizeof(cfg.espNowPeerMac));
    if (read != sizeof(cfg.espNowPeerMac)) {
      memset(cfg.espNowPeerMac, 0, sizeof(cfg.espNowPeerMac));
    }
  } else {
    cfg.espNowEnabled = true;
    memset(cfg.espNowPeerMac, 0, sizeof(cfg.espNowPeerMac));
  }

  if (ver >= 3) {
    cfg.zeroOffsetCm = prefs.getFloat("zero", 0.0f);
  } else {
    cfg.zeroOffsetCm = 0.0f;
  }

  return (ver <= CFG_VERSION);
}

void Storage::saveConfig(const ConfigData &cfg) {
  prefs.putUInt ("ver",  CFG_VERSION);
  prefs.putFloat("minL", cfg.minLevel);
  prefs.putFloat("maxL", cfg.maxLevel);
  prefs.putInt  ("samp", cfg.samplePeriod);

  prefs.putFloat("kMea", cfg.kalMea);
  prefs.putFloat("kEst", cfg.kalEst);
  prefs.putFloat("kQ",   cfg.kalQ);

  prefs.putUChar("i2c",  cfg.i2cAddress);
  prefs.putBool("enowEn", cfg.espNowEnabled);
  prefs.putBytes("enowMac", cfg.espNowPeerMac, sizeof(cfg.espNowPeerMac));
  prefs.putFloat("zero", cfg.zeroOffsetCm);

}
