#include <iostream>
#include "IOCPProxyLoggingServer.h"

extern bool g_bEndServer;

CIOCPProxyLoggingServer::CIOCPProxyLoggingServer(const int numMySQLThreads, const int numClientThreads, const std::string mySQLPort, const std::string listenPort) {
	m_pMySQLRoutine = CMySQLRoutine::GetInstance(numMySQLThreads, mySQLPort);
	m_pClientRoutine = CClientRoutine::GetInstance(numClientThreads, listenPort);

	m_pClientRoutine->SetHelperIOCP(m_pMySQLRoutine->GetIOCP());
	m_pMySQLRoutine->SetHelperIOCP(m_pClientRoutine->GetIOCP());

	m_tForClientRoutine = std::thread(&CIOCPProxyLoggingServer::StartClientRoutineInThread, m_pClientRoutine);		
}

CIOCPProxyLoggingServer::~CIOCPProxyLoggingServer() {
	g_bEndServer = true;
	if (m_tForClientRoutine.joinable()) {
		m_tForClientRoutine.join();
	}
	if (m_pClientRoutine) {
		delete m_pClientRoutine;
	};

	if (m_pMySQLRoutine) {
		delete m_pMySQLRoutine;
	}
}

void CIOCPProxyLoggingServer::StartClientRoutineInThread(CClientRoutine* routine) {
	routine->Start();
}
