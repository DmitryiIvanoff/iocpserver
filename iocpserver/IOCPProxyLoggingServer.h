#pragma once
#include "CIOCPClientServer.h"
#include "CIOCPMySQLServer.h"


class CIOCPProxyLoggingServer {
public:
	CIOCPProxyLoggingServer(int numMySQLThreads, int numClientThreads, std::string mySQLPort, std::string listenPort);

	~CIOCPProxyLoggingServer();

	static void startClientRoutineInThread(CIOCPClientServer* routine);

private:
	IOCPClientServerPtr clientServer;
	IOCPMySQLServerPtr mySQLServer;
	std::thread threadClientServer;
};