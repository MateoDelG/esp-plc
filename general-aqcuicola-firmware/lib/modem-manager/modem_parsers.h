// Pure parsing helpers for modem responses
#ifndef MODEM_PARSERS_H
#define MODEM_PARSERS_H

#include <Arduino.h>

namespace ModemParsers {

int16_t csqToPercent(int16_t csq);

bool parseCgattAttached(const String& response);
bool parseCgpaddrIp(const String& response, String& ipOut);

int parseNetOpenStatus(const String& response);
bool responseHasAlreadyOpened(const String& response);

bool parseCdnsgipIp(const String& response, String& ipOut);

bool parseHttpAction(const String& response, int& status, int& length);
void parseHttpUrl(const char* url, String& hostOut, String& pathOut);
bool parseHttpReadHeader(const String& line, int& dataLen);

bool parseMqttResultCode(const String& response, const char* prefix,
                         int& codeOut);
bool parseRxStart(const String& line, uint16_t& topicLen, uint16_t& payloadLen);

bool parseFloatValue(const String& text, float& valueOut);
bool sanitizeInfoText(String& text);

}  // namespace ModemParsers

#endif
