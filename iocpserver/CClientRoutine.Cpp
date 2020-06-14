#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
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

		//������� �� ����� � CClientRoutine::Start
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
		std::cout << "SetConsoleCtrlHandler() failed to install console handler:" << GetLastError() << std::endl;
		return;
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "WSAStartup() failed" << std::endl;
		SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CClientRoutine::ConsoleEventHandler), false);
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

	currentRoutine.reset(this);

	std::shared_ptr<std::thread> workThread;
	for (size_t i = 0; i < m_dThreadCount; i++) {

		workThread = std::make_shared<std::thread>(&CClientRoutine::WorkerThread);

		m_vWorkerPayloads.push_back(std::move(workThread));
	}	

	m_pLogger = CLogger::GetLogger();
}

void CClientRoutine::Start() {

	SOCKET sdClient = INVALID_SOCKET;
	
	BufferPtr buffer = nullptr;

	g_bEndServer = false;

	while (!g_bEndServer) {

		//��������� ������ ���������� �� ������� ��������� ���������� �� ������ m_sdListen
		//� ���������� ����� ���������� �������.
		sdClient = WSAAccept(m_dListenSocket, nullptr, nullptr, nullptr, 0);
		if (sdClient == SOCKET_ERROR) {

			//������ Ctrl+C
			std::cout << "WSAAccept() failed: " << WSAGetLastError() << std::endl;
			break;
		}

		//��������� ����� ������� � ������ ���������� , ���������� ��������� � ��������� �� � ���������
		UpdateCompletionPort(buffer, sdClient);
		if (buffer == nullptr) {					
			break;
		}

		//����������� ����� ������ - ������ ������� �� ���� � ���� ���������� ���������������� ������ �������������� ���������� � ��
		//(��. https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase.html)
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
			std::cout << "Thread[" << it->get()->get_id() << "] not stopped." << std::endl;
		}
		
	}

	//�� ���������� ��������� ��� ����������� ����������� ����������, ����� �.�. ������ ����� �������� == nullptr
	for (size_t i = 0; i < m_vConnectedClients.size(); i++) {
		RemoveSession(m_vConnectedClients[i]);
	}
	m_vConnectedClients.clear();

	if (m_hIOCP) {
		CloseHandle(m_hIOCP);
		m_hIOCP = nullptr;
	}

	//��������� ���� � ������� �������������� ����� ���������� ���������� (���� ���� ����������� �� ����������� ������ WSAAccept)
	if (m_dListenSocket != INVALID_SOCKET) {
		closesocket(m_dListenSocket);
		m_dListenSocket = INVALID_SOCKET;
	}

	WSACleanup();
	SetConsoleCtrlHandler(reinterpret_cast<PHANDLER_ROUTINE>(CClientRoutine::ConsoleEventHandler), false);

	currentRoutine.reset();
}

//�������������� �����, ������� ����� ������� ���� � �������� ����� ������������ �������.
SOCKET CClientRoutine::CreateSocket(const std::string port, const bool isListenSocket) {
	SOCKET socket;

	struct addrinfo hints = {0};
	struct addrinfo *addrlocal;

	hints.ai_flags = isListenSocket ? AI_PASSIVE : 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_IP;

	//��������� ������. � ���� ������ ������������ ������ ��� ��������� addrlocal. 
	if(getaddrinfo(nullptr, port.c_str(), &hints, &addrlocal) != 0  || !addrlocal) {
		std::cout << "getaddrinfo() failed with error " << WSAGetLastError() << std::endl;
        return NULL;
	}
	//������� �����
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

	//����������� ����� � �������������� ����� �������
	if(bind(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen) == SOCKET_ERROR ) {
		std::cout << "bind() failed: " << WSAGetLastError() << std::endl;
		return NULL;
	}

	//��������� ����� � ����� ������������� � ������������ ������ �������  == ���������� �������
	if(listen(socket, m_dThreadCount) == SOCKET_ERROR ) {
		std::cout << "listen() failed: " << WSAGetLastError() << std::endl;
		return NULL;
	}

	//������������� ������ ������ 0 (SO_SNDBUF).
	//int nZero = 0;
	//if(setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&nZero, sizeof(nZero)) == SOCKET_ERROR ) {
	//	//std::cout << "Error: " << WSAGetLastError() << std::endl;
	//	return NULL;
	//}

	freeaddrinfo(addrlocal);

	return socket;
}

//������� ����� ������������ ��� ������� ������ � ������ � ������ ��������� � ������ ����������.
int CClientRoutine::WorkerThread() {

	HANDLE hIOCP = currentRoutine->m_hIOCP;
	bool bSuccess = false;
	BufferPtr buffer = nullptr;
	LPWSAOVERLAPPED lpOverlapped = nullptr;

	DWORD dwIoSize = 0;
	
	while(true) {
		
        //������������ ������� ����� � ���� ������� ����� ����������, ������� ��� ������ ����� ��. ������� ��������� �����, ���� � ������� ����������� �������.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&buffer), &lpOverlapped, INFINITE);
		if (g_bEndServer || !buffer.get()) {
			//������� �� ����� � ����������� ��������� ������ - � ����������� ���� ����� ��������� � ��������.
			break;
		}

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//���������� � �������� ���������
			currentRoutine->RemoveSession(buffer);
			continue;
		}

		switch(buffer->IOOperation ) {
		case WriteToClient:
			//������ ������ ����� �� ������ - ���������� ����������� ������ ������ �� �������
			currentRoutine->OnClientDataSended(buffer);
			break;
		case ReadFromIOCP:
			//�������� ����� �� ������� - ���� ��� �/� ������, ������������� ���������� � �� 
			buffer->nTotalBytes = dwIoSize;
			currentRoutine->OnClientDataReceived(buffer);
			break;
		case ReadFromClient:
			std::cout << "Incorrect sequence" << std::endl;
			break;
		case SendToClient:
			//����� �� �� ������� �/� ������, �������������� ��, ���� ��� ���������� �������.
			currentRoutine->OnMySQLDataReceived(buffer);
			break;
		default:
			std::cout << "Incorrect sequence." << std::endl;
		}

		buffer.reset();
		lpOverlapped = nullptr;
	}

	return 0;
}

void CClientRoutine::OnClientDataSended(BufferPtr buffer) {

	buffer->IOOperation = ReadFromIOCP;

	if (!RecvBufferAsync(buffer)) {

		std::cout << "Receive failed: " << WSAGetLastError() << std::endl;
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

		std::cout << "Send failed: " << WSAGetLastError() << std::endl;
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
		std::cout << "WSArecvBuffer: error " << WSAGetLastError() << " in receive" << std::endl;
		return false;
	}
		
	return true;
}

bool CClientRoutine::SendBufferAsync(BufferPtr buffer) {
	
	BufferPtr pBuffer = GetNextBuffer();

	while (pBuffer) {
		//��� ����� ������� ����� �� ��� ������ ���� ���� ��� �������� WSASaend ��� ������ �������.
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

//���������� �������� ��� ������ ��� ������ ���������� � ��������� ����� � ������ ����������.
void CClientRoutine::UpdateCompletionPort(BufferPtr& buffer, SOCKET sdClient) {

	buffer.reset(new CBuffer(sdClient));
	if (!buffer.get()) {
		return;
	}
		
	//�������� ����� ������ ������� � ������ ����������, ����� ������� ��������� ���� ���������� � ��� ��� ����� ��������� �� ���� �������
	m_hIOCP = CreateIoCompletionPort(reinterpret_cast<HANDLE>(sdClient), m_hIOCP, reinterpret_cast<DWORD_PTR>(buffer.get()), 0);
	if(!m_hIOCP) {
		std::cout << "CreateIoCompletionPort() failed: " << GetLastError() << std::endl;
		buffer.reset();
		return;
	}

	buffer->m_dSessionId = m_dSessionId++;

	AddBufferInQueue(buffer);
	AddNewSession(buffer);
}

void CClientRoutine::AddNewSession(BufferPtr buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);
	
	m_vConnectedClients.push_back(std::move(buffer));

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

		/*it->second.reset(); - ������ ����������� � �����������*/ 

		m_mBuffer.erase(it);
		
	}
}

void CClientRoutine::AddBufferInQueue(BufferPtr& buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	InterlockedIncrement(&m_dIncomingSequence);

	buffer->sequenceNumber = m_dIncomingSequence;

	m_mBuffer.insert(std::pair<size_t, BufferPtr>(m_dIncomingSequence, buffer));

}

void CClientRoutine::RemoveSession(BufferPtr buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	auto it = std::find(m_vConnectedClients.begin(), m_vConnectedClients.end(), buffer);
			
	if (it != m_vConnectedClients.end()) {

		RemoveBufferFromQueue(*it);
		it->reset();
		m_vConnectedClients.erase(it);
	}
	
}