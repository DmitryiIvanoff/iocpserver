#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
#include "CMySQLRoutine.h"


extern bool g_bEndServer;// установитс€ в true в обработчиках событий консоли в рабочих рутинах
MySQLRoutinePtr CMySQLRoutine::currentRoutine = nullptr;

static std::mutex printMutex;

bool CMySQLRoutine::ConsoleEventHandler(DWORD dwEvent) {

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

		break;

	default:
		return false;
	}
	return true;
}

CMySQLRoutine::CMySQLRoutine(const int threadCount,const std::string mySQLPort) :
	clientIOCP(INVALID_HANDLE_VALUE),
	m_hIOCP(INVALID_HANDLE_VALUE),
	m_dThreadCount(threadCount),
	m_sMySQLPort(mySQLPort)
{
	WSADATA wsaData;

	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CMySQLRoutine::ConsoleEventHandler), true)) {
		std::cout << "MySQL: SetConsoleCtrlHandler() failed to install console handler:" << GetLastError() << std::endl;
		return;
	}
	//инициализируем использование Winsock DLL процессом.
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "MySQL:WSAStartup() failed" << std::endl;
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CMySQLRoutine::ConsoleEventHandler), false);
		return;
	}

	//создаем порт завершени€. “ут INVALID_HANDLE_VALUE сообщает системе что нужно создать новый порт завершени€.
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		std::cout << "MySQL:CreateIoCompletionPort() failed to create I/O completion port: " << GetLastError() << std::endl;
		return;
	}

	currentRoutine.reset(this);

	std::shared_ptr<std::thread> workThread;
	for (size_t i = 0; i < m_dThreadCount; i++) {

		workThread = std::make_shared<std::thread>(&CMySQLRoutine::WorkerThread);

		m_vWorkerPayloads.push_back(std::move(workThread));
	}

	m_pLogger = CLogger::GetLogger();

}

CMySQLRoutine::~CMySQLRoutine() {
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
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CMySQLRoutine::ConsoleEventHandler), false);

	currentRoutine.reset();
}

//»нициализируем сокет, который будет слушать порт к которому будут коннектитьс€ клиенты.
SOCKET CMySQLRoutine::CreateSocket(std::string port) {

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


int CMySQLRoutine::WorkerThread() {

	HANDLE hIOCP = currentRoutine->m_hIOCP;
	bool bSuccess = false;

	LPWSAOVERLAPPED lpOverlapped = nullptr;
	BufferPtr buffer;
	DWORD dwIoSize = 0;

	while (true) {

		//—читываем из очереди сообщени€-данные. ¬ызов этой функции блокирует поток до тех пор пока она не считает данные из очереди.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&buffer), &lpOverlapped, INFINITE);
		if (g_bEndServer || !buffer.get()) {
			//выходим из цикла и заканчиваем синхронно работу - в деструкторе этот поток аттачитс€ к главному.
			break;
		}

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//server->RemoveSession(buffer);
			continue;
		}
		
		switch (buffer->IOOperation) {
		case AcceptClient:
			//клиент подключилс€ -  коннектимс€ к Ѕƒ, читаем запрос и шлем ответ клиенту ч/з клиентскую рутину.
			currentRoutine->OnClientAccepted(buffer);
			break;

		case ReadFromClient:
			//считали ответ от клиента - шлем его в Ѕƒ, читаем ответ и шлем обратно ч/з клиентскую рутину
			currentRoutine->OnClientDataReceived(buffer);
			break;
		default:
			std::cout << "Incorrect sequence" << std::endl;
			break;
		}

		buffer.reset();
		lpOverlapped = nullptr;
	}
	std::cout << "Thread ended" << std::endl;
	return 0;
}

void CMySQLRoutine::OnClientAccepted(BufferPtr buffer) {
	buffer->m_dMySQLSocket = CreateSocket(m_sMySQLPort);
	buffer->IOOperation = SendToClient;

	if (!buffer->m_dMySQLSocket)
	{
		std::cout << "Error..." << std::endl;
		//server->RemoveSession(buffer);
		return;
	}
	RecvBuffer(buffer->m_dMySQLSocket, buffer);

	PostToIOCP(buffer.get());
}
void CMySQLRoutine::OnClientDataReceived(BufferPtr buffer) {
	buffer->IOOperation = SendToClient;

	if (!SendBuffer(buffer->m_dMySQLSocket, buffer)) {
		std::cout << "MySQL[" << std::this_thread::get_id() << "]: Send failed: " << WSAGetLastError() << std::endl;;
		//server->RemoveSession(buffer);
		return;
	}

	if (!RecvBuffer(buffer->m_dMySQLSocket, buffer)) {
		std::cout << "MySQL[" << std::this_thread::get_id() << "]: Receive failed: " << WSAGetLastError() << std::endl;
		//server->RemoveSession(buffer);
		return;
	}

	PostToIOCP(buffer.get());
}

bool CMySQLRoutine::PostToIOCP(CBuffer* bufferPtr) {
	
	size_t size = sizeof(*bufferPtr);
	return PostQueuedCompletionStatus(clientIOCP, size, (DWORD)(bufferPtr), &(bufferPtr->overlapped));
}

bool CMySQLRoutine::RecvBuffer(SOCKET recvSocket, BufferPtr buffer) {

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

bool CMySQLRoutine::SendBuffer(SOCKET sendSocket, BufferPtr buffer) {

	LPWSABUF buffSend = &buffer->wsabuf;
	buffSend->buf = buffer->buffer;
	buffSend->len = buffer->nTotalBytes;
	DWORD dwFlags = 0;

	return (send(sendSocket, buffSend->buf, buffSend->len, dwFlags) != SOCKET_ERROR);
}