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
	//���������� ������� �������
	static bool ConsoleEventHandler(DWORD dwEvent);

private:
	//����� ����� ���������� ������, ������������� ��������
	HANDLE clientIOCP;
	//����� ����� ����������
	HANDLE m_hIOCP;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;
	//���������� ������� ������� ������� �����
	size_t m_dThreadCount;
	//��������� �� ����, ����� ��� �trlHandler
	static CIOCPMySQLServer* currentServer;

	const std::string m_sMySQLPort;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CIOCPMySQLServer> IOCPMySQLServerPtr;