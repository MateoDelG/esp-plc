#include "modem_parsers.h"

namespace ModemParsers {

int16_t csqToPercent(int16_t csq) {
  if (csq < 0 || csq == 99) {
    return 0;
  }
  if (csq > 31) {
    csq = 31;
  }
  return map(csq, 0, 31, 0, 100);
}

bool parseCgattAttached(const String& response) {
  int idx = response.indexOf("+CGATT:");
  if (idx < 0) {
    return false;
  }
  String value = response.substring(idx + 7);
  value.trim();
  return value.startsWith("1");
}

bool parseCgpaddrIp(const String& response, String& ipOut) {
  int idx = response.indexOf("+CGPADDR:");
  if (idx < 0) {
    return false;
  }

  int comma = response.indexOf(',', idx);
  if (comma < 0) {
    return false;
  }

  String ip = response.substring(comma + 1);
  ip.trim();

  if (ip.length() == 0) {
    return false;
  }

  ipOut = ip;
  return true;
}

int parseNetOpenStatus(const String& response) {
  if (response.indexOf("+NETOPEN: 1") >= 0) {
    return 1;
  }
  if (response.indexOf("+NETOPEN: 0") >= 0) {
    return 0;
  }
  return -1;
}

bool responseHasAlreadyOpened(const String& response) {
  return response.indexOf("Network is already opened") >= 0 ||
         response.indexOf("+IP ERROR: Network is already opened") >= 0;
}

bool parseCdnsgipIp(const String& response, String& ipOut) {
  int idx = response.indexOf("+CDNSGIP:");
  if (idx < 0) {
    return false;
  }

  int firstQuote = response.indexOf('"', idx);
  if (firstQuote < 0) {
    return false;
  }
  int secondQuote = response.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) {
    return false;
  }
  int thirdQuote = response.indexOf('"', secondQuote + 1);
  if (thirdQuote < 0) {
    return false;
  }
  int fourthQuote = response.indexOf('"', thirdQuote + 1);
  if (fourthQuote < 0) {
    return false;
  }

  String ip = response.substring(thirdQuote + 1, fourthQuote);
  ip.trim();
  if (ip.length() == 0) {
    return false;
  }

  ipOut = ip;
  return true;
}

bool parseHttpAction(const String& response, int& status, int& length) {
  int idx = response.indexOf("+HTTPACTION:");
  if (idx < 0) {
    return false;
  }

  int firstComma = response.indexOf(',', idx);
  if (firstComma < 0) {
    return false;
  }

  int secondComma = response.indexOf(',', firstComma + 1);
  if (secondComma < 0) {
    return false;
  }

  String statusStr = response.substring(firstComma + 1, secondComma);
  String lenStr = response.substring(secondComma + 1);
  statusStr.trim();
  lenStr.trim();

  status = statusStr.toInt();
  length = lenStr.toInt();
  return true;
}

void parseHttpUrl(const char* url, String& hostOut, String& pathOut) {
  String u = url ? String(url) : String();
  u.trim();

  if (u.startsWith("http://")) {
    u.remove(0, 7);
  }

  int slash = u.indexOf('/');
  if (slash < 0) {
    hostOut = u;
    pathOut = "/";
  } else {
    hostOut = u.substring(0, slash);
    pathOut = u.substring(slash);
    if (pathOut.length() == 0) {
      pathOut = "/";
    }
  }
}

bool parseMqttResultCode(const String& response, const char* prefix,
                         int& codeOut) {
  if (!prefix || strlen(prefix) == 0) {
    return false;
  }

  int idx = response.indexOf(prefix);
  if (idx < 0) {
    return false;
  }

  int comma = response.indexOf(',', idx);
  if (comma < 0) {
    return false;
  }

  String codeStr = response.substring(comma + 1);
  codeStr.trim();
  codeOut = codeStr.toInt();
  return true;
}

bool parseRxStart(const String& line, uint16_t& topicLen, uint16_t& payloadLen) {
  if (!line.startsWith("+CMQTTRXSTART:")) {
    return false;
  }
  int firstComma = line.indexOf(',');
  if (firstComma < 0) {
    return false;
  }
  int secondComma = line.indexOf(',', firstComma + 1);
  if (secondComma < 0) {
    return false;
  }
  String topicStr = line.substring(firstComma + 1, secondComma);
  String payloadStr = line.substring(secondComma + 1);
  topicStr.trim();
  payloadStr.trim();
  topicLen = static_cast<uint16_t>(topicStr.toInt());
  payloadLen = static_cast<uint16_t>(payloadStr.toInt());
  return topicLen > 0 && payloadLen > 0;
}

}  // namespace ModemParsers
