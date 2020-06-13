#pragma once
#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <winsock2.h>
#include "IServer.h"
#include "CLogger.h"

//Класс, отвечающий за обслуживание клиентских подключений.
class CIOCPClientServer : public IServer{
public:
	CIOCPClientServer(int threadCount, const std::string listenPort);

	~CIOCPClientServer();
	
	//В этом методе происходит обработка новых срединений от клиентов.
	void Start();

	void SetHelperIOCP(HANDLE compPort) { m_hMySQLIOCP = compPort; }

	HANDLE GetIOCP() const { return m_hIOCP; }


private:
	void AddNewSession(ClientContextPtr lpPerSocketContext);

	void RemoveSession(ClientContextPtr lpPerSocketContext);

	SOCKET CreateSocket(const std::string port, const bool isListenSocket);

	static int WorkerThread(LPVOID WorkContext);

	void UpdateCompletionPort(ClientContextPtr& context, SOCKET sdClient);

	bool PostToIOCP(CClientContext* lpPerSocketContext);

	bool RecvBufferAsync(IOContextPtr buffer);
	bool SendBufferAsync(IOContextPtr buffer);

	IOContextPtr GetNextBuffer();

	void AddBufferInQueue(IOContextPtr& buffer);

	void RemoveBufferFromQueue(IOContextPtr buffer);

	IOContextPtr ProcessNextBuffer();
	//обработчик событий консоли
	static bool ConsoleEventHandler(DWORD dwEvent);

private:

	//сокет на который коннектится клиент
	SOCKET m_dListenSocket;
	SOCKET m_dMySQLSocket;
	//количество потоков которое создаст класс
	size_t m_dThreadCount;

	size_t m_dIncomingSequence;
	size_t m_dOutgoingSequence;
	
	//хендл порта завершения
	HANDLE m_hIOCP;
	//хендл порта завершения mySQL рутины
	HANDLE m_hMySQLIOCP;
	
	std::map<size_t, IOContextPtr> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;
	//контейнер для хранения данных, связанных с каждым вновь подключенным клиентом. Контекст хранится в контейнере в течении сессии.
	std::vector<ClientContextPtr> m_vConnectedClients;
	//контейнер для рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	//мьютекс для безопасного добавления и удаления из контейнера
	std::mutex m_mContextsSync;
	std::mutex m_mThreadSync;
	
	//указатель на себя, нужен для сtrlHandler
	static CIOCPClientServer* currentServer;
	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CIOCPClientServer> IOCPClientServerPtr;
