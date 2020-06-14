#pragma once
#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <winsock2.h>
#include "IRoutine.h"
#include "CLogger.h"

//�����, ���������� �� ������������ ���������� �����������.
class CClientRoutine : public IRoutine {
public:
	CClientRoutine(int threadCount, const std::string listenPort);

	~CClientRoutine();
	
	//� ���� ������ ���������� ��������� ����� ���������� �� ��������: ��� ������� ������ ������� ���������� ������ - ����� � ����,
	//� ������� ������ ����� ������� ����� ��������������-��������.
	void Start();

	void SetHelperIOCP(const HANDLE compPort) { m_hMySQLIOCP = compPort; }

	HANDLE GetIOCP() const { return m_hIOCP; }


private:
	void AddNewSession(BufferPtr lpPerSocketContext);

	void RemoveSession(BufferPtr lpPerSocketContext);

	SOCKET CreateSocket(const std::string port, const bool isListenSocket);

	static int WorkerThread();

	void UpdateCompletionPort(BufferPtr& context, SOCKET sdClient);

	bool PostToIOCP(CBuffer* lpPerSocketContext);

	bool RecvBufferAsync(BufferPtr buffer);
	bool SendBufferAsync(BufferPtr buffer);

	BufferPtr GetNextBuffer();

	void AddBufferInQueue(BufferPtr& buffer);

	void RemoveBufferFromQueue(BufferPtr buffer);

	BufferPtr ProcessNextBuffer();
	//���������� ������� �������
	static bool ConsoleEventHandler(DWORD dwEvent);

	void OnClientDataSended(BufferPtr buffer);
	void OnClientDataReceived(BufferPtr buffer);
	void OnMySQLDataReceived(BufferPtr buffer);

private:

	//����� �� ������� ����������� ������
	SOCKET m_dListenSocket;
	//���������� ������� ������� ������� �����
	size_t m_dThreadCount;
	//����������, ������ ��� ����������� �������
	size_t m_dIncomingSequence;
	size_t m_dOutgoingSequence;
	
	//����� ����� ����������
	HANDLE m_hIOCP;
	//����� ����� ���������� mySQL ������
	HANDLE m_hMySQLIOCP;
	//�������
	std::map<size_t, BufferPtr> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;
	//��������� ��� �������� ������, ��������� � ������ ����� ������������ ��������. ����� �������� � ���������� � ������� ������,
	//������ ������� ����� ������ �������� � �������� ��������������� ������, ���������� mysql ��.
	std::vector<BufferPtr> m_vConnectedClients;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	std::mutex m_mBufferSync;
	
	//��������� �� ����, ����� ��� ����������� ������� �������
	static CClientRoutine* currentRoutine;
	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CClientRoutine> ClientRoutinePtr;
