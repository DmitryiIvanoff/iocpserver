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

/*
�����-�������� ������� ������� ��� ��������� � ��������: ����������� ������ � �������� ������ ������� ����������� ����� ���������� m_hIOCP.
�������� ��������� � ��������������� ������� CMySQLRoutine ���������� �� ������ � ��: � �� ���� ���������� �������� ������ �� �����.
*/
class CClientRoutine : public IRoutine {
public:
	
	static CClientRoutine* GetInstance(const int threadCount, const std::string listenPort);

	~CClientRoutine();
	
	//� ���� ������ ���������� ��������� ����� ���������� �� ��������: ��� ������� ������ ������� ���������� ������ - ����� � ����,
	//� ������� ������ ����� ������� � ��������������� �������.
	void Start();

	//�������� ���� ���������� ��������������� ������.
	void SetHelperIOCP(const HANDLE compPort) override { m_hMySQLIOCP = compPort; }

	HANDLE GetIOCP() const override { return m_hIOCP; }


private:

	CClientRoutine(const int threadCount, const std::string listenPort);

	CClientRoutine() = delete;

	//������� ����� �� m_mClients.
	void RemoveSession(const CBuffer* buffer);

	//�������������� �����:
	//�������� isListenSocket == true - ������� �����, �� ������� ����� ������������ ������� � ������ ���,
	//isListenSocket == false - ���������� �����, � ������� � ���������� ����� ����������� ��� �������� ������ � ������.
	SOCKET CreateSocket(const std::string port, const bool isListenSocket);
	
	//�������� ������� ��������: � ���� ������ ���������� �������� ������� ����� ���������� � � ����������� �� ���� �������� ���������� ����� �������� 
	//���������� ����������.
	static int WorkerThread(CClientRoutine* routine);

	//��� ����������� ������ ������� � ���� ������ ��������� ����� ����� � ������������� � ��������, ����� ���������� ���������� ����� ������ � ������ ����������.
	CBuffer* UpdateCompletionPort(const SOCKET sdClient);

	//������������ ��� "�������" � ��������������� ������� - ������ ������, ���������� �� ������� � ���� ���������� ��������������� ������,
	//����� � ��������������� ������ �������� �����, ������� ��������������.
	bool PostToIOCP(CBuffer* buffer);

	//�����, ���������� �� ����������� ������ �� ����� ����������.
	bool RecvBufferAsync(CBuffer* buffer);
	//�����, ���������� �� �������� ������ � ���� ���������� - ����� ������ ������� ������.
	bool SendBufferAsync(const CBuffer* buffer);

	//��������������� �����, ������������ � SendBufferAsync. ��������� �� ������� m_mBuffer ����� ��� ������ � ����� �������, ���� ������� ������� ��� �������� ������� ������.
	//����� ���������� nullptr.
	CBuffer* GetNextBuffer();

	//��������������� �����, ������������ � SendBufferAsync. ������ ���� ����� ��� � GetNextBuffer + ����������� ������� m_dOutgoingSequence.
	CBuffer* ProcessNextBuffer();

	//��������� ������ � m_mClients.
	void AddNewSession(CBuffer* buffer);

	//��������� ����� � ������� m_mBuffer ������ ��� ����� �������� ����� ������ ��� ����������� ������ �� �������.
	void AddBufferInQueue(CBuffer* buffer);

	//������� ����� �� ������� m_mBuffer ������ ��� ����� ������ ��������� ����������, ���� ���������� ������ � ������/������ � ��, ���� ����� ��� ������� � ����� �������.
	void RemoveBufferFromQueue(BufferPtr buffer);

	//���������� ������� �������, ������� Ctrl+C
	static bool ConsoleEventHandler(DWORD dwEvent);

	//������, ���������� � WorkerThread � ����������� �� ���� ������� enIOOperation ���������� ���� �� ���.
	void OnClientDataSended(CBuffer* buffer);
	void OnClientDataReceived(CBuffer* buffer);
	void OnMySQLDataReceived(CBuffer* buffer);

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
	std::map<size_t, CBuffer*> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;
	//��������� ��� �������� ������, ��������� � ������ ����� ������������ ��������. ����� �������� � ���������� � ������� ������,
	//������ ������� ����� ������ �������� � �������� ��������������� ������, ���������� mysql ��.
	std::map<size_t, BufferPtr> m_mClients;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	std::mutex m_mBufferSync;

	static CClientRoutine* currentRoutine;

	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};
