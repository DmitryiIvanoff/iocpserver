#pragma warning (disable:4127)

#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
#include "CIOCPClientServer.h"

#pragma warning( disable : 4996)

extern bool g_bEndServer;// set to TRUE on CTRL-C
CIOCPClientServer* CIOCPClientServer::currentServer = nullptr;
int CIOCPClientServer::clientID = 0;

//
//  Intercept CTRL-C or CTRL-BRK events and cause the server to initiate shutdown.
//  CTRL-BRK resets the restart flag, and after cleanup the server restarts.
//
bool CIOCPClientServer::сtrlHandler(DWORD dwEvent) {

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
			for (size_t i = 0; i < currentServer->m_vWorkerThread.size(); i++) {
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

CIOCPClientServer::CIOCPClientServer(int threadCount, std::string port, std::string mySQLPort):
	m_hIOCP(INVALID_HANDLE_VALUE),
	m_dThreadCount(threadCount),
	m_dListenSocket(INVALID_SOCKET),
	m_dIncomingSequence(0),
	m_dOutgoingSequence(1)
{
	WSADATA wsaData;
		
	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPClientServer::сtrlHandler), true)) {
		std::cout << "SetConsoleCtrlHandler() failed to install console handler:" << GetLastError() << std::endl;
		return;
	}
	//инициализируем использование winsocks dll процессом.
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "WSAStartup() failed" << std::endl;
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPClientServer::сtrlHandler), false);
		return;
	}

	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		std::cout << "CreateIoCompletionPort() failed to create I/O completion port: " << GetLastError() << std::endl;
		return;
	}

	m_dListenSocket = createSocket(port, true);
	if (!m_dListenSocket) {
		return;
	}

	/*m_dMySQLSocket = createSocket(mySQLPort, false);
	if (!m_dMySQLSocket)
	{
		return;
	}*/
	//updateCompletionPort(mysqlPointer, m_dMySQLSocket, AcceptClient);
	
	for (int i = 0; i < m_dThreadCount; i++) {
		m_vWorkerThread.push_back(std::move(std::make_shared<std::thread>(&CIOCPClientServer::workerThread, this)));
	}	

	currentServer = this;
}

void CIOCPClientServer::start() {

	SOCKET sdClient = INVALID_SOCKET;
	SOCKET adMySQL = INVALID_SOCKET;
	ClientContextPtr lpPerSocketContext = nullptr;
	DWORD dwRecvNumBytes = 0;
	DWORD dwFlags = 0;

	g_bEndServer = false;

	while (!g_bEndServer) {

		//Извлекает первое соединение из очереди ожидающих соединений на сокете m_sdListen
		//и возвращает сокет очередного клиента.
		sdClient = WSAAccept(m_dListenSocket, nullptr, nullptr, nullptr, 0);
		if (sdClient == SOCKET_ERROR) {

			//
			// If user hits Ctrl+C or Ctrl+Brk or console window is closed, the control
			// handler will close the g_sdListen socket. The above WSAAccept call will 
			// fail and we thus break out the loop,
			//
			std::cout << "WSAAccept() failed: " << WSAGetLastError() << std::endl;
			break;
		}

		adMySQL = createSocket("3306", false);
		if (!adMySQL)
		{
			return;
		}
			
		//связываем сокет клиента с портом завершения , аллоцируем структуру и добавляем ее в контейнер
		updateCompletionPort(lpPerSocketContext, sdClient, adMySQL, SendToClient);
		if (lpPerSocketContext == nullptr) {					
			throw std::exception("Error: ...");
		}

		

		recvBuffer(adMySQL, lpPerSocketContext->m_pIOContext, lpPerSocketContext->clientId);

		postContextToIOCP(lpPerSocketContext.get());

		lpPerSocketContext.reset();
	} 
	
	
	if (sdClient != INVALID_SOCKET) {
		closesocket(sdClient);
		sdClient = INVALID_SOCKET;
	}
}

CIOCPClientServer::~CIOCPClientServer() {
	g_bEndServer = true;

	//Отправляем на порт завершения данные нулевой длины чтобы функция GetQueuedCompletionStatus 
	//получила эти данные и workerThread продолжит выполняться до блока где проверяется длина полученных данных и выйдет из цикла- завершит работу.
	if (m_hIOCP) {
		for (size_t i = 0; i < m_vWorkerThread.size(); i++) {
			PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);
		}
	}

	//Проверяем выполняются ли еще потоки, если нет то аттачим их к главному треду чтобы синхронно и безопасно удалить их.
	for (auto it = m_vWorkerThread.begin(); it != m_vWorkerThread.end(); ++it) {

		if (it->get()->joinable() && WaitForSingleObject(it->get()->native_handle(), 500) == WAIT_OBJECT_0) {
			it->get()->join();
			it->reset();
		}
		else {
			std::cout << "Thread[" << it->get()->get_id() << "] not stopped." << std::endl;
		}
		
	}
	//не используем итераторы для динамически изменяемого контейнера, иначе м.б. ошибка когда итератор == nullptr
	for (size_t i = 0; i < m_vContexts.size(); i++) {
		removeIOContext(m_vContexts[i]);
	}
	m_vContexts.clear();

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
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPClientServer::сtrlHandler), false);
}

//Инициализируем сокет, который будет слушать порт к которому будут коннектиться клиенты.
SOCKET CIOCPClientServer::createSocket(std::string port, bool isListenSocket) {
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

	//устанавливаем размер буфера отправки (SO_SNDBUF) в 0 - данные для отправки ине будут беферризироваться. Параметр будет браться из опций самого сокета (SOL_SOCKET)
	//int nZero = 0;
	//if(setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&nZero, sizeof(nZero)) == SOCKET_ERROR ) {
	//	//std::cout << "setsockopt(SNDBUF) failed: " << WSAGetLastError() << std::endl;
	//	return NULL;
	//}
	//деаллоцируем структуру addrlocal.
	freeaddrinfo(addrlocal);

	return socket;
}

//Рабочий поток обрабатывает все оперции чтения и записи в сокете связанным с портом завершения.
int CIOCPClientServer::workerThread(LPVOID WorkThreadContext) {

	CIOCPClientServer* server = static_cast<CIOCPClientServer*>(WorkThreadContext);
	HANDLE hIOCP = server->m_hIOCP;
	bool bSuccess = false;
	ClientContextPtr lpPerSocketContext = nullptr;
	LPWSAOVERLAPPED lpOverlapped = nullptr;
	//ClientContextPtr lpPerSocketContext;
	IOContextPtr lpIOContext;
	DWORD dwIoSize = 0;
	
	while(true) {
		//std::cout << "Client[" << std::this_thread::get_id() << "] GetQueuedCompletionStatus["<< std::this_thread::get_id() <<"] start" << std::endl;
		
		
        //присоединяем текущий поток к пулу потоков порта завершения, который был создан ранее ОС. Функция блокирует поток, если в очереди отсутствуют запросы.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&lpPerSocketContext), &lpOverlapped, INFINITE);
		//std::cout << "Client[" << std::this_thread::get_id() << "] GetQueuedCompletionStatus[" << std::this_thread::get_id() << "] end" << std::endl;
		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {

			//соединение с клиентом разорвано
			server->removeIOContext(lpPerSocketContext);
			continue;
		}

		if (g_bEndServer) {
			//выходим из цикла и заканчиваем синхронно работу - в деструкторе этот поток аттачится к главному.
			break;
		}

		if(!lpPerSocketContext.get()) {

			//
			// CTRL-C handler used PostQueuedCompletionStatus to post an I/O packet with
			// a nullptr CompletionKey (or if we get one for any reason).  It is time to exit.
			//
			break;
		}

        //
		// determine what type of IO packet has completed by checking the CIOContext 
		// associated with this socket.  This will determine what action to take.
		//
		//std::lock_guard<std::mutex> socket(server->m_mThreadGuard);
		//TODO: заменить на функции
		lpIOContext = lpPerSocketContext->m_pIOContext;

		switch( lpIOContext->IOOperation ) {
		case WriteToClient:
			//TODO не вызывать если нужно досылать данные
			lpIOContext->IOOperation = AcceptClient;
			if (!server->WSArecvBuffer(lpIOContext->m_dClientSocket, lpIOContext, lpPerSocketContext->clientId)) {
				std::cout << "Receive failed: " << WSAGetLastError() << std::endl;
				server->removeIOContext(lpPerSocketContext);
			}

			break;
		case AcceptClient:
			lpIOContext->nTotalBytes = dwIoSize;
			lpIOContext->IOOperation = ReadFromClient;
			if (!server->sendBuffer(lpIOContext->m_dMySQLSocket, lpIOContext, lpPerSocketContext->clientId)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Send failed: " << WSAGetLastError() << std::endl;;
				server->removeIOContext(lpPerSocketContext);
			}
			
			//ответ клиетну
		case PartialBuffer:
		case ReadFromClient:
			lpIOContext->IOOperation = SendToClient;
			//читаем ответ
			if (!server->recvBuffer(lpIOContext->m_dMySQLSocket, lpIOContext, lpPerSocketContext->clientId)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Receive failed: " << WSAGetLastError() << std::endl;
				server->removeIOContext(lpPerSocketContext);
			}

			server->postContextToIOCP(lpPerSocketContext.get());
			break;
		case SendToClient:
			//ответ клиетну
			if (lpIOContext->IOOperation != PartialBuffer) {
				lpIOContext->IOOperation = WriteToClient;
			}
			if (!server->WSAsendBuffer(lpIOContext->m_dClientSocket, lpIOContext, lpPerSocketContext->clientId)) {
				std::cout << "Send failed: " << WSAGetLastError() << std::endl;
				server->removeIOContext(lpPerSocketContext);
			}

			break;
		}

		lpPerSocketContext = nullptr;
		lpOverlapped = nullptr;
	}
	//std::cout << "End" << std::endl;
	return 0;
}



bool CIOCPClientServer::recvBuffer(SOCKET recvSocket, IOContextPtr data, size_t id) {

	DWORD dwFlags = 0;
	DWORD dRecvTotalBytes = 0;
	LPWSABUF buffRecv = &data->wsabuf;

	buffRecv->buf = data->buffer;
	buffRecv->len = MAX_BUFF_SIZE;

	while ((data->nTotalBytes = recv(recvSocket, buffRecv->buf, buffRecv->len, dwFlags)) == MAX_BUFF_SIZE) {
		//напрямую пишем в сокет клиента большие данные - TODO: сделать через порт завершения.
		/*for (int i = 0; i < MAX_BUFF_SIZE; i++) {
			std::cout << data->buffer[i];
		};*/
		//buffRecv->buf = data->buffer;
		std::cout << "\n=================================" << std::endl;
		sendBuffer(data->m_dClientSocket, data, id);
		//recvBuffer(data->m_dClientSocket, data, id);
	}

	return true;// ((data->nTotalBytes = recv(recvSocket, buffRecv->buf, buffRecv->len, dwFlags)) > 0);
}

bool CIOCPClientServer::sendBuffer(SOCKET sendSocket, IOContextPtr data, size_t id) {

	LPWSABUF buffSend = &data->wsabuf;
	buffSend->buf = data->buffer;
	buffSend->len = data->nTotalBytes;
	DWORD dwFlags = 0;
	send(sendSocket, buffSend->buf, buffSend->len, dwFlags);

	return true;// send(sendSocket, buffSend->buf, buffSend->len, dwFlags) != SOCKET_ERROR);

}

bool CIOCPClientServer::postContextToIOCP(CClientContext* lpPerSocketContext) {

	size_t size = sizeof(*lpPerSocketContext);
	return PostQueuedCompletionStatus(m_hIOCP, size, (DWORD)(lpPerSocketContext), &(lpPerSocketContext->m_pIOContext->overlapped));

}

bool CIOCPClientServer::WSArecvBuffer(SOCKET recvSocket, IOContextPtr buffer, size_t id) {

	addBufferInMap(buffer);
	
	//std::cout << "Client[" << id << "] recvBuffer socket[" << recvSocket << "]" << std::endl;

	LPWSAOVERLAPPED pOverlapped = &buffer->overlapped;
	//ZeroMemory(pOverlapped, sizeof(WSAOVERLAPPED));
	DWORD totalBytes = 0;
	DWORD dwFlags = 0;
	LPWSABUF buffRecv = &buffer->wsabuf;
	//ставим указатель на началло буфера
	buffRecv->buf = buffer->buffer;
	buffRecv->len = MAX_BUFF_SIZE;
	//std::fill(buffRecv->buf, buffRecv->buf + MAX_BUFF_SIZE, 0);

	if(WSARecv(recvSocket, buffRecv, 1, &totalBytes, &dwFlags, pOverlapped, nullptr) == SOCKET_ERROR
		&& (ERROR_IO_PENDING != WSAGetLastError())) {
		std::cout << "WSArecvBuffer: error " << WSAGetLastError() << " in receive" << std::endl;
		return false;
	}
		
	return true;
}

bool CIOCPClientServer::WSAsendBuffer(SOCKET sendSocket, IOContextPtr buffer, size_t id) {
	
	IOContextPtr pBuffer = getNextBuffer(buffer);

	while (pBuffer) {
		//нам нужно следить чтобы за раз только один тред мог вызывать WSASaend в сокет клиента сокет.
		std::lock_guard<std::mutex> lock(buffer->m_mClientGuard);

		//указатель на overlapped
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

		pBuffer = processNextBuffer(pBuffer);
	}

	
	return true;
}

//Аллоцирует контекст для сокета для нового соединения и связывает сокет с портом завершения.
void CIOCPClientServer::updateCompletionPort(ClientContextPtr& context, SOCKET sdClient, SOCKET sdMySQL, etIOOperation operation) {

	context.reset(new CClientContext(sdClient, sdMySQL, operation));
	if (!context.get()) {
		return;
	}
		
	//свзыввем хендл сокета клиента с портом завершения, иными словами оповещаем порт завершения о том что хотим наблюдать за этим сокетом
	m_hIOCP = CreateIoCompletionPort(reinterpret_cast<HANDLE>(sdClient), m_hIOCP, reinterpret_cast<DWORD_PTR>(context.get()), 0);
	if(!m_hIOCP) {
		//std::cout << "CreateIoCompletionPort() failed: " << GetLastError() << std::endl;
		context.reset();
		return;
	}

	context->clientId = clientID++;

	addBufferInMap(context->m_pIOContext);
	addIOContext(context);
}

void CIOCPClientServer::addIOContext(ClientContextPtr lpPerSocketContext) {

	std::lock_guard<std::mutex> lock(m_mContextsGuard);
	
	m_vContexts.push_back(std::move(lpPerSocketContext));

}

IOContextPtr CIOCPClientServer::getNextBuffer(IOContextPtr buffer) {
	std::lock_guard<std::mutex> lock(m_mContextsGuard);
	auto removedItemIt = std::find(m_vRemovedBufferNumbers.begin(), m_vRemovedBufferNumbers.end(), m_dOutgoingSequence);

	if (removedItemIt != m_vRemovedBufferNumbers.end()) {
		m_vRemovedBufferNumbers.erase(removedItemIt);
		InterlockedIncrement(&m_dOutgoingSequence);
	}

	IOContextPtr nextPtr = nullptr;
	auto it = m_mBuffer.begin();

	if (it != m_mBuffer.end() && it->second != nullptr && it->second->sequenceNumber == m_dOutgoingSequence 
		&& (it->second->IOOperation == WriteToClient || it->second->IOOperation == PartialBuffer)) {
		
		nextPtr = it->second;
		m_mBuffer.erase(it);
	}

	return nextPtr;
}

IOContextPtr CIOCPClientServer::processNextBuffer(IOContextPtr buffer) {
	std::lock_guard<std::mutex> lock(m_mContextsGuard);
	InterlockedIncrement(&m_dOutgoingSequence);
	IOContextPtr nextPtr = nullptr;
	auto it = m_mBuffer.begin();

	if (it != m_mBuffer.end() && it->second != nullptr && it->second->sequenceNumber == m_dOutgoingSequence 
		&& (it->second->IOOperation == WriteToClient || it->second->IOOperation == PartialBuffer)) {
		
		nextPtr = it->second;
		m_mBuffer.erase(it);
	}

	return nextPtr;
}

void CIOCPClientServer::removeBufferFromMap(IOContextPtr buffer) {

	auto it = m_mBuffer.find(buffer->sequenceNumber);

	if (it != m_mBuffer.end()) {

		m_vRemovedBufferNumbers.push_back(it->first);

		/*it->second.reset(); - объект уничтожится в деструкторе*/ 

		m_mBuffer.erase(it);
		
	}
}

void CIOCPClientServer::addBufferInMap(IOContextPtr& buffer) {

	std::lock_guard<std::mutex> lock(m_mContextsGuard);

	InterlockedIncrement(&m_dIncomingSequence);

	buffer->sequenceNumber = m_dIncomingSequence;

	m_mBuffer.insert(std::pair<size_t, IOContextPtr>(m_dIncomingSequence, buffer));

}

void CIOCPClientServer::removeIOContext(ClientContextPtr lpPerSocketContext) {

	std::lock_guard<std::mutex> lock(m_mContextsGuard);

	auto it = std::find(m_vContexts.begin(), m_vContexts.end(), lpPerSocketContext);
			
	if (it != m_vContexts.end()) {

		removeBufferFromMap(it->get()->m_pIOContext);
		it->reset();
		m_vContexts.erase(it);
	}
	
}
