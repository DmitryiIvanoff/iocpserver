#pragma warning (disable:4127)

#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
#include "CIOCPClientServer.h"

#pragma warning( disable : 4996)

extern bool g_bEndServer;// set to TRUE on CTRL-C
CIOCPClientServer* CIOCPClientServer::currentServer = nullptr;
size_t CIOCPClientServer::m_dSessionId = 0;

//
//  Intercept CTRL-C or CTRL-BRK events and cause the server to initiate shutdown.
//  CTRL-BRK resets the restart flag, and after cleanup the server restarts.
//
bool CIOCPClientServer::ConsoleEventHandler(DWORD dwEvent) {

	SOCKET sockTemp = INVALID_SOCKET;

	switch (dwEvent) {
	case CTRL_C_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
	case CTRL_CLOSE_EVENT:
		if (!currentServer) {
			return false;
		}

		if (currentServer->m_hIOCP) {
			for (size_t i = 0; i < currentServer->m_vWorkerPayloads.size(); i++) {
				PostQueuedCompletionStatus(currentServer->m_hIOCP, 0, 0, nullptr);
			}
		}

		//
		//We want to make closesocket the last call in the handler because it will
		//cause the WSAAccept to return in the main thread
		//
		sockTemp = currentServer->m_dListenSocket;
		currentServer->m_dListenSocket = INVALID_SOCKET;
		g_bEndServer = true;
		closesocket(sockTemp);
		sockTemp = INVALID_SOCKET;
		break;

	default:
		
		return false;
	}
	return true;
}

CIOCPClientServer::CIOCPClientServer(int threadCount, const std::string port) :
	m_dListenSocket(INVALID_SOCKET),
	m_dMySQLSocket(INVALID_SOCKET),
	m_dThreadCount(threadCount),
	m_dIncomingSequence(0),
	m_dOutgoingSequence(1),
	m_hIOCP(INVALID_HANDLE_VALUE),
	m_hMySQLIOCP(INVALID_HANDLE_VALUE)
{
	WSADATA wsaData;
		
	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPClientServer::ConsoleEventHandler), true)) {
		std::cout << "SetConsoleCtrlHandler() failed to install console handler:" << GetLastError() << std::endl;
		return;
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "WSAStartup() failed" << std::endl;
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPClientServer::ConsoleEventHandler), false);
		return;
	}

	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		std::cout << "CreateIoCompletionPort() failed to create I/O completion port: " << GetLastError() << std::endl;
		return;
	}

	m_dListenSocket = CreateSocket(port, true);
	if (!m_dListenSocket) {
		return;
	}

	std::shared_ptr<std::thread> workThread;
	for (size_t i = 0; i < m_dThreadCount; i++) {

		workThread = std::make_shared<std::thread>(&CIOCPClientServer::WorkerThread, this);

		m_vWorkerPayloads.push_back(std::move(workThread));
	}	

	m_pLogger = CLogger::GetLogger();

	currentServer = this;
}

void CIOCPClientServer::Start() {

	SOCKET sdClient = INVALID_SOCKET;
	
	ClientContextPtr lpPerSocketContext = nullptr;

	g_bEndServer = false;

	while (!g_bEndServer) {

		//Извлекает первое соединение из очереди ожидающих соединений на сокете m_sdListen
		//и возвращает сокет очередного клиента.
		sdClient = WSAAccept(m_dListenSocket, nullptr, nullptr, nullptr, 0);
		if (sdClient == SOCKET_ERROR) {

			//Нажали Ctrl+C
			std::cout << "WSAAccept() failed: " << WSAGetLastError() << std::endl;
			break;
		}

		//связываем сокет клиента с портом завершения , аллоцируем структуру и добавляем ее в контейнер
		UpdateCompletionPort(lpPerSocketContext, sdClient);
		if (lpPerSocketContext == nullptr) {					
			break;
		}

		lpPerSocketContext->m_dSessionId = m_dSessionId++;

		//подключился новый клиент - постим мессадж об этом в порт завершения вспомогательного класса обслуживающего соединения с БД
		//(см. https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase.html)
		PostToIOCP(lpPerSocketContext.get());

		lpPerSocketContext.reset();
	} 
	
	
	if (sdClient != INVALID_SOCKET) {
		closesocket(sdClient);
		sdClient = INVALID_SOCKET;
	}
}

CIOCPClientServer::~CIOCPClientServer() {
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
			std::cout << "Thread[" << it->get()->get_id() << "] not stopped." << std::endl;
		}
		
	}

	//не используем итераторы для динамически изменяемого контейнера, иначе м.б. ошибка когда итератор == nullptr
	for (size_t i = 0; i < m_vConnectedClients.size(); i++) {
		RemoveSession(m_vConnectedClients[i]);
	}
	m_vConnectedClients.clear();

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
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPClientServer::ConsoleEventHandler), false);
}

//Инициализируем сокет, который будет слушать порт к которому будут коннектиться клиенты.
SOCKET CIOCPClientServer::CreateSocket(const std::string port, const bool isListenSocket) {
	SOCKET socket;

	struct addrinfo hints = {0};
	struct addrinfo *addrlocal;

	hints.ai_flags = isListenSocket ? AI_PASSIVE : 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;

	//конвертим адресс. В этом методе аллоцируется память для указателя addrlocal. 
	if(getaddrinfo(nullptr, port.c_str(), &hints, &addrlocal) != 0  || !addrlocal) {
		std::cout << "getaddrinfo() failed with error " << WSAGetLastError() << std::endl;
        return NULL;
	}
	//создаем сокет
	socket = WSASocket(addrlocal->ai_family, addrlocal->ai_socktype, addrlocal->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
	if(socket == INVALID_SOCKET ) {
		std::cout << "WSASocket(g_sdListen) failed: " << WSAGetLastError() << std::endl;
		return NULL;
	}

	if (!isListenSocket) {
		const char chOpt = 1;
		if ((setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &chOpt, sizeof(char))) == SOCKET_ERROR) {
			std::cout << "setsockopt failed: " << WSAGetLastError() << std::endl;
				return NULL;

		}
		if ((connect(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen)) < 0)
		{
			std::cout << "MySQL:  error " << WSAGetLastError() << " in connect" << std::endl;
			return NULL;
		}
		
		freeaddrinfo(addrlocal);
		return socket;
	}

	//ассоциируем сокет с конвертируемым ранее адресом
	if(bind(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen) == SOCKET_ERROR ) {
		std::cout << "bind() failed: " << WSAGetLastError() << std::endl;
		return NULL;
	}

	//переводим сокет в режим прослушивания с максимальной длиной очереди  == количеству потоков
	if(listen(socket, m_dThreadCount) == SOCKET_ERROR ) {
		std::cout << "listen() failed: " << WSAGetLastError() << std::endl;
		return NULL;
	}

	//устанавливаем размер буфера 0 (SO_SNDBUF).
	//int nZero = 0;
	//if(setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&nZero, sizeof(nZero)) == SOCKET_ERROR ) {
	//	//std::cout << "setsockopt(SNDBUF) failed: " << WSAGetLastError() << std::endl;
	//	return NULL;
	//}

	freeaddrinfo(addrlocal);

	return socket;
}

//Рабочий поток обрабатывает все оперции чтения и записи в сокете связанным с портом завершения.
int CIOCPClientServer::WorkerThread(LPVOID WorkThreadContext) {

	CIOCPClientServer* server = static_cast<CIOCPClientServer*>(WorkThreadContext);
	HANDLE hIOCP = server->m_hIOCP;
	bool bSuccess = false;
	ClientContextPtr lpPerSocketContext = nullptr;
	LPWSAOVERLAPPED lpOverlapped = nullptr;
	
	IOContextPtr lpIOContext;
	DWORD dwIoSize = 0;
	
	while(true) {
		
        //присоединяем текущий поток к пулу потоков порта завершения, который был создан ранее ОС. Функция блокирует поток, если в очереди отсутствуют запросы.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&lpPerSocketContext), &lpOverlapped, INFINITE);
		
		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//соединение с клиентом разорвано
			server->RemoveSession(lpPerSocketContext);
			continue;
		}

		if (g_bEndServer || !lpPerSocketContext.get()) {
			//выходим из цикла и заканчиваем синхронно работу - в деструкторе этот поток аттачится к главному.
			break;
		}

		//std::lock_guard<std::mutex> socket(server->m_mThreadSync);
		//TODO: заменить на функции
		lpIOContext = lpPerSocketContext->m_pIOContext;

		switch( lpIOContext->IOOperation ) {
		case WriteToClient:
			//Клиент считал буфер из сокета - инициируем асинхронное чтение данных от клиента
			lpIOContext->IOOperation = AcceptClient;
			if (!server->RecvBufferAsync(lpIOContext)) {
				std::cout << "Receive failed: " << WSAGetLastError() << std::endl;
				server->RemoveSession(lpPerSocketContext);
			}

			break;
		case AcceptClient:
			//Опреация асинхронного чтения клиентских данных завершена - шлем их ч/з рутину, обслуживающую соединения с БД 
			lpIOContext->nTotalBytes = dwIoSize;
			lpIOContext->IOOperation = ReadFromClient;
			server->AddBufferInQueue(lpIOContext);
			server->m_pLogger->Write(lpIOContext);
			server->PostToIOCP(lpPerSocketContext.get());
			
			break;
		case ReadFromClient:
			std::cout << "Incorrect sequence" << std::endl;
			break;
		case SendToClient:
			//считали данные из БД ч/з рутину, обслуживающуюу БД, шлем данные клиенту.
			lpIOContext->IOOperation = WriteToClient;
			if (!server->SendBufferAsync(lpIOContext)) {
				std::cout << "Send failed: " << WSAGetLastError() << std::endl;
				server->RemoveSession(lpPerSocketContext);
			}

			break;
		}

		lpPerSocketContext = nullptr;
		lpOverlapped = nullptr;
	}

	return 0;
}

bool CIOCPClientServer::PostToIOCP(CClientContext* lpPerSocketContext) {

	size_t size = sizeof(*lpPerSocketContext);
	return PostQueuedCompletionStatus(m_hMySQLIOCP, size, (DWORD)(lpPerSocketContext), &(lpPerSocketContext->m_pIOContext->overlapped));

}

bool CIOCPClientServer::RecvBufferAsync(IOContextPtr buffer) {

	LPWSAOVERLAPPED pOverlapped = &buffer->overlapped;
	//ZeroMemory(pOverlapped, sizeof(WSAOVERLAPPED));

	DWORD totalBytes = 0;
	DWORD dwFlags = 0;
	LPWSABUF buffRecv = &buffer->wsabuf;
	buffRecv->buf = buffer->buffer;
	buffRecv->len = MAX_BUFF_SIZE;
	//std::fill(buffRecv->buf, buffRecv->buf + MAX_BUFF_SIZE, 0);

	if(WSARecv(buffer->m_dClientSocket, buffRecv, 1, &totalBytes, &dwFlags, pOverlapped, nullptr) == SOCKET_ERROR
		&& (ERROR_IO_PENDING != WSAGetLastError())) {
		std::cout << "WSArecvBuffer: error " << WSAGetLastError() << " in receive" << std::endl;
		return false;
	}
		
	return true;
}

bool CIOCPClientServer::SendBufferAsync(IOContextPtr buffer) {
	
	IOContextPtr pBuffer = GetNextBuffer();

	while (pBuffer) {
		//нам нужно следить чтобы за раз только один тред мог вызывать WSASaend для сессии.
		std::lock_guard<std::mutex> lock(buffer->m_mClientSync);

		LPWSAOVERLAPPED pOverlapped = &pBuffer->overlapped;

		DWORD dwFlags = 0;
		LPWSABUF buffSend = &pBuffer->wsabuf;
		buffSend->buf = pBuffer->buffer;
		buffSend->len = pBuffer->nTotalBytes;

		if (WSASend(pBuffer->m_dClientSocket, buffSend, 1, nullptr, dwFlags, pOverlapped, nullptr) == SOCKET_ERROR
			&& (ERROR_IO_PENDING != WSAGetLastError())) {
			std::cout << "WSASendBuffer: error " << WSAGetLastError() << " in send" << std::endl;
			return false;
		}

		pBuffer = ProcessNextBuffer();
	}

	
	return true;
}

//Аллоцирует контекст для сокета для нового соединения и связывает сокет с портом завершения.
void CIOCPClientServer::UpdateCompletionPort(ClientContextPtr& context, SOCKET sdClient) {

	context.reset(new CClientContext(sdClient));
	if (!context.get()) {
		return;
	}
		
	//свзыввем хендл сокета клиента с портом завершения, иными словами оповещаем порт завершения о том что хотим наблюдать за этим сокетом
	m_hIOCP = CreateIoCompletionPort(reinterpret_cast<HANDLE>(sdClient), m_hIOCP, reinterpret_cast<DWORD_PTR>(context.get()), 0);
	if(!m_hIOCP) {
		std::cout << "CreateIoCompletionPort() failed: " << GetLastError() << std::endl;
		context.reset();
		return;
	}

	context->m_dSessionId = m_dSessionId++;

	AddBufferInQueue(context->m_pIOContext);
	AddNewSession(context);
}

void CIOCPClientServer::AddNewSession(ClientContextPtr lpPerSocketContext) {

	std::lock_guard<std::mutex> lock(m_mContextsSync);
	
	m_vConnectedClients.push_back(std::move(lpPerSocketContext));

}

IOContextPtr CIOCPClientServer::GetNextBuffer() {
	std::lock_guard<std::mutex> lock(m_mContextsSync);
	auto removedItemIt = std::find(m_vRemovedBufferNumbers.begin(), m_vRemovedBufferNumbers.end(), m_dOutgoingSequence);

	if (removedItemIt != m_vRemovedBufferNumbers.end()) {
		m_vRemovedBufferNumbers.erase(removedItemIt);
		InterlockedIncrement(&m_dOutgoingSequence);
	}

	IOContextPtr nextPtr = nullptr;
	auto it = m_mBuffer.begin();

	if (it != m_mBuffer.end() && it->second != nullptr && it->second->sequenceNumber == m_dOutgoingSequence 
		&& it->second->IOOperation == WriteToClient) {
		
		nextPtr = it->second;
		m_mBuffer.erase(it);
	}

	return nextPtr;
}

IOContextPtr CIOCPClientServer::ProcessNextBuffer() {
	std::lock_guard<std::mutex> lock(m_mContextsSync);
	InterlockedIncrement(&m_dOutgoingSequence);
	IOContextPtr nextPtr = nullptr;
	auto it = m_mBuffer.begin();

	if (it != m_mBuffer.end() && it->second != nullptr && it->second->sequenceNumber == m_dOutgoingSequence 
		&& it->second->IOOperation == WriteToClient) {
		
		nextPtr = it->second;
		m_mBuffer.erase(it);
	}

	return nextPtr;
}

void CIOCPClientServer::RemoveBufferFromQueue(IOContextPtr buffer) {

	auto it = m_mBuffer.find(buffer->sequenceNumber);

	if (it != m_mBuffer.end()) {

		m_vRemovedBufferNumbers.push_back(it->first);

		/*it->second.reset(); - объект уничтожится в деструкторе*/ 

		m_mBuffer.erase(it);
		
	}
}

void CIOCPClientServer::AddBufferInQueue(IOContextPtr& buffer) {

	std::lock_guard<std::mutex> lock(m_mContextsSync);

	InterlockedIncrement(&m_dIncomingSequence);

	buffer->sequenceNumber = m_dIncomingSequence;

	m_mBuffer.insert(std::pair<size_t, IOContextPtr>(m_dIncomingSequence, buffer));

}

void CIOCPClientServer::RemoveSession(ClientContextPtr lpPerSocketContext) {

	std::lock_guard<std::mutex> lock(m_mContextsSync);

	auto it = std::find(m_vConnectedClients.begin(), m_vConnectedClients.end(), lpPerSocketContext);
			
	if (it != m_vConnectedClients.end()) {

		RemoveBufferFromQueue(it->get()->m_pIOContext);
		it->reset();
		m_vConnectedClients.erase(it);
	}
	
}
