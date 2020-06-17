#pragma once
#include <thread>
#include <vector>
#include <memory>
#include <winsock2.h>
#include "IRoutine.h"
#include "CLogger.h"

class CMySQLRoutine;
typedef std::shared_ptr<CMySQLRoutine> MySQLRoutinePtr;

/*
Класс-синглтон который хендлит все операции с БД: чтение и запись из БД. 
Работает совместно с вспомогательной рутиной CClientRoutine, отвечающей за работу с БД: в ее порт завершения пассятся ссылки на буфер.
NOTE: большие данные пишутся напрямую в сокет клиента минуя вспомогательную рутину.
*/
class CMySQLRoutine : public IRoutine {
public:

	static CMySQLRoutine* GetInstance(const int threadCount, const std::string listenPort);

	~CMySQLRoutine();
	//Ассайним порт завершения вспомогательной рутины.
	void SetHelperIOCP(const HANDLE compPort) override { m_hClientIOCP = compPort; }

	HANDLE GetIOCP() const override { return m_hIOCP; }

private:
	CMySQLRoutine(const int threadCount, const std::string mySQLPosrt);

	CMySQLRoutine() = delete;

	//Основная рабочая нагрузка: в этом методе происходит хендлинг ивентов порта завершения и в зависимости от кода операции выбирается какое действие 
	//необходимо произвести. Данные в порт завершения пишутся вспомогательной рутиной.
	static int WorkerThread(CMySQLRoutine* routine);

	SOCKET CreateSocket(std::string port);

	//Используется для "общения" с вспомогательной рутиной - пассит данные, полученные от БД в порт завершения вспомогательной рутины,
	//далее в вспомогательной рутине райзится ивент, который обрабатывается в соответствии с кодо порерации.
	bool PostToIOCP(CBuffer* buffer);

	//синхронное чтение из сокета БД
	bool RecvBuffer(SOCKET recvSocket, CBuffer* buffer);
	//синхронная запись в сокет БД.
	bool SendBuffer(SOCKET sendSocket, CBuffer* buffer);
	//обработчик событий консоли
	static bool ConsoleEventHandler(DWORD dwEvent);

	//Методы, вызываемые в WorkerThread в зависимости от кода опреции enIOOperation вызывается один из них.
	void OnClientAccepted(CBuffer* buffer);
	void OnClientDataReceived(CBuffer* buffer);

private:
	//хендл порта завершения рутины, обслуживающей клиентов
	HANDLE m_hClientIOCP;
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