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

// ласс, отвечающий за обслуживание клиентских подключений.
class CClientRoutine : public IRoutine {
public:
	
	static ClientRoutinePtr GetInstance(const int threadCount, const std::string listenPort);

	~CClientRoutine();
	
	//¬ этом методе происходит обработка новых срединений от клиентов: дл€ каждого нового клиента выдел€етс€ пам€ть - буфер в куче,
	//в течении сессии буфер шаритс€ между подпрограммами-рутинами.
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
	//обработчик событий консоли
	static bool ConsoleEventHandler(DWORD dwEvent);

	void OnClientDataSended(BufferPtr buffer);
	void OnClientDataReceived(BufferPtr buffer);
	void OnMySQLDataReceived(BufferPtr buffer);

private:

	//сокет на который коннектитс€ клиент
	SOCKET m_dListenSocket;
	//количество потоков которое создаст класс
	size_t m_dThreadCount;
	//переменные, нужные дл€ организации очереди
	size_t m_dIncomingSequence;
	size_t m_dOutgoingSequence;
	
	//хендл порта завершени€
	HANDLE m_hIOCP;
	//хендл порта завершени€ mySQL рутины
	HANDLE m_hMySQLIOCP;
	//очередь
	std::map<size_t, BufferPtr> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;
	//контейнер дл€ хранени€ данных, св€занных с каждым вновь подключенным клиентом. Ѕуфер хранитс€ в контейнере в течении сессии,
	//данные шар€тс€ между своими потоками и потоками вспомогательной рутины, работающей mysql Ѕƒ.
	std::map<size_t, BufferPtr> m_mClients;
	//контейнер дл€ рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	std::mutex m_mBufferSync;

	static ClientRoutinePtr currentRoutine;

	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};
