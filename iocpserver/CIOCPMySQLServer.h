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

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CIOCPMySQLServer> IOCPMySQLServerPtr;