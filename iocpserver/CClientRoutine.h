#pragma once
#include <thread>
#include <vector>
#include <map>
#include <memory>
#include <winsock2.h>
#include "IRoutine.h"
#include "CLogger.h"

// ласс, отвечающий за обслуживание клиентских подключений.
class CClientRoutine : public IRoutine {
public:
	CClientRoutine(int threadCount, const std::string listenPort);

	~CClientRoutine();
	
	//¬ этом методе происходит обработка новых срединений от клиентов: дл€ каждого нового клиента выдел€етс€ пам€ть - буфер в куче,
	//в течении сессии буфер шаритс€ между подпрограммами-рутинами.
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
	std::vector<BufferPtr> m_vConnectedClients;
	//контейнер дл€ рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	std::mutex m_mBufferSync;
	
	//указатель на себ€, нужен дл€ обработчика событий консоли
	static CClientRoutine* currentRoutine;
	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};

typedef std::shared_ptr<CClientRoutine> ClientRoutinePtr;
