#ifndef IOCPSERVER_H
#define IOCPSERVER_H

#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <winsock2.h>
#include "IServer.h"

class CIOCPClientServer : public IServer{
public:
	CIOCPClientServer(int threadCount, std::string listenPort, std::string mySQLIOCP = "3306");

	~CIOCPClientServer();

	void start();

	void setMySQLIOCP(HANDLE iocp) { m_hMySQLIOCP = iocp; }

	HANDLE getIOCP() { return m_hIOCP; }


private:


	SOCKET createSocket(std::string port, bool isListenSocket) override;

	static int workerThread(LPVOID WorkContext);

	void updateCompletionPort(ClientContextPtr& context, SOCKET sdClient, SOCKET sdMySQL, etIOOperation operation) override;

	void removeIOContext(ClientContextPtr lpPerSocketContext) override;

	void addIOContext(ClientContextPtr lpPerSocketContext) override;

	bool postContextToIOCP(CClientContext* lpPerSocketContext);

	bool WSArecvBuffer(SOCKET recvSocket, IOContextPtr buffer, size_t id);
	bool WSAsendBuffer(SOCKET recvSocket, IOContextPtr buffer, size_t id);

	bool recvBuffer(SOCKET recvSocket, IOContextPtr buffer, size_t id) override;
	bool sendBuffer(SOCKET sendSocket, IOContextPtr buffer, size_t id) override;

	IOContextPtr getNextBuffer(IOContextPtr buffer);

	void addBufferInMap(IOContextPtr& buffer);

	void removeBufferFromMap(IOContextPtr buffer);

	IOContextPtr processNextBuffer(IOContextPtr buffer);
	//обработчик событий консоли
	static bool сtrlHandler(DWORD dwEvent);
	
	SOCKET m_dMySQLSocket;

	//хендл порта завершения
	HANDLE m_hIOCP;
	//хендл порта завершения mySQL рутины
	HANDLE m_hMySQLIOCP;

	size_t m_dIncomingSequence;
	size_t m_dOutgoingSequence;
	std::map<size_t, IOContextPtr> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;

	//контейнер для хранения данных, связанных с каждым вновь подключенным клиентом. Контекст хранится в контейнере в течении сессии.
	std::vector<ClientContextPtr> m_vContexts;
	//контейнер для рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerThread;
	//мьютекс для безопасного добавления и удаления из контейнера
	std::mutex m_mContextsGuard;
	std::mutex m_mThreadGuard;
	//количество потоков которое создаст класс
	int m_dThreadCount;
	//указатель на себя, нужен для сtrlHandler
	static CIOCPClientServer* currentServer;
	//сокет на который коннектится клиент
	SOCKET m_dListenSocket;

	ClientContextPtr mysqlPointer;

	static int clientID;
};

typedef std::shared_ptr<CIOCPClientServer> IOCPClientServerPtr;
#endif
