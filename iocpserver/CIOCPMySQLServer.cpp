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
bool CIOCPMySQLServer::�trlHandler(DWORD dwEvent) {

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
	m_dMySQLSocket(INVALID_SOCKET),
	clientIOCP(INVALID_HANDLE_VALUE)
{
	WSADATA wsaData;

	if (!SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::�trlHandler), true)) {
		std::cout << "MySQL: SetConsoleCtrlHandler() failed to install console handler:" << GetLastError() << std::endl;
		return;
	}
	//�������������� ������������� Winsock DLL ���������.
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "MySQL:WSAStartup() failed" << std::endl;
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::�trlHandler), false);
		return;
	}

	//������� ���� ����������. ��� INVALID_HANDLE_VALUE �������� ������� ��� ����� ������� ����� ���� ����������.
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	if (m_hIOCP == nullptr) {
		std::cout << "MySQL:CreateIoCompletionPort() failed to create I/O completion port: " << GetLastError() << std::endl;
		return;
	}

	for (int i = 0; i < m_dThreadCount; i++) {
		m_vWorkerThread.push_back(std::move(std::make_shared<std::thread>(&CIOCPMySQLServer::workerThread, this)));
	}

	m_dMySQLSocket = CreateSocket(mySQLPosrt);
	if (!m_dMySQLSocket)
	{
		return;
	}

	m_pLogger = CLogger::GetLogger();
	
	currentServer = this;
}

CIOCPMySQLServer::~CIOCPMySQLServer() {
	g_bEndServer = true;

	//���������� �� ���� ���������� ������ ������� ����� ����� ����������� ������� GetQueuedCompletionStatus 
	//�������� ��� ������ � workerThread ��������� ����������� �� ����� ��� ����������� ����� ���������� ������ � �������� ���� ����������.
	if (m_hIOCP) {
		for (size_t i = 0; i < m_vWorkerThread.size(); i++) {
			PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);
		}
	}

	//��������� ����������� �� ��� ������, ���� ��� �� ������� �� � �������� ����� ����� ��������� � ��������� ��������� ��.
	for (auto it = m_vWorkerThread.begin(); it != m_vWorkerThread.end(); ++it) {

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
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CIOCPMySQLServer::�trlHandler), false);
}

//�������������� �����, ������� ����� ������� ���� � �������� ����� ������������ �������.
SOCKET CIOCPMySQLServer::CreateSocket(std::string port) {

	SOCKET socket;

	struct addrinfo hints = { 0 };
	struct addrinfo *addrlocal;

	hints.ai_flags = AI_NUMERICHOST;//AI_NUMERICHOST - ��� ���� ����� �������� ���� �������� ������ ����� ������ � ��������
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;

	//��������� ������. � ���� ������ ������������ ������ ��� ��������� addrlocal. 
	if (getaddrinfo("127.0.0.1", port.c_str(), &hints, &addrlocal) != 0 || !addrlocal) {
		std::cout << "MySQL: getaddrinfo() failed with error " << WSAGetLastError() << std::endl;
		return NULL;
	}
	//������� �����
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
	//������������ ��������� addrlocal.
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
			//������� �� ����� � ����������� ��������� ������ - � ����������� ���� ����� ��������� � ��������.
			break;
		}

		//������������ ������� ����� � ���� ������� ����� ����������, ������� ��� ������ ����� ��. ������� ��������� �����, ���� � ������� ����������� �������.
		//����� ����� ������ ��������� �����, �.�. �������� ����� ����� �������� ���. ���� � ���� ������ ������ ����������, �� 
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&lpPerSocketContext), &lpOverlapped, INFINITE);

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//server->removeIOContext(lpPerSocketContext);
			continue;
		}

		if (!lpPerSocketContext.get() || !lpPerSocketContext->m_pIOContext.get()) {

			break;
		}

		lpIOContext = lpPerSocketContext->m_pIOContext;
		if (!lpIOContext) {
			//������ ������� ����������:
			continue;
		}
		//lpIOContext->nTotalBytes = dwIoSize;
		
		switch (lpIOContext->IOOperation) {
		case WriteToClient:
			std::cout << "Incorrect sequence" << std::endl;
			break;
		case AcceptClient:
			//������ ����������� -  ����������� � ��, ������ ������ � ���� ������� �/� ���������� ������.
			lpIOContext->m_dMySQLSocket = server->CreateSocket("3306");
			lpIOContext->IOOperation = SendToClient;

			if (!lpIOContext->m_dMySQLSocket)
			{
				std::cout << "Error..." << std::endl;
				break;
			}
			server->RecvBuffer(lpIOContext->m_dMySQLSocket, lpPerSocketContext->m_pIOContext);

			server->PostToIOCP(lpPerSocketContext.get());

			break;

		case ReadFromClient:
			//������� ����� �� ������� - ���� ��� � ��, ������ ����� � ���� ������� �/� ���������� ������
			//lpIOContext->nTotalBytes = dwIoSize;
			lpIOContext->IOOperation = SendToClient;

			server->m_pLogger->Write(lpIOContext->buffer, lpIOContext->nTotalBytes);

			if (!server->SendBuffer(lpIOContext->m_dMySQLSocket, lpIOContext)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Send failed: " << WSAGetLastError() << std::endl;;
				//server->RemoveSession(lpPerSocketContext);
			}

			if (!server->RecvBuffer(lpIOContext->m_dMySQLSocket, lpIOContext)) {
				std::cout << "MySQL[" << std::this_thread::get_id() << "]: Receive failed: " << WSAGetLastError() << std::endl;
				//server->RemoveSession(lpPerSocketContext);
			}

			server->PostToIOCP(lpPerSocketContext.get());
			break;
		case SendToClient:
			std::cout << "Incorrect sequence" << std::endl;
			break;
		}
	}
	std::cout << "Thread ended" << std::endl;
	return 0;
}

bool CIOCPMySQLServer::PostToIOCP(CClientContext* lpPerSocketContext) {
	
	size_t size = sizeof(*lpPerSocketContext);
	return PostQueuedCompletionStatus(clientIOCP, size, (DWORD)(lpPerSocketContext), &(lpPerSocketContext->m_pIOContext->overlapped));

}

bool CIOCPMySQLServer::RecvBuffer(SOCKET recvSocket, IOContextPtr data) {

	DWORD dwFlags = 0;
	LPWSABUF buffRecv = &data->wsabuf;

	buffRecv->buf = data->buffer;
	buffRecv->len = MAX_BUFF_SIZE;

	while ((data->nTotalBytes = recv(recvSocket, buffRecv->buf, buffRecv->len, dwFlags)) == MAX_BUFF_SIZE) {

		m_pLogger->Write(data->buffer, data->nTotalBytes);

		//�������� ����� � ����� ������� ������� ������
		if (!SendBuffer(data->m_dClientSocket, data)) {
			break;
		}

		buffRecv->buf = data->buffer;
	}

	m_pLogger->Write(data->buffer, data->nTotalBytes);

	return true;
}

bool CIOCPMySQLServer::SendBuffer(SOCKET sendSocket, IOContextPtr data) {

	LPWSABUF buffSend = &data->wsabuf;
	buffSend->buf = data->buffer;
	buffSend->len = data->nTotalBytes;
	DWORD dwFlags = 0;
	//send(sendSocket, buffSend->buf, buffSend->len, dwFlags);

	return (send(sendSocket, buffSend->buf, buffSend->len, dwFlags) != SOCKET_ERROR);

}