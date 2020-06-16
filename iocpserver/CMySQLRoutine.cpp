#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
#include <sstream>
#include "CMySQLRoutine.h"


extern bool g_bEndServer;// ����������� � true � ������������ ������� ������� � ������� �������
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

MySQLRoutinePtr CMySQLRoutine::GetInstance(const int threadCount, const std::string listenPort) {
	if (!currentRoutine) {
		//�������� currentRoutine ���������� � ������������, � ������������ ����� ��������� 
		//�.�. � ������������ �������� ������� ������ � ������� ����� ��� ������������������ ����� ��������� currentRoutine
		new CMySQLRoutine(threadCount, listenPort);
	}
	return currentRoutine;
}

CMySQLRoutine::CMySQLRoutine(const int threadCount,const std::string mySQLPort) :
	m_hClientIOCP(INVALID_HANDLE_VALUE),
	m_hIOCP(INVALID_HANDLE_VALUE),
	m_dThreadCount(threadCount),
	m_sMySQLPort(mySQLPort)
{
	WSADATA wsaData;

	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CMySQLRoutine::ConsoleEventHandler), true)) {
		m_pLogger->Error("MySQL: SetConsoleCtrlHandler() failed to install console handler:" + GetLastError() );
		return;
	}
	//�������������� ������������� Winsock DLL ���������.
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		m_pLogger->Error("MySQL:WSAStartup() failed" );
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CMySQLRoutine::ConsoleEventHandler), false);
		return;
	}

	//������� ���� ����������. ��� INVALID_HANDLE_VALUE �������� ������� ��� ����� ������� ����� ���� ����������.
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		m_pLogger->Error("MySQL:CreateIoCompletionPort() failed to create I/O completion port: " + GetLastError() );
		return;
	}

	currentRoutine.reset(this);

	std::shared_ptr<std::thread> workThread;
	for (size_t i = 0; i < m_dThreadCount; i++) {

		workThread = std::make_shared<std::thread>(&CMySQLRoutine::WorkerThread);

		m_vWorkerPayloads.push_back(std::move(workThread));
	}

	m_pLogger = CLogger::GetInstance();

}

CMySQLRoutine::~CMySQLRoutine() {
	g_bEndServer = true;

	//����� � ������� ����� ���������� ������ ������� ����� - �������� ������� �������, ��� ����� ��������� ������.
	if (m_hIOCP) {
		for (size_t i = 0; i < m_vWorkerPayloads.size(); i++) {
			PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);
		}
	}

	//������� ������� ������� � ��� ������������ � ��� �������� ����� ��������� � ��������� ��������� �� ������.
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

	if (m_hIOCP) {
		CloseHandle(m_hIOCP);
		m_hIOCP = nullptr;
	}

	WSACleanup();
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CMySQLRoutine::ConsoleEventHandler), false);

	currentRoutine.reset();
}

//�������������� �����, ������� ����� ������� ���� � �������� ����� ������������ �������.
SOCKET CMySQLRoutine::CreateSocket(std::string port) {

	SOCKET socket;

	struct addrinfo hints = { 0 };
	struct addrinfo *addrlocal;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;
#ifdef _DEBUG
	hints.ai_flags = AI_NUMERICHOST;//AI_NUMERICHOST - ��� ���� ����� RawCap ���� �������� ������ ����� ������ � ��������

	//��������� ������. � ���� ������ ������������ ������ ��� ��������� addrlocal. 
	if (getaddrinfo("127.0.0.1", port.c_str(), &hints, &addrlocal) != 0 || !addrlocal) {
		m_pLogger->Error("MySQL: getaddrinfo() failed with error " + WSAGetLastError() );
		return NULL;
	}
#else
	hints.ai_flags = 0;

	//��������� ������. � ���� ������ ������������ ������ ��� ��������� addrlocal. 
	if (getaddrinfo(nullptr, port.c_str(), &hints, &addrlocal) != 0 || !addrlocal) {
		m_pLogger->Error("MySQL: getaddrinfo() failed with error " + WSAGetLastError() );
		return NULL;
	}
#endif

	socket = WSASocket(addrlocal->ai_family, addrlocal->ai_socktype, addrlocal->ai_protocol, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (socket == INVALID_SOCKET) {
		m_pLogger->Error("MySQL: WSASocket(g_sdListen) failed: " + WSAGetLastError() );
		return NULL;
	}

	const char chOpt = 1;
	if ((setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &chOpt, sizeof(char))) == SOCKET_ERROR) {
		m_pLogger->Error("setsockopt failed: " + WSAGetLastError() );
		return NULL;

	}
	if (connect(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen) < 0)
	{
		m_pLogger->Error("MySQL:  error " + WSAGetLastError());
		return NULL;
	}
	//������������ ��������� addrlocal.
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

		//��������� �� ������� ���������-������. ����� ���� ������� ��������� ����� �� ��� ��� ���� ��� �� ������� ������ �� �������.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&buffer), &lpOverlapped, INFINITE);
		if (g_bEndServer || !buffer.get()) {
			//������� �� ����� � ����������� ��������� ������ - � ����������� ���� ����� ��������� � ��������.
			break;
		}

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			buffer->IOOperation = ErrorInDB;
			currentRoutine->PostToIOCP(buffer.get());
			continue;
		}
		
		switch (buffer->IOOperation) {
		case AcceptClient:
			//������ ����������� -  ����������� � ��, ������ ������ � ���� ����� ������� �/� ���������� ������.
			currentRoutine->OnClientAccepted(buffer);
			break;

		case ReadFromClient:
			//������� ����� �� ������� - ���� ��� � ��, ������ ����� � ���� ������� �/� ���������� ������
			currentRoutine->OnClientDataReceived(buffer);
			break;
		default:
			currentRoutine->m_pLogger->Error("Incorrect sequence" );
			break;
		}

		buffer.reset();
		lpOverlapped = nullptr;
	}
	return 0;
}

void CMySQLRoutine::OnClientAccepted(BufferPtr buffer) {
	buffer->m_dMySQLSocket = CreateSocket(m_sMySQLPort);
	buffer->IOOperation = SendToClient;

	if (!buffer->m_dMySQLSocket)
	{
		m_pLogger->Error("Error..." );
		buffer->IOOperation = ErrorInDB;
		PostToIOCP(buffer.get());
		return;
	}
	RecvBuffer(buffer->m_dMySQLSocket, buffer);

	PostToIOCP(buffer.get());
}
void CMySQLRoutine::OnClientDataReceived(BufferPtr buffer) {
	buffer->IOOperation = SendToClient;

	if (!SendBuffer(buffer->m_dMySQLSocket, buffer)) {
		m_pLogger->Error("MySQL Send failed: " + WSAGetLastError() );
		buffer->IOOperation = ErrorInDB;
		PostToIOCP(buffer.get());
		return;
	}

	if (!RecvBuffer(buffer->m_dMySQLSocket, buffer)) {
		m_pLogger->Error("MySQL Receive failed: " + WSAGetLastError() );
		buffer->IOOperation = ErrorInDB;
		PostToIOCP(buffer.get());
		return;
	}

	PostToIOCP(buffer.get());
}

bool CMySQLRoutine::PostToIOCP(CBuffer* bufferPtr) {
	
	size_t size = sizeof(*bufferPtr);
	return PostQueuedCompletionStatus(m_hClientIOCP, size, (DWORD)(bufferPtr), &(bufferPtr->overlapped));
}

bool CMySQLRoutine::RecvBuffer(SOCKET recvSocket, BufferPtr buffer) {

	DWORD dwFlags = 0;
	LPWSABUF buffRecv = &buffer->wsabuf;

	buffRecv->buf = buffer->buffer;
	buffRecv->len = MAX_BUFF_SIZE;

	while ((buffer->nTotalBytes = recv(recvSocket, buffRecv->buf, buffRecv->len, dwFlags)) == MAX_BUFF_SIZE) {

		m_pLogger->Write(buffer);

		//�������� ����� � ����� ������� ������� ������
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