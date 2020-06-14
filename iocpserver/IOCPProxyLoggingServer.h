#pragma once
#include "CClientRoutine.h"
#include "CMySQLRoutine.h"


class CIOCPProxyLoggingServer {
public:
	CIOCPProxyLoggingServer(const int numMySQLThreads, const int numClientThreads, std::string mySQLPort, std::string listenPort);

	~CIOCPProxyLoggingServer();

	static void startClientRoutineInThread(CClientRoutine* routine);

private:
	ClientRoutinePtr clientRoutine;
	MySQLRoutinePtr mySQLRoutine;
	std::thread threadClientRoutine;
};