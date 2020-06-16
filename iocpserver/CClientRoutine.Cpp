#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
#include <sstream>
#include "CClientRoutine.h"

#pragma warning( disable : 4996)

extern bool g_bEndServer;

ClientRoutinePtr CClientRoutine::currentRoutine = nullptr;

size_t CClientRoutine::m_dSessionId = 0;

bool CClientRoutine::ConsoleEventHandler(DWORD dwEvent) {

	SOCKET sockTemp = INVALID_SOCKET;

	switch (dwEvent) {
		case CTRL_C_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT:
		case CTRL_CLOSE_EVENT:
			if (!currentRoutine) {
				return false;
			}

			if (currentRoutine->m_hIOCP) {
				for (size_t i = 0; i < currentRoutine->m_vWorkerPayloads.size(); i++) {
					PostQueuedCompletionStatus(currentRoutine->m_hIOCP, 0, 0, nullptr);
				}
			}

			//выходим из цикла в CClientRoutine::Start
			sockTemp = currentRoutine->m_dListenSocket;
			currentRoutine->m_dListenSocket = INVALID_SOCKET;
			g_bEndServer = true;
			closesocket(sockTemp);
			sockTemp = INVALID_SOCKET;
			break;

		default:
			return false;
	}
	return true;
}

ClientRoutinePtr CClientRoutine::GetInstance(const int threadCount, const std::string listenPort) {
	if (!currentRoutine) {
		//Значение currentRoutine присвоится в конструкторе, я зимплементил такое поведение 
		//т.к. в конструкторе стартуют рабочие потоки в которых нужен уже инициализированный умный указатель currentRoutine
		new CClientRoutine(threadCount, listenPort);
	}
	return currentRoutine;
}

CClientRoutine::CClientRoutine(int threadCount, const std::string port) :
	m_dListenSocket(INVALID_SOCKET),
	m_dThreadCount(threadCount),
	m_dIncomingSequence(0),
	m_dOutgoingSequence(1),
	m_hIOCP(INVALID_HANDLE_VALUE),
	m_hMySQLIOCP(INVALID_HANDLE_VALUE)
{
	WSADATA wsaData;
		
	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CClientRoutine::ConsoleEventHandler), true)) {
				
		m_pLogger->Error("SetConsoleCtrlHandler() failed to install console handler: " + GetLastError());
		
		return;
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		m_pLogger->Error("WSAStartup() failed");
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CClientRoutine::ConsoleEventHandler), false);
		return;
	}

	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		m_pLogger->Error("CreateIoCompletionPort() failed to create I/O completion port: " + GetLastError());
		return;
	}

	m_dListenSocket = CreateSocket(port, true);
	if (!m_dListenSocket) {
		return;
	}

	currentRoutine.reset(this);

	std::shared_ptr<std::thread> workThread;
	for (size_t i = 0; i < m_dThreadCount; i++) {

		workThread = std::make_shared<std::thread>(&CClientRoutine::WorkerThread);

		m_vWorkerPayloads.push_back(std::move(workThread));
	}	

	m_pLogger = CLogger::GetInstance();
}

void CClientRoutine::Start() {

	SOCKET sdClient = INVALID_SOCKET;
	
	BufferPtr buffer = nullptr;

	g_bEndServer = false;

	while (!g_bEndServer) {

		//Извлекает первое соединение из очереди ожидающих соединений на сокете m_sdListen
		//и возвращает сокет очередного клиента.
		sdClient = WSAAccept(m_dListenSocket, nullptr, nullptr, nullptr, 0);
		if (sdClient == SOCKET_ERROR) {

			//Нажали Ctrl+C
			m_pLogger->Error("WSAAccept() failed: " + WSAGetLastError());
			break;
		}

		//связываем сокет клиента с портом завершения , аллоцируем структуру и добавляем ее в контейнер
		UpdateCompletionPort(buffer, sdClient);
		if (buffer == nullptr) {					
			break;
		}

		//подключился новый клиент - постим мессадж об этом в порт завершения вспомогательного класса обслуживающего соединения с БД
		//(см. https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase.html)
		PostToIOCP(buffer.get());

		buffer.reset();
	} 
	
	
	if (sdClient != INVALID_SOCKET) {
		closesocket(sdClient);
		sdClient = INVALID_SOCKET;
	}
}

CClientRoutine::~CClientRoutine() {
	g_bEndServer = true;

	//Пушим в очередь порта завершения данные нулевой длины - сигналим рабочим потокам, что нужно завершить работу.
	if (m_hIOCP) {
		for (size_t i = 0; i < m_vWorkerPayloads.size(); i++) {
			PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);
		}
	}

	//Аттачим рабочие птококи с уже завершенными в них рутинами чтобы синхронно и безопасно завершить их работу.
	for (auto it = m_vWorkerPayloads.begin(); it != m_vWorkerPayloads.end(); ++it) {

		if (it->get()->joinable() && WaitForSingleObject(it->get()->native_handle(), 500) == WAIT_OBJECT_0) {
			it->get()->join();
			it->reset();
		}
		else {
			auto threadId = it->get()->get_id();

			std::stringstream ss;

			ss << threadId;

			m_pLogger->Error("Thread[" + ss.str() + "] not stopped.");
		}
		
	}

	//не используем итераторы для динамически изменяемого контейнера, иначе м.б. ошибка когда итератор == nullptr
	for (size_t i = 0; i < m_mClients.size(); i++) {
		RemoveSession(m_mClients[i]);
	}
	m_mClients.clear();

	if (m_hIOCP) {
		CloseHandle(m_hIOCP);
		m_hIOCP = nullptr;
	}

	//завершаем цикл в котором обрабатываются новые клиентские соединения (Если цикл остановился на блокирующем вызове WSAAccept)
	if (m_dListenSocket != INVALID_SOCKET) {
		closesocket(m_dListenSocket);
		m_dListenSocket = INVALID_SOCKET;
	}

	WSACleanup();
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CClientRoutine::ConsoleEventHandler), false);

	currentRoutine.reset();
}

//Инициализируем сокет, который будет слушать порт к которому будут коннектиться клиенты.
SOCKET CClientRoutine::CreateSocket(const std::string port, const bool isListenSocket) {
	SOCKET socket;

	struct addrinfo hints = {0};
	struct addrinfo *addrlocal;

	hints.ai_flags = isListenSocket ? AI_PASSIVE : 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;

	//конвертим адресс. В этом методе аллоцируется память для указателя addrlocal. 
	if(getaddrinfo(nullptr, port.c_str(), &hints, &addrlocal) != 0  || !addrlocal) {
		m_pLogger->Error("getaddrinfo() failed with error " + WSAGetLastError());
        return NULL;
	}
	//создаем сокет
	socket = WSASocket(addrlocal->ai_family, addrlocal->ai_socktype, addrlocal->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
	if(socket == INVALID_SOCKET ) {
		m_pLogger->Error("WSASocket(g_sdListen) failed: " + WSAGetLastError());
		return NULL;
	}

	if (!isListenSocket) {
		const char chOpt = 1;
		if ((setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &chOpt, sizeof(char))) == SOCKET_ERROR) {
			m_pLogger->Error("setsockopt failed: " + WSAGetLastError());
				return NULL;

		}
		if ((connect(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen)) < 0)
		{
			m_pLogger->Error("MySQL:  error " + WSAGetLastError());
			return NULL;
		}
		
		freeaddrinfo(addrlocal);
		return socket;
	}

	//ассоциируем сокет с конвертируемым ранее адресом
	if(bind(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen) == SOCKET_ERROR ) {
		m_pLogger->Error("bind() failed: " + WSAGetLastError());
		return NULL;
	}

	//переводим сокет в режим прослушивания с максимальной длиной очереди  == количеству потоков
	if(listen(socket, m_dThreadCount) == SOCKET_ERROR ) {
		m_pLogger->Error("listen() failed: " + WSAGetLastError());
		return NULL;
	}

	//устанавливаем размер буфера 0 (SO_SNDBUF).
	//int nZero = 0;
	//if(setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&nZero, sizeof(nZero)) == SOCKET_ERROR ) {
	//	//m_pLogger->Error("Error: " + WSAGetLastError());
	//	return NULL;
	//}

	freeaddrinfo(addrlocal);

	return socket;
}

//Рабочий поток обрабатывает все оперции чтения и записи в сокете связанным с портом завершения.
int CClientRoutine::WorkerThread() {

	HANDLE hIOCP = currentRoutine->m_hIOCP;
	bool bSuccess = false;
	BufferPtr buffer = nullptr;
	LPWSAOVERLAPPED lpOverlapped = nullptr;

	DWORD dwIoSize = 0;
	
	while(true) {
		
        //присоединяем текущий поток к пулу потоков порта завершения, который был создан ранее ОС. Функция блокирует поток, если в очереди отсутствуют запросы.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&buffer), &lpOverlapped, INFINITE);
		if (g_bEndServer || !buffer.get()) {
			//выходим из цикла и заканчиваем синхронно работу - в деструкторе этот поток аттачится к главному.
			break;
		}

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//соединение с клиентом разорвано
			currentRoutine->RemoveSession(buffer);
			continue;
		}

		switch(buffer->IOOperation ) {
		case WriteToClient:
			//Клиент считал буфер из сокета - инициируем асинхронное чтение данных от клиента
			currentRoutine->OnClientDataSended(buffer);
			break;
		case ReadFromIOCP:
			//Получили пакет от клиента - шлем его ч/з рутину, обслуживающую соединения с БД 
			buffer->nTotalBytes = dwIoSize;
			currentRoutine->OnClientDataReceived(buffer);
			break;
		case SendToClient:
			//Ответ из БД получен ч/з рутину, обслуживающуюу БД, шлем его асинхронно клиенту.
			currentRoutine->OnMySQLDataReceived(buffer);
			break;
		case ErrorInDB:
			currentRoutine->RemoveSession(buffer);
			break;
		default:
			currentRoutine->m_pLogger->Error("Incorrect sequence.");
		}

		buffer.reset();
		lpOverlapped = nullptr;
	}

	return 0;
}

void CClientRoutine::OnClientDataSended(BufferPtr buffer) {

	buffer->IOOperation = ReadFromIOCP;

	if (!RecvBufferAsync(buffer)) {

		m_pLogger->Error("Receive failed: " + WSAGetLastError());
		RemoveSession(buffer);
	}
}

void CClientRoutine::OnClientDataReceived(BufferPtr buffer) {

	buffer->IOOperation = ReadFromClient;

	AddBufferInQueue(buffer);

	m_pLogger->Write(buffer);

	PostToIOCP(buffer.get());
}

void CClientRoutine::OnMySQLDataReceived(BufferPtr buffer) {

	buffer->IOOperation = WriteToClient;

	if (!SendBufferAsync(buffer)) {

		m_pLogger->Error("Send failed: " + WSAGetLastError());
		RemoveSession(buffer);
	}
}

bool CClientRoutine::PostToIOCP(CBuffer* buffer) {

	size_t size = sizeof(*buffer);
	return PostQueuedCompletionStatus(m_hMySQLIOCP, size, (DWORD)(buffer), &(buffer->overlapped));

}

bool CClientRoutine::RecvBufferAsync(BufferPtr buffer) {

	LPWSAOVERLAPPED pOverlapped = &buffer->overlapped;
	DWORD totalBytes = 0;
	DWORD dwFlags = 0;

	LPWSABUF buffRecv = &buffer->wsabuf;
	buffRecv->buf = buffer->buffer;
	buffRecv->len = MAX_BUFF_SIZE;

	if(WSARecv(buffer->m_dClientSocket, buffRecv, 1, &totalBytes, &dwFlags, pOverlapped, nullptr) == SOCKET_ERROR
		&& (ERROR_IO_PENDING != WSAGetLastError())) {

		m_pLogger->Error("WSArecvBuffer: error " + WSAGetLastError());
		return false;
	}
		
	return true;
}

bool CClientRoutine::SendBufferAsync(const BufferPtr buffer) {
	
	BufferPtr pBuffer = GetNextBuffer();

	while (pBuffer) {
		//нам нужно следить чтобы за раз только один тред мог вызывать WSASaend для сокета клиента.
		std::lock_guard<std::mutex> lock(buffer->m_mClientSync);

		LPWSAOVERLAPPED pOverlapped = &pBuffer->overlapped;

		DWORD dwFlags = 0;
		LPWSABUF buffSend = &pBuffer->wsabuf;
		buffSend->buf = pBuffer->buffer;
		buffSend->len = pBuffer->nTotalBytes;

		if (WSASend(pBuffer->m_dClientSocket, buffSend, 1, nullptr, dwFlags, pOverlapped, nullptr) == SOCKET_ERROR
			&& (ERROR_IO_PENDING != WSAGetLastError())) {
			
			m_pLogger->Error("WSASendBuffer: error " + WSAGetLastError());

			return false;
		}

		pBuffer = ProcessNextBuffer();
	}

	
	return true;
}

//Аллоцирует контекст для сокета для нового соединения и связывает сокет с портом завершения.
void CClientRoutine::UpdateCompletionPort(BufferPtr& buffer, const SOCKET sdClient) {

	buffer.reset(new CBuffer(sdClient));
	if (!buffer.get()) {
		return;
	}
		
	//свзыввем хендл сокета клиента с портом завершения, иными словами оповещаем порт завершения о том что хотим наблюдать за этим сокетом
	m_hIOCP = CreateIoCompletionPort(reinterpret_cast<HANDLE>(sdClient), m_hIOCP, reinterpret_cast<DWORD_PTR>(buffer.get()), 0);
	if(!m_hIOCP) {
		
		m_pLogger->Error("CreateIoCompletionPort() failed: " + GetLastError());

		buffer.reset();
		return;
	}

	buffer->m_dSessionId = m_dSessionId++;

	AddBufferInQueue(buffer);
	AddNewSession(buffer);
}

void CClientRoutine::AddNewSession(const BufferPtr buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);
	
	m_mClients.insert(std::pair<size_t, BufferPtr>(buffer->m_dSessionId, std::move(buffer)));

}

BufferPtr CClientRoutine::GetNextBuffer() {

	std::lock_guard<std::mutex> lock(m_mBufferSync);
	auto removedItemIt = std::find(m_vRemovedBufferNumbers.begin(), m_vRemovedBufferNumbers.end(), m_dOutgoingSequence);

	if (removedItemIt != m_vRemovedBufferNumbers.end()) {
		m_vRemovedBufferNumbers.erase(removedItemIt);
		InterlockedIncrement(&m_dOutgoingSequence);
	}

	BufferPtr nextPtr = nullptr;
	auto it = m_mBuffer.begin();

	if (it != m_mBuffer.end() && it->second != nullptr && it->second->sequenceNumber == m_dOutgoingSequence 
		&& it->second->IOOperation == WriteToClient) {
		
		nextPtr = it->second;
		m_mBuffer.erase(it);
	}

	return nextPtr;
}

BufferPtr CClientRoutine::ProcessNextBuffer() {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	InterlockedIncrement(&m_dOutgoingSequence);

	BufferPtr nextPtr = nullptr;

	auto it = m_mBuffer.begin();

	if (it != m_mBuffer.end() && it->second != nullptr && it->second->sequenceNumber == m_dOutgoingSequence 
		&& it->second->IOOperation == WriteToClient) {
		
		nextPtr = it->second;
		m_mBuffer.erase(it);
	}

	return nextPtr;
}

void CClientRoutine::RemoveBufferFromQueue(BufferPtr buffer) {

	auto it = m_mBuffer.find(buffer->sequenceNumber);

	if (it != m_mBuffer.end()) {

		m_vRemovedBufferNumbers.push_back(it->first);

		buffer.reset();

		m_mBuffer.erase(it);
		
	}
}

void CClientRoutine::AddBufferInQueue(const BufferPtr buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	InterlockedIncrement(&m_dIncomingSequence);

	buffer->sequenceNumber = m_dIncomingSequence;

	m_mBuffer.insert(std::pair<size_t, BufferPtr>(m_dIncomingSequence, buffer));

}

void CClientRoutine::RemoveSession(BufferPtr buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	auto it = m_mClients.find(buffer->m_dSessionId);
			
	if (it != m_mClients.end()) {

		RemoveBufferFromQueue(it->second);

		buffer.reset();

		m_mClients.erase(it);
	}
	
}
