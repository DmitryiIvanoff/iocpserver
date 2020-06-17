#pragma once
#include "CClientRoutine.h"
#include "CMySQLRoutine.h"


class CIOCPProxyLoggingServer {
public:
	CIOCPProxyLoggingServer(const int numMySQLThreads, const int numClientThreads, std::string mySQLPort, std::string listenPort);

	~CIOCPProxyLoggingServer();

	//запускаем метод CClientRoutine::Run  в отдельном потоке чтобы не блокировать основной.
	static void StartClientRoutineInThread(CClientRoutine* routine);

private:
	//NOTE: классы CMySQLRoutine и CClientRoutine синглтоны.
	CClientRoutine* m_pClientRoutine;
	CMySQLRoutine* m_pMySQLRoutine;
	std::thread m_tForClientRoutine;
};