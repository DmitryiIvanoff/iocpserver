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
			//Значение currentRoutine присвоится в конструкторе, я зимплементил такое поведение 
			//т.к. в конструкторе стартуют рабочие потоки в которых нужен уже инициализированный умный указатель currentRoutine
			new CMySQLRoutine(threadCount, listenPort);
		}
		return currentRoutine;
	}
	~CMySQLRoutine();

	void SetHelperIOCP(const HANDLE compPort) override { clientIOCP = compPort; }

	HANDLE GetIOCP() const override { return m_hIOCP; }

private:
	CMySQLRoutine(const int threadCount, const std::string mySQLPosrt);

	//хендлит все операции с портом завершения
	static int WorkerThread();

	SOCKET CreateSocket(std::string port);

	bool PostToIOCP(CBuffer* buffer);

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
	static MySQLRoutinePtr currentRoutine;

	const std::string m_sMySQLPort;

	LoggerPtr m_pLogger;
};