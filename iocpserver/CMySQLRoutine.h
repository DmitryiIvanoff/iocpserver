#pragma once
#include <thread>
#include <vector>
#include <memory>
#include <winsock2.h>
#include "IRoutine.h"
#include "CLogger.h"

class CMySQLRoutine;
typedef std::shared_ptr<CMySQLRoutine> MySQLRoutinePtr;

class CMySQLRoutine : public IRoutine {
public:
	static MySQLRoutinePtr GetInstance(const int threadCount, const std::string listenPort) {
		if (!currentRoutine) {
			//�������� currentRoutine ���������� � ������������, � ������������ ����� ��������� 
			//�.�. � ������������ �������� ������� ������ � ������� ����� ��� ������������������ ����� ��������� currentRoutine
			new CMySQLRoutine(threadCount, listenPort);
		}
		return currentRoutine;
	}
	~CMySQLRoutine();

	void SetHelperIOCP(const HANDLE compPort) override { clientIOCP = compPort; }

	HANDLE GetIOCP() const override { return m_hIOCP; }

private:
	CMySQLRoutine(const int threadCount, const std::string mySQLPosrt);

	//������� ��� �������� � ������ ����������
	static int WorkerThread();

	SOCKET CreateSocket(std::string port);

	bool PostToIOCP(CBuffer* buffer);

	bool RecvBuffer(SOCKET recvSocket, BufferPtr buffer);
	bool SendBuffer(SOCKET sendSocket, BufferPtr buffer);
	//���������� ������� �������
	static bool ConsoleEventHandler(DWORD dwEvent);

	void OnClientAccepted(BufferPtr buffer);
	void OnClientDataReceived(BufferPtr buffer);

private:
	//����� ����� ���������� ������, ������������� ��������
	HANDLE clientIOCP;
	//����� ����� ����������
	HANDLE m_hIOCP;
	//��������� ��� ������� �������
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;
	//���������� ������� ������� ������� �����
	size_t m_dThreadCount;
	//��������� �� ����, ����� ��� ����������� ������� ������� � WorkerThread
	static MySQLRoutinePtr currentRoutine;

	const std::string m_sMySQLPort;

	LoggerPtr m_pLogger;
};