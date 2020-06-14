#include "CParser.h"
#include <cstring>
	
//� ������ �������������� MySQL-5.7, � ��� ������������ Protocol::HandshakeResponse41 �� ������ ����������� �� ������� ��
//(��. https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_packets_protocol_handshake_response.html)

const size_t MYSQL_LOGIN_FILLER_LEN = 23;
//��������� mysql ��������� Protocol::HandshakeResponse41 - ����� ������� �� � ������������ �����
struct mysqlLoginClientInfo {
	size_t  capability;
	size_t  maxPacketSize;
	char    charset;
	char	filler[MYSQL_LOGIN_FILLER_LEN];
	//char*   userName;// Protocol::NullTerminatedString = "username\0";
};

//��������� MySQL ����� Protocol::Packet - � ��� ���������� mysql �������.
//(�� https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_packets.html#sect_protocol_basic_packets_packet)
struct mysqlPacketInfo {
	char  payloadLength[3];
	char  sequenceId;
	//char* mysqlQuery; // Protocol::VariableLengthString - length == payloadLength
};

inline mysqlPacketInfo getPacketInfo(std::string& data) {
	mysqlPacketInfo info;
	memcpy(&info, &data[0], sizeof(mysqlPacketInfo));
	return info;
}

inline mysqlLoginClientInfo getLoginInfo(std::string& data) {
	mysqlLoginClientInfo info;
	const char* offset = &data[0] + sizeof(mysqlPacketInfo);
	memcpy(&info, offset, sizeof(mysqlLoginClientInfo));
	return info;
}

void CParser::ParseClientCredentials(std::string& message, const mysqlPacketInfo packetInfo) {

	size_t messageLength = *(&(packetInfo.payloadLength[0]));

	const char* offset = &message[0] + sizeof(mysqlPacketInfo);

	if (messageLength > message.length()) {
		messageLength = message.length() - sizeof(mysqlPacketInfo);
	}

	std::string payload(offset, messageLength);

	//parse login data
	const mysqlLoginClientInfo loginInfo = getLoginInfo(payload);

	size_t capability = loginInfo.capability;
	
	size_t maxPacketSize = loginInfo.maxPacketSize;
	
	size_t charset = loginInfo.charset;
	
	offset = &payload[0] + sizeof(mysqlLoginClientInfo);

	std::string userName(offset);

	message = "Client username: " + userName;
}

void CParser::ParseClientQuery(std::string& message, const mysqlPacketInfo packetInfo) {
	
	size_t messageLength = *(&(packetInfo.payloadLength[0]));

	const char* offset = &message[0] + sizeof(mysqlPacketInfo) + 1;

	if (messageLength > message.length()) {
		messageLength = message.length() - sizeof(mysqlPacketInfo);
	}
	//-1 �.�. ��� �� ����� ������ ���������� ������ � ���������
	messageLength = messageLength > 0 ? messageLength - 1 : messageLength;

	std::string mysqlQuery(offset, messageLength);

	message = mysqlQuery;
}

bool CParser::Parse(std::string& message) {

	bool result = true;
	const mysqlPacketInfo packetInfo = getPacketInfo(message);
	size_t sequenceId = packetInfo.sequenceId;

	//sequenceId == 1 �������� ��� �� �� ������ ����������� � ���� ������ ���� ���������� - ������ ��
	if (sequenceId == 1) {
		ParseClientCredentials(message, packetInfo);
	}
	//SQL - �������
	else {
		ParseClientQuery(message, packetInfo);
	}
	
	return result;
}