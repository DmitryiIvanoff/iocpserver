#pragma once
#include <string>

struct mysqlPacketInfo;

class CParser
{
public:
	bool Parse(std::string& message);

private:
	void ParseClientQuery(std::string& message, const mysqlPacketInfo packetInfo);
	
	void ParseClientCredentials(std::string& message, const mysqlPacketInfo packetInfo);
};

