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

//Класс, отвечающий за обслуживание клиентских подключений.
class CClientRoutine : public IRoutine {
public:
	
	static ClientRoutinePtr GetInstance(const int threadCount, const std::string listenPort) {
		if (!currentRoutine) {
			//Значение currentRoutine присвоится в конструкторе, я зимплементил такое поведение 
			//т.к. в конструкторе стартуют рабочие потоки в которых нужен уже инициализированный умный указатель currentRoutine
			new CClientRoutine(threadCount, listenPort);
		}
		return currentRoutine;
	}

	~CClientRoutine();
	
	//В этом методе происходит обработка новых срединений от клиентов: для каждого нового клиента выделяется память - буфер в куче,
	//в течении сессии буфер шарится между подпрограммами-рутинами.
	void Start();

	void SetHelperIOCP(const HANDLE compPort) override { m_hMySQLIOCP = compPort; }

	HANDLE GetIOCP() const override { return m_hIOCP; }


private:

	CClientRoutine(const int threadCount, const std::string listenPort);

	void AddNewSession(BufferPtr buffer);

	void RemoveSession(BufferPtr buffer);

	SOCKET CreateSocket(const std::string port, const bool isListenSocket);

	static int WorkerThread();

	void UpdateCompletionPort(BufferPtr& buffer, SOCKET sdClient);

	bool PostToIOCP(CBuffer* buffer);

	bool RecvBufferAsync(BufferPtr buffer);
	bool SendBufferAsync(BufferPtr buffer);

	BufferPtr GetNextBuffer();

	void AddBufferInQueue(BufferPtr& buffer);

	void RemoveBufferFromQueue(BufferPtr buffer);

	BufferPtr ProcessNextBuffer();
	//обработчик событий консоли
	static bool ConsoleEventHandler(DWORD dwEvent);

	void OnClientDataSended(BufferPtr buffer);
	void OnClientDataReceived(BufferPtr buffer);
	void OnMySQLDataReceived(BufferPtr buffer);

private:

	//сокет на который коннектится клиент
	SOCKET m_dListenSocket;
	//количество потоков которое создаст класс
	size_t m_dThreadCount;
	//переменные, нужные для организации очереди
	size_t m_dIncomingSequence;
	size_t m_dOutgoingSequence;
	
	//хендл порта завершения
	HANDLE m_hIOCP;
	//хендл порта завершения mySQL рутины
	HANDLE m_hMySQLIOCP;
	//очередь
	std::map<size_t, BufferPtr> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;
	//контейнер для хранения данных, связанных с каждым вновь подключенным клиентом. Буфер хранится в контейнере в течении сессии,
	//данные шарятся между своими потоками и потоками вспомогательной рутины, работающей mysql БД.
	std::vector<BufferPtr> m_vConnectedClients;
	//контейнер для рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	std::mutex m_mBufferSync;

	static ClientRoutinePtr currentRoutine;

	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};
