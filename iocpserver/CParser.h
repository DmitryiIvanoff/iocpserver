#pragma once
#include <string>
#include "CClientContext.h"

class CParser
{
public:
	bool Parse(std::string& message, enIOOperation opCode);

private:
	void ParseClientQuery(std::string& message);
	
	void ParseClientCredentials(std::string& message);
};

