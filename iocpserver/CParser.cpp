#include "CParser.h"
	
bool CParser::Parse(std::string& message, etIOOperation opCode) {

	bool result = true;
	switch (opCode) {
	case SendToClient://ответ БД
		ParseServerMessage(message);
		break;
	case ReadFromClient://клиент послал данные
		ParseClientMessage(message);
		break;
	default:
		result = false;
		message = "Error: incorrect operation Id.";
		break;
	}
	return result;
}

inline char* getPacketMessage(std::string& data) {
	return (&data[0] + MYSQL_PACKET_HEADER_LEN);
}

inline size_t getPacketLength(std::string& data) {

	return (*(reinterpret_cast<int*>(&data[0])) & 0x00ffffff);
}

//https://dev.mysql.com/doc/internals/en/connection-phase-packets.html
void CParser::ParseClientMessage(std::string& message) {

	if (message.length() < MYSQL_PACKET_HEADER_LEN + MYSQL_COMMAND_LEN) {
		return;
	}

	char* clientMsg = getPacketMessage(message);
	int pcktLength = getPacketLength(message) - MYSQL_COMMAND_LEN;
	char cmd = *clientMsg;

	if (pcktLength > message.length()) {
		pcktLength = message.length();
	}

	message = message.substr(MYSQL_PACKET_HEADER_LEN + MYSQL_COMMAND_LEN, pcktLength);
}

//TODO: доделать парсер echo с БД
void CParser::ParseServerMessage(std::string& message) {
	if (message.length() < MYSQL_PACKET_HEADER_LEN + MYSQL_COMMAND_LEN) {
		return;
	}

	char* clientMsg = getPacketMessage(message);
	int pcktLength = getPacketLength(message) - MYSQL_COMMAND_LEN;
	char cmd = *clientMsg;

	if (pcktLength > message.length()) {
		pcktLength = message.length();
	}
	if (message.length() > MYSQL_DB_NAME_LEN)
	{
		message = message.substr(MYSQL_PACKET_HEADER_LEN + MYSQL_COMMAND_LEN, MYSQL_DB_NAME_LEN - 1);
	}
	else {
		message = message.substr(MYSQL_PACKET_HEADER_LEN + MYSQL_COMMAND_LEN, pcktLength);
	}
	
}

