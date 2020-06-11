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
	//���������� ������� �������
	static bool �trlHandler(DWORD dwEvent);
	
	SOCKET m_dMySQLSocket;

	//����� ����� ����������
	HANDLE m_hIOCP;
	//����� ����� ���������� mySQL ������
	HANDLE m_hMySQLIOCP;

	size_t m_dIncomingSequence;
	size_t m_dOutgoingSequence;
	std::map<size_t, IOContextPtr> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;

	//��������� ��� �������� ������, ��������� � ������ ����� ������������ ��������. �������� �������� � ���������� � ������� ������.
	std::vector<ClientContextPtr> m_vContexts;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerThread;
	//������� ��� ����������� ���������� � �������� �� ����������
	std::mutex m_mContextsGuard;
	std::mutex m_mThreadGuard;
	//���������� ������� ������� ������� �����
	int m_dThreadCount;
	//��������� �� ����, ����� ��� �trlHandler
	static CIOCPClientServer* currentServer;
	//����� �� ������� ����������� ������
	SOCKET m_dListenSocket;

	ClientContextPtr mysqlPointer;

	static int clientID;
};

typedef std::shared_ptr<CIOCPClientServer> IOCPClientServerPtr;
#endif
