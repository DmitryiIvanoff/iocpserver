#pragma once
#include "CClientRoutine.h"
#include "CMySQLRoutine.h"


class CIOCPProxyLoggingServer {
public:
	CIOCPProxyLoggingServer(const int numMySQLThreads, const int numClientThreads, std::string mySQLPort, std::string listenPort);

	~CIOCPProxyLoggingServer();

	//��������� ����� CClientRoutine::Run  � ��������� ������ ����� �� ����������� ��������.
	static void StartClientRoutineInThread(CClientRoutine* routine);

private:
	//NOTE: ������ CMySQLRoutine � CClientRoutine ���������.
	CClientRoutine* m_pClientRoutine;
	CMySQLRoutine* m_pMySQLRoutine;
	std::thread m_tForClientRoutine;
};