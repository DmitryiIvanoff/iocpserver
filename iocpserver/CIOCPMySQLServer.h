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
	//���������� ������� �������
	static bool �trlHandler(DWORD dwEvent);

	//����� ����� ���������� ������, ������������� ��������
	HANDLE clientIOCP;
	//����� ����� ����������
	HANDLE m_hIOCP;

	//��������� ��� �������� ����������, ��������� � ������ ������������ ��������.
	std::vector<ClientContextPtr> m_vContexts;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerThread;
	//������� ��� ����������� ���������� � �������� �� ����������
	std::mutex m_mContextsSync;
	//���������� ������� ������� ������� �����
	int m_dThreadCount;
	//��������� �� ����, ����� ��� �trlHandler
	static CIOCPMySQLServer* currentServer;
	

	//��� ������ � mySQL:
	SOCKET m_dMySQLSocket;
};

typedef std::shared_ptr<CIOCPMySQLServer> IOCPMySQLServerPtr;