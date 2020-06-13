#pragma once
#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <winsock2.h>
#include "IServer.h"
#include "CLogger.h"

//�����, ���������� �� ������������ ���������� �����������.
class CIOCPClientServer : public IServer{
public:
	CIOCPClientServer(int threadCount, const std::string listenPort);

	~CIOCPClientServer();
	
	//� ���� ������ ���������� ��������� ����� ���������� �� ��������.
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
	//���������� ������� �������
	static bool ConsoleEventHandler(DWORD dwEvent);

private:

	//����� �� ������� ����������� ������
	SOCKET m_dListenSocket;
	SOCKET m_dMySQLSocket;
	//���������� ������� ������� ������� �����
	size_t m_dThreadCount;

	size_t m_dIncomingSequence;
	size_t m_dOutgoingSequence;
	
	//����� ����� ����������
	HANDLE m_hIOCP;
	//����� ����� ���������� mySQL ������
	HANDLE m_hMySQLIOCP;
	
	std::map<size_t, IOContextPtr> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;
	//��������� ��� �������� ������, ��������� � ������ ����� ������������ ��������. �������� �������� � ���������� � ������� ������.
	std::vector<ClientContextPtr> m_vConnectedClients;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	//������� ��� ����������� ���������� � �������� �� ����������
	std::mutex m_mContextsSync;
	std::mutex m_mThreadSync;
	
	//��������� �� ����, ����� ��� �trlHandler
	static CIOCPClientServer* currentServer;
	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CIOCPClientServer> IOCPClientServerPtr;
