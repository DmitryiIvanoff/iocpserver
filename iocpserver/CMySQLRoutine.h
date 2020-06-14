#pragma once
#include <thread>
#include <vector>
#include <memory>
#include <winsock2.h>
#include "IRoutine.h"
#include "CLogger.h"

class CMySQLRoutine : public IRoutine {
public:
	CMySQLRoutine(int threadCount, const std::string mySQLPosrt);

	~CMySQLRoutine();

	void SetHelperIOCP(const HANDLE compPort) { clientIOCP = compPort; }

	HANDLE GetIOCP() { return m_hIOCP; }

private:
	//хендлит все операции с портом завершения
	static int WorkerThread();

	SOCKET CreateSocket(std::string port);

	bool PostToIOCP(CBuffer* lpPerSocketContext);

	bool RecvBuffer(SOCKET recvSocket, BufferPtr buffer);
	bool SendBuffer(SOCKET sendSocket, BufferPtr buffer);
	//обработчик событий консоли
	static bool ConsoleEventHandler(DWORD dwEvent);

	void OnClientAccepted(BufferPtr buffer);
	void OnClientDataReceived(BufferPtr buffer);

private:
	//хендл порта завершения рутины, обслуживающей клиентов
	HANDLE clientIOCP;
	//хендл порта завершения
	HANDLE m_hIOCP;
	//контейнер для рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;
	//количество потоков которое создаст класс
	size_t m_dThreadCount;
	//указатель на себя, нужен для обработчика событий консоли и WorkerThread
	static CMySQLRoutine* currentRoutine;

	const std::string m_sMySQLPort;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CMySQLRoutine> MySQLRoutinePtr;