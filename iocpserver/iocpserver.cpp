#include "IOCPProxyLoggingServer.h"

static std::string g_sClientPort = "4000";
static std::string g_sMySQLPort = "3306";
extern bool g_bEndServer = false;

int main()
{
	std::shared_ptr<CIOCPProxyLoggingServer> server(new CIOCPProxyLoggingServer(16, 10, g_sMySQLPort, g_sClientPort));
	getchar();
}