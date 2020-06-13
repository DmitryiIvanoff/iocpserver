#pragma once
#include <string>
#include "CClientContext.h"

const size_t MYSQL_PACKET_HEADER_LEN = 4;
const size_t MYSQL_COMMAND_LEN = 1;
const size_t MYSQL_DB_NAME_LEN = 1024;

class CParser
{
public:
	bool Parse(std::string& message, enIOOperation opCode);

private:
	void ParseClientMessage(std::string& message);
	
	void ParseServerMessage(std::string& message);
};

