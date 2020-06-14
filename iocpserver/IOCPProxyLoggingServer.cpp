#include <iostream>
#include "IOCPProxyLoggingServer.h"

extern bool g_bEndServer;

CIOCPProxyLoggingServer::CIOCPProxyLoggingServer(const int numMySQLThreads, const int numClientThreads, const std::string mySQLPort, const std::string listenPort) {
	mySQLRoutine = std::make_shared<CMySQLRoutine>(numMySQLThreads, mySQLPort);
	clientRoutine = std::make_shared<CClientRoutine>(numClientThreads, listenPort);

	clientRoutine->SetHelperIOCP(mySQLRoutine->GetIOCP());
	mySQLRoutine->SetHelperIOCP(clientRoutine->GetIOCP());

	threadClientRoutine = std::thread(&CIOCPProxyLoggingServer::startClientRoutineInThread, clientRoutine.get());		
}

CIOCPProxyLoggingServer::~CIOCPProxyLoggingServer() {
	g_bEndServer = true;
	if (threadClientRoutine.joinable()) {
		threadClientRoutine.join();
	}
}

void CIOCPProxyLoggingServer::startClientRoutineInThread(CClientRoutine* routine) {
	routine->Start();
}
