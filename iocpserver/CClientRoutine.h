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
Класс-синглтон который хендлит все опрерации с клиентом: асинхронные чтение и отправка данных клиенту посредством порта завершения m_hIOCP.
Работает совместно с вспомогательной рутиной CMySQLRoutine отвечающей за работу с БД: в ее порт завершения пассятся ссылки на буфер.
*/
class CClientRoutine : public IRoutine {
public:
	
	static CClientRoutine* GetInstance(const int threadCount, const std::string listenPort);

	~CClientRoutine();
	
	//В этом методе происходит обработка новых срединений от клиентов: для каждого нового клиента выделяется память - буфер в куче,
	//в течении сессии буфер шарится с вспомогательной рутиной.
	void Start();

	//Ассайним порт завершения вспомогательной рутины.
	void SetHelperIOCP(const HANDLE compPort) override { m_hMySQLIOCP = compPort; }

	HANDLE GetIOCP() const override { return m_hIOCP; }


private:

	CClientRoutine(const int threadCount, const std::string listenPort);

	CClientRoutine() = delete;

	//Удаляем буфер из m_mClients.
	void RemoveSession(const CBuffer* buffer);

	//Инициализируем сокет:
	//Параметр isListenSocket == true - создаем сокет, на который будут коннектиться клиенты в первый раз,
	//isListenSocket == false - инициируем сокет, с которым в дальнейшем будет происходить все опреации чтения и записи.
	SOCKET CreateSocket(const std::string port, const bool isListenSocket);
	
	//Основная рабочая нагрузка: в этом методе происходит хендлинг ивентов порта завершения и в зависимости от кода операции выбирается какое действие 
	//необходимо произвести.
	static int WorkerThread(CClientRoutine* routine);

	//Про подключении нового клиента в этом методе создается новый сокет и ассоциируется с клиентом, также происходит связывание этого сокета с портом завершения.
	CBuffer* UpdateCompletionPort(const SOCKET sdClient);

	//Используется для "общения" с вспомогательной рутиной - пассит данные, полученные от клиента в порт завершения вспомогательной рутины,
	//далее в вспомогательной рутине райзится ивент, который обрабатывается.
	bool PostToIOCP(CBuffer* buffer);

	//Метод, отвечающий за асинхронное чтение из порта завершения.
	bool RecvBufferAsync(CBuffer* buffer);
	//Метод, отвечающий за асинхрую запись в порт завершения - далее данные считает клиент.
	bool SendBufferAsync(const CBuffer* buffer);

	//Вспомогательный метод, используется в SendBufferAsync. Извлекает из очереди m_mBuffer пакет для записи в сокет клиента, если подошла очередь для отправки данного буфера.
	//Иначе возвращает nullptr.
	CBuffer* GetNextBuffer();

	//Вспомогательный метод, используется в SendBufferAsync. Делает тоже самое что и GetNextBuffer + инкрементит счетчик m_dOutgoingSequence.
	CBuffer* ProcessNextBuffer();

	//добавляем сессию в m_mClients.
	void AddNewSession(CBuffer* buffer);

	//добавляет буфер в очередь m_mBuffer всякий раз когда приходит новый клиент или считываются данные от клиента.
	void AddBufferInQueue(CBuffer* buffer);

	//удаляем буфер из очереди m_mBuffer всякий раз когда клиент разрывает соединений, либо происходит ошибка в чтении/записи с БД, либо буфер был записан в сокет клиента.
	void RemoveBufferFromQueue(BufferPtr buffer);

	//обработчик событий консоли, хендлим Ctrl+C
	static bool ConsoleEventHandler(DWORD dwEvent);

	//Методы, вызываемые в WorkerThread в зависимости от кода опреции enIOOperation вызывается один из них.
	void OnClientDataSended(CBuffer* buffer);
	void OnClientDataReceived(CBuffer* buffer);
	void OnMySQLDataReceived(CBuffer* buffer);

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
	std::map<size_t, CBuffer*> m_mBuffer;
	std::vector<size_t> m_vRemovedBufferNumbers;
	//контейнер для хранения данных, связанных с каждым вновь подключенным клиентом. Буфер хранится в контейнере в течении сессии,
	//данные шарятся между своими потоками и потоками вспомогательной рутины, работающей mysql БД.
	std::map<size_t, BufferPtr> m_mClients;
	//контейнер для рабочих потоков
	std::vector<std::shared_ptr<std::thread>> m_vWorkerPayloads;

	std::mutex m_mBufferSync;

	static CClientRoutine* currentRoutine;

	static size_t m_dSessionId;

	LoggerPtr m_pLogger;
};
