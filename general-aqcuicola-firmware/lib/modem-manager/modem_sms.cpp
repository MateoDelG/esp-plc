#include "modem_sms.h"

#include "modem_manager.h"
#include "modem_at.h"

SmsHandler::SmsHandler(ModemManager& modem) : modem_(modem) {}

bool SmsHandler::begin() {
  modem_.at().exec(3000L, GF("+CMGF=1"));
  modem_.at().exec(3000L, GF("+CNMI=1,2,0,0,0"));
  return true;
}

bool SmsHandler::parseIncoming(const String& line, String& outText) {
  if (!line.startsWith("+CMT:")) {
    return false;
  }

  int bodyStart = line.indexOf('\n');
  if (bodyStart < 0) {
    return false;
  }

  outText = line.substring(bodyStart + 1);
  outText.replace("\r", "");
  outText.replace("\n", "");
  outText.trim();

  return outText.length() > 0;
}

bool SmsHandler::isResetCommand(const String& text) {
  if (text.length() == 0) {
    return false;
  }

  String lower = text;
  lower.toLowerCase();
  return lower == "reset";
}

bool SmsHandler::isUpdateCommand(const String& text) {
  if (text.length() == 0) {
    return false;
  }

  String lower = text;
  lower.toLowerCase();
  return lower == "update";
}
