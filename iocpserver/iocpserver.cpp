#include <iostream>
#include <string>
#include "IOCPProxyLoggingServer.h"

extern bool g_bEndServer = false;
static std::string g_sClientPort = "4000";
static std::string g_sMySQLPort = "3306";
const int g_dDBThreadsCount = 16;
const int g_dClientThreadsCount = 10;

std::string g_sInstruction = "To end execution press Ctrl + C";

int main()
{
	std::cout << g_sInstruction;
	std::shared_ptr<CIOCPProxyLoggingServer> server(new CIOCPProxyLoggingServer(g_dDBThreadsCount, g_dClientThreadsCount, g_sMySQLPort, g_sClientPort));
	getchar();
}