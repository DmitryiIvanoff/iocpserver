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
bool CIOCPMySQLServer::ConsoleEventHandler(DWORD dwEvent) {

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

		break;

	default:
		// unknown type--better pass it on.
		return false;
	}
	return true;
}

CIOCPMySQLServer::CIOCPMySQLServer(int threadCount,const std::string mySQLPort) :
	clientIOCP(INVALID_HANDLE_VALUE),
	m_hIOCP(INVALID_HANDLE_VALUE),
	m_dThreadCount(threadCount),
	m_sMySQLPort(mySQLPort)
{
	WSADATA wsaData;

	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::ConsoleEventHandler), true)) {
		std::cout << "MySQL: SetConsoleCtrlHandler() failed to install console handler:" << GetLastError() << std::endl;
		return;
	}
	//инициализируем использование Winsock DLL процессом.
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "MySQL:WSAStartup() failed" << std::endl;
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::ConsoleEventHandler), false);
		return;
	}

	//создаем порт завершени€. “ут INVALID_HANDLE_VALUE сообщает системе что нужно создать новый порт завершени€.
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		std::cout << "MySQL:CreateIoCompletionPort() failed to create I/O completion port: " << GetLastError() << std::endl;
		return;
	}

	for (size_t i = 0; i < m_dThreadCount; i++) {
		m_vWorkerPayloads.push_back(std::move(std::make_shared<std::thread>(&CIOCPMySQLServer::WorkerThread, this)));
	}

	m_pLogger = CLogger::GetLogger();
	
	currentServer = this;
}

CIOCPMySQLServer::~CIOCPMySQLServer() {
	g_bEndServer = true;

	//ѕушим в очередь порта завершени€ данные нулевой длины - сигналим рабочим потокам, что нужно завершить работу.
	if (m_hIOCP) {
		for (size_t i = 0; i < m_vWorkerPayloads.size(); i++) {
			PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);
		}
	}

	//јттачим рабочие птококи с уже завершенными в них рутинами чтобы синхронно и безопасно завершить их работу.
	for (auto it = m_vWorkerPayloads.begin(); it != m_vWorkerPayloads.end(); ++it) {

		if (it->get()->joinable() && WaitForSingleObject(it->get()->native_handle(), 500) == WAIT_OBJECT_0) {
			it->get()->join();
			it->reset();
		}
		else {
			std::cout << "MySQL: Thread[" << it->get()->get_id() << "] not stopped." << std::endl;
		}

	}

	if (m_hIOCP) {
		CloseHandle(m_hIOCP);
		m_hIOCP = nullptr;
	}

	WSACleanup();
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::ConsoleEventHandler), false);
}

//»нициализируем сокет, который будет слушать порт к которому будут коннектитьс€ клиенты.
SOCKET CIOCPMySQLServer::CreateSocket(std::string port) {

	SOCKET socket;

	struct addrinfo hints = { 0 };
	struct addrinfo *addrlocal;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;
#ifdef _DEBUG
	hints.ai_flags = AI_NUMERICHOST;//AI_NUMERICHOST - дл€ того чтобы RawCap смог трейсить пакеты между прокси и мускулом

	//конвертим адресс. ¬ этом методе аллоцируетс€ пам€ть дл€ указател€ addrlocal. 
	if (getaddrinfo("127.0.0.1", port.c_str(), &hints, &addrlocal) != 0 || !addrlocal) {
		std::cout << "MySQL: getaddrinfo() failed with error " << WSAGetLastError() << std::endl;
		return NULL;
	}
#else
	hints.ai_flags = 0;

	//конвертим адресс. ¬ этом методе аллоцируетс€ пам€ть дл€ указател€ addrlocal. 
	if (getaddrinfo(nullptr, port.c_str(), &hints, &addrlocal) != 0 || !addrlocal) {
		std::cout << "MySQL: getaddrinfo() failed with error " << WSAGetLastError() << std::endl;
		return NULL;
	}
#endif

	socket = WSASocket(addrlocal->ai_family, addrlocal->ai_socktype, addrlocal->ai_protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (socket == INVALID_SOCKET) {
		std::cout << "MySQL: WSASocket(g_sdListen) failed: " << WSAGetLastError() << std::endl;
		return NULL;
	}

	const char chOpt = 1;
	if ((setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &chOpt, sizeof(char))) == SOCKET_ERROR) {
		std::cout << "setsockopt failed: " << WSAGetLastError() << std::endl;
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
int CIOCPMySQLServer::WorkerThread(LPVOID WorkThreadContext) {

	CIOCPMySQLServer* server = static_cast<CIOCPMySQLServer*>(WorkThreadContext);
	HANDLE hIOCP = server->m_hIOCP;
	bool bSuccess = false;

	LPWSAOVERLAPPED lpOverlapped = nullptr;
	ClientContextPtr lpPerSocketContext = nullptr;
	IOContextPtr lpIOContext;
	DWORD dwIoSize = 0;

	while (true) {

		//—читываем из очереди сообщени€-данные. ¬ызов этой функции блокирует поток до тех пор пока она не считает данные из очереди.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&lpPerSocketContext), &lpOverlapped, INFINITE);
		if (g_bEndServer || !lpPerSocketContext.get() || !lpPerSocketContext->m_pIOContext.get()) {
			//выходим из цикла и заканчиваем синхронно работу - в деструкторе этот поток аттачитс€ к главному.
			break;
		}

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//server->removeIOContext(lpPerSocketContext);
			continue;
		}

		lpIOContext = lpPerSocketContext->m_pIOContext;
		if (!lpIOContext) {
			//клиент дропнул соединение:
			continue;
		}
		
		switch (lpIOContext->IOOperation) {
		case WriteToClient:
			std::cout << "Incorrect sequence" << std::endl;
			break;
		case AcceptClient:
			//клиент подключилс€ -  коннектимс€ к Ѕƒ, читаем запрос и шлем ответ клиенту ч/з клиентскую рутину.
			lpIOContext->m_dMySQLSocket = server->CreateSocket(server->m_sMySQLPort);
			lpIOContext->IOOperation = SendToClient;

			if (!lpIOContext->m_dMySQLSocket)
			{
				std::cout << "Error..." << std::endl;
				//server->RemoveSession(lpPerSocketContext);
				break;
			}
			server->RecvBuffer(lpIOContext->m_dMySQLSocket, lpPerSocketContext->m_pIOContext);

			server->PostToIOCP(lpPerSocketContext.get());

			break;

		case ReadFromClient:
			//считали ответ от клиента - шлем его в Ѕƒ, читаем ответ и шлем обратно ч/з клиентскую рутину
			lpIOContext->IOOperation = SendToClient;

			if (!server->SendBuffer(lpIOContext->m_dMySQLSocket, lpIOContext)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Send failed: " << WSAGetLastError() << std::endl;;
				//server->RemoveSession(lpPerSocketContext);
				continue;
			}

			if (!server->RecvBuffer(lpIOContext->m_dMySQLSocket, lpIOContext)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Receive failed: " << WSAGetLastError() << std::endl;
				//server->RemoveSession(lpPerSocketContext);
				continue;
			}

			server->PostToIOCP(lpPerSocketContext.get());
			break;
		case SendToClient:
			std::cout << "Incorrect sequence" << std::endl;
			break;
		}

		lpPerSocketContext = nullptr;
		lpOverlapped = nullptr;
	}
	std::cout << "Thread ended" << std::endl;
	return 0;
}

bool CIOCPMySQLServer::PostToIOCP(CClientContext* lpPerSocketContext) {
	
	size_t size = sizeof(*lpPerSocketContext);
	return PostQueuedCompletionStatus(clientIOCP, size, (DWORD)(lpPerSocketContext), &(lpPerSocketContext->m_pIOContext->overlapped));
}

bool CIOCPMySQLServer::RecvBuffer(SOCKET recvSocket, IOContextPtr buffer) {

	DWORD dwFlags = 0;
	LPWSABUF buffRecv = &buffer->wsabuf;

	buffRecv->buf = buffer->buffer;
	buffRecv->len = MAX_BUFF_SIZE;

	while ((buffer->nTotalBytes = recv(recvSocket, buffRecv->buf, buffRecv->len, dwFlags)) == MAX_BUFF_SIZE) {

		m_pLogger->Write(buffer);

		//напр€мую пишем в сокет клиента большие данные
		if (!SendBuffer(buffer->m_dClientSocket, buffer)) {
			return false;
		}

		buffRecv->buf = buffer->buffer;
	}

	m_pLogger->Write(buffer);

	return true;
}

bool CIOCPMySQLServer::SendBuffer(SOCKET sendSocket, IOContextPtr buffer) {

	LPWSABUF buffSend = &buffer->wsabuf;
	buffSend->buf = buffer->buffer;
	buffSend->len = buffer->nTotalBytes;
	DWORD dwFlags = 0;

	return (send(sendSocket, buffSend->buf, buffSend->len, dwFlags) != SOCKET_ERROR);
}