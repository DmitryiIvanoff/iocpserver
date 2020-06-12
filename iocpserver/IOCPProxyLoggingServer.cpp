#include <iostream>
#include "IOCPProxyLoggingServer.h"

extern bool g_bEndServer;

CIOCPProxyLoggingServer::CIOCPProxyLoggingServer(int numMySQLThreads, int numClientThreads, std::string mySQLPort, std::string listenPort) {
	mySQLServer = std::make_shared<CIOCPMySQLServer>(numMySQLThreads, mySQLPort);
	clientServer = std::make_shared<CIOCPClientServer>(numClientThreads, listenPort);

	clientServer->SetHelperIOCP(mySQLServer->GetIOCP());
	mySQLServer->SetHelperIOCP(clientServer->GetIOCP());

	threadClientServer = std::thread(&CIOCPProxyLoggingServer::startClientRoutineInThread, clientServer.get());		
}

CIOCPProxyLoggingServer::~CIOCPProxyLoggingServer() {
	g_bEndServer = true;
	if (threadClientServer.joinable()) {
		threadClientServer.join();
	}
}

void CIOCPProxyLoggingServer::startClientRoutineInThread(CIOCPClientServer* routine) {
	routine->Start();
}
