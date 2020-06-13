#include "CParser.h"
#include <cstring>
	
//В тестах использовалсся MySQL-5.7, в ней используется Protocol::HandshakeResponse41 на запрос креденшелов от сервера БД
//(см. https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_packets_protocol_handshake_response.html)

const size_t MYSQL_LOGIN_FILLER_LEN = 23;

struct mysqlLoginClientInfo {
	size_t  capability;
	size_t  maxPacketSize;
	char    charset;
	char	filler[MYSQL_LOGIN_FILLER_LEN];
	//char*   userName;// Protocol::NullTerminatedString = "username\0";
};

//Структура MySQL паетов ( Protocol::Packet)
//(см https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_packets.html#sect_protocol_basic_packets_packet)
struct mysqlPacketInfo {
	char  payloadLength[3];
	char  sequenceId;

};

inline mysqlPacketInfo* getPacketInfo(std::string& data) {
	mysqlPacketInfo* info = new mysqlPacketInfo();
	memcpy(info, &data[0], sizeof(mysqlPacketInfo));
	return info;
}

inline mysqlLoginClientInfo* getLoginInfo(std::string& data) {
	mysqlLoginClientInfo* info = new mysqlLoginClientInfo();
	const char* offset = &data[0] + sizeof(mysqlPacketInfo);
	memcpy(info, offset, sizeof(mysqlLoginClientInfo));
	return info;
}

void CParser::ParseClientCredentials(std::string& message) {

	//parse message header
	const mysqlPacketInfo* packetInfo = getPacketInfo(message);

	size_t messageLength = *(&(packetInfo->payloadLength[0]));

	size_t sequenceId = packetInfo->sequenceId;

	char* offset = &message[0] + sizeof(mysqlPacketInfo);

	if (messageLength > message.length()) {
		messageLength = message.length() - sizeof(mysqlPacketInfo);
	}

	std::string payload(offset, messageLength);

	//parse login data
	const mysqlLoginClientInfo* loginInfo = getLoginInfo(payload);

	size_t capability = loginInfo->capability;
	
	size_t maxPacketSize = loginInfo->maxPacketSize;
	
	size_t charset = loginInfo->charset;
	
	offset = &payload[0] + sizeof(mysqlLoginClientInfo);

	std::string userName(offset);

	message = "Client username: " + userName;
}

void CParser::ParseClientQuery(std::string& message) {
	const mysqlPacketInfo* packetInfo = getPacketInfo(message);
	
	size_t messageLength = *(&(packetInfo->payloadLength[0]));

	size_t sequenceId = packetInfo->sequenceId;

	char* offset = &message[0] + sizeof(mysqlPacketInfo);

	if (messageLength > message.length()) {
		messageLength = message.length() - sizeof(mysqlPacketInfo);
	}

	std::string mysqlQuery(offset, messageLength);

	message = "Client query: " + mysqlQuery;
}

bool CParser::Parse(std::string& message, enIOOperation opCode) {

	bool result = true;
	switch (opCode) {
	case SendToClient://ответ БД
		//ParseServerMessage(message);
		break;
	case ReadFromClient://клиент послал данные
		//ParseClientCredentials(message);
		ParseClientQuery(message);
		break;
	default:
		result = false;
		message = "Error: incorrect operation Id.";
		break;
	}
	return result;
}