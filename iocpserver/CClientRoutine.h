#pragma once
#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <winsock2.h>
#include "IRoutine.h"
#include "CLogger.h"

class CClientRoutine;
typedef std::shared_ptr<CClientRoutine> ClientRoutinePtr;

//�����, ���������� �� ������������ ���������� �����������.
class CClientRoutine : public IRoutine {
public:
	
	static ClientRoutinePtr GetInstance(const int threadCount, const std::string listenPort);

	~CClientRoutine();
	
	//� ���� ������ ���������� ��������� ����� ���������� �� ��������: ��� ������� ������ ������� ���������� ������ - ����� � ����,
	//� ������� ������ ����� ������� ����� ��������������-��������.
	void Start();

	void SetHelperIOCP(const HANDLE compPort) override { m_hMySQLIOCP = compPort; }

	HANDLE GetIOCP() const override { return m_hIOCP; }


private:

	CClientRoutine(const int threadCount, const std::string listenPort);

	CClientRoutine() = delete;

	void RemoveSession(BufferPtr buffer);

	SOCKET CreateSocket(const std::string port, const bool isListenSocket);

	static int WorkerThread();

	void UpdateCompletionPort(BufferPtr& buffer, const SOCKET sdClient);

	bool PostToIOCP(CBuffer* buffer);

	bool RecvBufferAsync(BufferPtr buffer);
	bool SendBufferAsync(const BufferPtr buffer);

	BufferPtr GetNextBuffer();

	void AddNewSession(const BufferPtr buffer);
	void AddBufferInQueue(const BufferPtr buffer);

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
	std::map<size_t, BufferPtr> m_mClients;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	std::mutex m_mBufferSync;

	static ClientRoutinePtr currentRoutine;

	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};
