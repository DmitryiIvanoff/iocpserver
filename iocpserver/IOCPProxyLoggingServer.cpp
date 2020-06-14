#include <iostream>
#include "IOCPProxyLoggingServer.h"

extern bool g_bEndServer;

CIOCPProxyLoggingServer::CIOCPProxyLoggingServer(const int numMySQLThreads, const int numClientThreads, const std::string mySQLPort, const std::string listenPort) {
	mySQLRoutine = CMySQLRoutine::GetInstance(numMySQLThreads, mySQLPort);
	clientRoutine = CClientRoutine::GetInstance(numClientThreads, listenPort);

	clientRoutine->SetHelperIOCP(mySQLRoutine->GetIOCP());
	mySQLRoutine->SetHelperIOCP(clientRoutine->GetIOCP());

	threadClientRoutine = std::thread(&CIOCPProxyLoggingServer::startClientRoutineInThread, clientRoutine.get());		
}

CIOCPProxyLoggingServer::~CIOCPProxyLoggingServer() {
	g_bEndServer = true;
	if (threadClientRoutine.joinable()) {
		threadClientRoutine.join();
	}
	clientRoutine.reset();
	mySQLRoutine.reset();
}

void CIOCPProxyLoggingServer::startClientRoutineInThread(CClientRoutine* routine) {
	routine->Start();
}
