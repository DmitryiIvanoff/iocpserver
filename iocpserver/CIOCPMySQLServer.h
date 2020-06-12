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
	CIOCPMySQLServer(int threadCount, std::string mySQLPosrt);

	~CIOCPMySQLServer();

	void SetHelperIOCP(HANDLE compPort) { clientIOCP = compPort; }

	HANDLE GetIOCP() { return m_hIOCP; }

private:
	static int workerThread(LPVOID WorkContext);

	SOCKET CreateSocket(std::string port);

	void UpdateCompletionPort(ClientContextPtr& context, SOCKET sdClient, SOCKET sdMySQL, etIOOperation operation);

	bool PostToIOCP(CClientContext* lpPerSocketContext);

	bool RecvBuffer(SOCKET recvSocket, IOContextPtr data);
	bool SendBuffer(SOCKET sendSocket, IOContextPtr data);

private:
	//обработчик событий консоли
	static bool сtrlHandler(DWORD dwEvent);

	//хендл порта завершения рутины, обслуживающей клиентов
	HANDLE clientIOCP;
	//хендл порта завершения
	HANDLE m_hIOCP;

	//контейнер для хранения контекстов, связанных с каждым подключенным клиентом.
	std::vector<ClientContextPtr> m_vContexts;
	//контейнер для рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerThread;
	//мьютекс для безопасного добавления и удаления из контейнера
	std::mutex m_mContextsSync;
	//количество потоков которое создаст класс
	int m_dThreadCount;
	//указатель на себя, нужен для сtrlHandler
	static CIOCPMySQLServer* currentServer;
	

	//для работы с mySQL:
	SOCKET m_dMySQLSocket;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CIOCPMySQLServer> IOCPMySQLServerPtr;