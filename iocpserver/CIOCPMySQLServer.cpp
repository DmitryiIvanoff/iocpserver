#pragma warning (disable:4127)

#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
#include "CIOCPMySQLServer.h"


extern bool g_bEndServer;// set to TRUE on CTRL-C
CIOCPMySQLServer* CIOCPMySQLServer::currentServer = nullptr;

static std::mutex printMutex;

//
//  Intercept CTRL-C or CTRL-BRK events and cause the server to initiate shutdown.
//  CTRL-BRK resets the restart flag, and after cleanup the server restarts.
//
bool CIOCPMySQLServer::сtrlHandler(DWORD dwEvent) {

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

		break;

	default:
		// unknown type--better pass it on.
		return false;
	}
	return true;
}

CIOCPMySQLServer::CIOCPMySQLServer(int threadCount, std::string mySQLPosrt) :
	m_hIOCP(INVALID_HANDLE_VALUE),
	m_dThreadCount(threadCount),
	//m_dListenSocket(INVALID_SOCKET),
	m_dMySQLSocket(INVALID_SOCKET),
	clientIOCP(INVALID_HANDLE_VALUE)
{
	WSADATA wsaData;

	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::сtrlHandler), true)) {
		std::cout << "MySQL: SetConsoleCtrlHandler() failed to install console handler:" << GetLastError() << std::endl;
		return;
	}
	//инициализируем использование Winsock DLL процессом.
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "MySQL:WSAStartup() failed" << std::endl;
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::сtrlHandler), false);
		return;
	}

	//создаем порт завершения. Тут INVALID_HANDLE_VALUE сообщает системе что нужно создать новый порт завершения.
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		std::cout << "MySQL:CreateIoCompletionPort() failed to create I/O completion port: " << GetLastError() << std::endl;
		return;
	}

	for (int i = 0; i < m_dThreadCount; i++) {
		m_vWorkerThread.push_back(std::move(std::make_shared<std::thread>(&CIOCPMySQLServer::workerThread, this)));
	}

	//The socket function creates a socket that is bound to a specific transport service provider.
	//@param1 AF_INET - The Internet Protocol version 4 (IPv4) address family. 
	//@param2 SOCK_STREAM use TCP protocol
	//@param3 If a value of 0 is specified, the caller does not wish to specify a protocol and the service provider will choose the protocol to use.
	m_dMySQLSocket = createSocket(mySQLPosrt, false);// socket(AF_INET, SOCK_STREAM, 0);
	if (!m_dMySQLSocket)
	{
		return;
	}
	
	currentServer = this;
}

CIOCPMySQLServer::~CIOCPMySQLServer() {
	g_bEndServer = true;

	//Отправляем на порт завершения данные нулевой длины чтобы блокирующая функция GetQueuedCompletionStatus 
	//получила эти данные и workerThread продолжит выполняться до блока где проверяется длина полученных данных и закончит свое выполнение.
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
			std::cout << "MySQL: Thread[" << it->get()->get_id() << "] not stopped." << std::endl;
		}

	}
	////не используем итераторы для динамически изменяемого контейнера, иначе м.б. ошибка когда итератор == nullptr
	//for (size_t i = 0; i < m_vContexts.size(); i++) {
	//	removeIOContext(m_vContexts[i]);
	//}
	//m_vContexts.clear();

	if (m_hIOCP) {
		CloseHandle(m_hIOCP);
		m_hIOCP = nullptr;
	}

	////завершаем цикл в котором обрабатываются новые клиентские соединения (Если цикл остановился на блокирующем вызове WSAAccept)
	//if (m_dListenSocket != INVALID_SOCKET) {
	//	closesocket(m_dListenSocket);
	//	m_dListenSocket = INVALID_SOCKET;
	//}

	WSACleanup();
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::сtrlHandler), false);
}

//Инициализируем сокет, который будет слушать порт к которому будут коннектиться клиенты.
SOCKET CIOCPMySQLServer::createSocket(std::string port, bool isListenSocket) {

	SOCKET socket;

	struct addrinfo hints = { 0 };
	struct addrinfo *addrlocal;

	//AI_PASSIVE - назначить сокету адрес моего хоста (сетевой адрес структуры не будет указан). Используется для серверных сокетов
	hints.ai_flags = isListenSocket ? AI_PASSIVE : AI_NUMERICHOST;//AI_NUMERICHOST - для того чтобы ваершарк смог трейсить пакеты между прокси и мускулом
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;

	//конвертим адресс. В этом методе аллоцируется память для указателя addrlocal. 
	if (getaddrinfo(isListenSocket ? nullptr : "127.0.0.1", port.c_str(), &hints, &addrlocal) != 0 || !addrlocal) {
		std::cout << "MySQL: getaddrinfo() failed with error " << WSAGetLastError() << std::endl;
		return NULL;
	}
	//создаем сокет
	socket = WSASocket(addrlocal->ai_family, addrlocal->ai_socktype, addrlocal->ai_protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (socket == INVALID_SOCKET) {
		std::cout << "MySQL: WSASocket(g_sdListen) failed: " << WSAGetLastError() << std::endl;
		return NULL;
	}


	if (connect(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen) < 0)
	{
		std::cout << "MySQL:  error " << WSAGetLastError() << " in connect" << std::endl;
		return NULL;
	}
	//деаллоцируем структуру addrlocal.
	freeaddrinfo(addrlocal);
	return socket;
}

//
// Worker thread that handles all I/O requests on any socket handle added to the IOCP.
//
int CIOCPMySQLServer::workerThread(LPVOID WorkThreadContext) {

	CIOCPMySQLServer* server = static_cast<CIOCPMySQLServer*>(WorkThreadContext);
	HANDLE hIOCP = server->m_hIOCP;
	bool bSuccess = false;

	LPWSAOVERLAPPED lpOverlapped = nullptr;
	ClientContextPtr lpPerSocketContext;
	IOContextPtr lpIOContext;
	DWORD dwIoSize = 0;

	while (true) {

		if (g_bEndServer) {
			//выходим из цикла и заканчиваем синхронно работу - в деструкторе этот поток аттачится к главному.
			break;
		}

		//присоединяем текущий поток к пулу потоков порта завершения, который был создан ранее ОС. Функция блокирует поток, если в очереди отсутствуют запросы.
		//Вызов этого метода блокирует поток, т.е. основное время поток проводит тут. Если в этот момент клиент отключится, то 
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&lpPerSocketContext), &lpOverlapped, INFINITE);

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//соединение с клиентом разорвано
			//server->removeIOContext(lpPerSocketContext);
			continue;
		}

		if (!lpPerSocketContext.get() || !lpPerSocketContext->m_pIOContext.get()) {

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
		lpIOContext = lpPerSocketContext->m_pIOContext;
		if (!lpIOContext) {
			//клиент дропнул соединение:
			continue;
		}
		//lpIOContext->nTotalBytes = dwIoSize;
		
		//if (lpIOContext->IOOperation == AcceptClient || lpIOContext->IOOperation == ReadFromClient) {
		std::lock_guard<std::mutex> lock(server->m_mContextsGuard);

		std::cout << "IOCP rec: " << lpIOContext->buffer << " ,bytes: " << dwIoSize << std::endl;
			lpIOContext->IOOperation = ReadFromClient;
			if (!server->sendBuffer(server->m_dMySQLSocket, lpIOContext, lpPerSocketContext->clientId)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Send failed: " << WSAGetLastError() << std::endl;;
				//server->removeIOContext(lpPerSocketContext);
				continue;
			}

			
			//читаем ответ
			if (!server->recvBuffer(server->m_dMySQLSocket, lpIOContext, lpPerSocketContext->clientId)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Receive failed: " << WSAGetLastError() << std::endl;
				//server->removeIOContext(lpPerSocketContext);
				continue;
			}
			lpIOContext->IOOperation = SendToClient;
			server->postContextToIOCP(lpPerSocketContext.get());			
		//}
	}
	std::cout << "Thread ended" << std::endl;
	return 0;
}

bool CIOCPMySQLServer::postContextToIOCP(CClientContext* lpPerSocketContext) {
	
	size_t size = sizeof(*lpPerSocketContext);
	return PostQueuedCompletionStatus(clientIOCP, size, (DWORD)(lpPerSocketContext), &(lpPerSocketContext->m_pIOContext->overlapped));

}

bool CIOCPMySQLServer::recvBuffer(SOCKET recvSocket, IOContextPtr data, size_t id) {
	//std::cout << "mysql[" << id << "] recvBuffer socket[" << recvSocket << "]" << std::endl;
	DWORD dwFlags = 0;
	DWORD dRecvTotalBytes = 0;
	LPWSABUF buffRecv = &data->wsabuf;
	//указатель на начало буфера
	buffRecv->buf = data->buffer;
	buffRecv->len = MAX_BUFF_SIZE;
	std::fill(buffRecv->buf, buffRecv->buf + MAX_BUFF_SIZE, 0);


	data->nTotalBytes = recv(recvSocket, buffRecv->buf, buffRecv->len, dwFlags);

	std::cout << "Recv: " << buffRecv->buf << std::endl;
	
	return true;
}

bool CIOCPMySQLServer::sendBuffer(SOCKET sendSocket, IOContextPtr data, size_t id) {

	
	LPWSABUF buffSend = &data->wsabuf;
	buffSend->buf = data->buffer;
	buffSend->len = data->nTotalBytes;
	DWORD dwFlags = 0;
	int dSendedBytes = send(sendSocket, buffSend->buf, buffSend->len, dwFlags);

	std::cout << "Send: " << buffSend->buf << " ,bytes: " << dSendedBytes << std::endl;

	return true;
}

bool CIOCPMySQLServer::WSArecvBuffer(SOCKET recvSocket, IOContextPtr data, size_t id) {
	std::cout << "mysql["<<id<<"] WSArecvBuffer socket[" << recvSocket << "]" << std::endl;
	//указатель на overlapped
	LPWSAOVERLAPPED pOverlapped = &data->overlapped;
	ZeroMemory(pOverlapped, sizeof(WSAOVERLAPPED));

	DWORD dwFlags = 0;
	LPWSABUF buffRecv = &data->wsabuf;
	//ставим указатель на началло буфера
	buffRecv->buf = data->buffer;
	buffRecv->len = MAX_BUFF_SIZE;
	std::fill(buffRecv->buf, buffRecv->buf + MAX_BUFF_SIZE, 0);

	if (WSARecv(recvSocket, buffRecv, 1, nullptr, &dwFlags, pOverlapped, nullptr) == SOCKET_ERROR
		&& (ERROR_IO_PENDING != WSAGetLastError())) {
		std::cout << "MySQL: WSArecvBuffer: error " << WSAGetLastError() << " in receive" << std::endl;
		return false;
	}

	//количество полученных байт хранится в структуре overlapped
	data->nTotalBytes = pOverlapped->InternalHigh;
	return true;
}

bool CIOCPMySQLServer::WSASendBuffer(SOCKET sendSocket, IOContextPtr data, size_t id) {
	std::cout << "mysql["<<id<<"] WSASendBuffer socket[" << sendSocket << "]" << std::endl;
	//указатель на overlapped
	LPWSAOVERLAPPED pOverlapped = &data->overlapped;

	DWORD dwFlags = 0;
	LPWSABUF buffSend = &data->wsabuf;
	buffSend->buf = data->buffer;
	buffSend->len = data->nTotalBytes;

	if (WSASend(sendSocket, buffSend, 1, nullptr, dwFlags, pOverlapped, nullptr) == SOCKET_ERROR
		&& (ERROR_IO_PENDING != WSAGetLastError())) {
		std::cout << "MySQL: WSASendBuffer: error " << WSAGetLastError() << " in send" << std::endl;
		return false;
	}
	return true;
}

//Аллоцирует контекст для сокета и связывает сокет с портом завершения.
void CIOCPMySQLServer::updateCompletionPort(ClientContextPtr& context, SOCKET sdClient, SOCKET sdMySQL, etIOOperation operation) {

	//
	// Allocate a socket context for the new connection.  
	//
	context.reset(new CClientContext(sdClient, sdMySQL, operation));
	if (!context) {
		return;
	}

	//свзыввем хендл сокета клиента с портом завершения, иными словами оповещаем порт завершения о том что хотим наблюдать за этим сокетом
	m_hIOCP = CreateIoCompletionPort(reinterpret_cast<HANDLE>(sdClient), m_hIOCP, reinterpret_cast<DWORD_PTR>(context.get()), 0);
	if (!m_hIOCP) {
		std::cout << "MySQL: CreateIoCompletionPort() failed: " << GetLastError() << std::endl;
		context.reset();
		return;
	}

	addIOContext(context);
}

//
//  Add a client connection context structure to the global list of context structures.
//
void CIOCPMySQLServer::addIOContext(ClientContextPtr lpPerSocketContext) {

	std::lock_guard<std::mutex> lock(m_mContextsGuard);
	//Добавляем копию контекста в вектор, количество ссылок в умном указателе увеличивается на 1 и будет равно 2, когда мы получим новое клиентское сединение в Run,
	//то количество ссылок уменьшится на 1, когда клиент разорвет соединение то объект уничтожится.
	m_vContexts.push_back(std::move(lpPerSocketContext));

	return;
}

//
void CIOCPMySQLServer::removeIOContext(ClientContextPtr lpPerSocketContext) {


	std::lock_guard<std::mutex> lock(m_mContextsGuard);

	auto it = std::find(m_vContexts.begin(), m_vContexts.end(), lpPerSocketContext);

	if (it != m_vContexts.end()) {

		it->reset();
		m_vContexts.erase(it);
	}


	return;
}

