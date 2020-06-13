#pragma once
#include <thread>
#include <vector>
#include <memory>
#include <winsock2.h>
#include "IServer.h"
#include "CLogger.h"

class CIOCPMySQLServer : public IServer
{
public:
	CIOCPMySQLServer(int threadCount, const std::string mySQLPosrt);

	~CIOCPMySQLServer();

	void SetHelperIOCP(HANDLE compPort) { clientIOCP = compPort; }

	HANDLE GetIOCP() { return m_hIOCP; }

private:
	static int WorkerThread(LPVOID WorkContext);

	SOCKET CreateSocket(std::string port);

	bool PostToIOCP(CClientContext* lpPerSocketContext);

	bool RecvBuffer(SOCKET recvSocket, IOContextPtr buffer);
	bool SendBuffer(SOCKET sendSocket, IOContextPtr buffer);
	//обработчик событий консоли
	static bool ConsoleEventHandler(DWORD dwEvent);

private:
	//хендл порта завершения рутины, обслуживающей клиентов
	HANDLE clientIOCP;
	//хендл порта завершения
	HANDLE m_hIOCP;
	//контейнер для рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;
	//количество потоков которое создаст класс
	size_t m_dThreadCount;
	//указатель на себя, нужен для сtrlHandler
	static CIOCPMySQLServer* currentServer;

	const std::string m_sMySQLPort;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CIOCPMySQLServer> IOCPMySQLServerPtr;