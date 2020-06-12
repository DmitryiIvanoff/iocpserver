#pragma once
#include <thread>
#include <vector>
#include <memory>
#include <winsock2.h>
#include "IServer.h"

class CIOCPMySQLServer : public IServer
{
public:
	CIOCPMySQLServer(int threadCount, std::string mySQLPosrt);

	~CIOCPMySQLServer();

	void SetHelperIOCP(HANDLE compPort) { clientIOCP = compPort; }

	HANDLE getIOCP() { return m_hIOCP; }

private:
	static int workerThread(LPVOID WorkContext);

	SOCKET CreateSocket(std::string port, bool isListenSocket);// override;

	void UpdateCompletionPort(ClientContextPtr& context, SOCKET sdClient, SOCKET sdMySQL, etIOOperation operation);// override;

	void RemoveIOContext(ClientContextPtr lpPerSocketContext);// override;

	void AddIOContext(ClientContextPtr lpPerSocketContext);// override;
	bool postContextToIOCP(CClientContext* lpPerSocketContext);

	//bool postContextToIOCP(ClientContextPtr lpPerSocketContext) override { return true; };

	bool WSASendBuffer(SOCKET sendSocket, IOContextPtr data, size_t id);
	bool WSArecvBuffer(SOCKET recvSocket, IOContextPtr data, size_t id);
	bool RecvBuffer(SOCKET recvSocket, IOContextPtr data, size_t id);// override;
	bool SendBuffer(SOCKET sendSocket, IOContextPtr data, size_t id);// override;

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
};

typedef std::shared_ptr<CIOCPMySQLServer> IOCPMySQLServerPtr;