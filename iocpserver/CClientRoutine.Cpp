#include <Ws2tcpip.h>
#include <iostream>
#include <chrono>
#include <sstream>
#include "CClientRoutine.h"

#pragma warning( disable : 4996)

extern bool g_bEndServer;

CClientRoutine* CClientRoutine::currentRoutine = nullptr;

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

CClientRoutine* CClientRoutine::GetInstance(const int threadCount, const std::string listenPort) {
	if (!currentRoutine) {
		currentRoutine = new CClientRoutine(threadCount, listenPort);
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

	std::shared_ptr<std::thread> workThread;
	for (size_t i = 0; i < m_dThreadCount; i++) {

		workThread = std::make_shared<std::thread>(&CClientRoutine::WorkerThread, this);

		m_vWorkerPayloads.push_back(std::move(workThread));
	}	

	m_pLogger = CLogger::GetInstance();
}

void CClientRoutine::Start() {

	SOCKET sdClient = INVALID_SOCKET;
	
	CBuffer* buffer = nullptr;

	g_bEndServer = false;

	while (!g_bEndServer) {

		//��������� ������ ���������� �� ������� ��������� ���������� �� ������ m_sdListen
		//� ���������� ����� ���������� �������.
		sdClient = WSAAccept(m_dListenSocket, nullptr, nullptr, nullptr, 0);
		if (sdClient == SOCKET_ERROR) {

			//������ Ctrl+C
			m_pLogger->Error("WSAAccept() failed: " + WSAGetLastError());
			break;
		}

		//��������� ����� ������� � ������ ���������� , ���������� ��������� � ��������� �� � ���������
		buffer = UpdateCompletionPort(sdClient);
		if (!buffer) {					
			continue;
		}

		//����������� ����� ������ - ������ ������� �� ���� � ���� ���������� ���������������� ������ �������������� ���������� � ��
		//(��. https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase.html)
		PostToIOCP(buffer);

		buffer = nullptr;
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
	for (size_t i = 0; i < m_vWorkerPayloads.size(); i++) {

		auto workerThread = m_vWorkerPayloads[i];

		if (workerThread->joinable() && WaitForSingleObject(workerThread->native_handle(), 500) == WAIT_OBJECT_0) {
			workerThread->join();
			workerThread.reset();
		}
		else {
			auto threadId = workerThread->get_id();

			std::stringstream ss;

			ss << threadId;

			m_pLogger->Error("Thread[" + ss.str() + "] not stopped.");
		}
		
	}

	//�� ���������� ��������� ��� ����������� ����������� ����������, ����� �.�. ������ ����� �������� == nullptr
	for (size_t i = 0; i < m_mClients.size(); i++) {
		RemoveSession(m_mClients[i].get());
	}
	m_mClients.clear();

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

	currentRoutine = nullptr;
}

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
		m_pLogger->Error("getaddrinfo() failed with error " + WSAGetLastError());
        return NULL;
	}
	//������� �����
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

	//����������� ����� � �������������� ����� �������
	if(bind(socket, addrlocal->ai_addr, (int)addrlocal->ai_addrlen) == SOCKET_ERROR ) {
		m_pLogger->Error("bind() failed: " + WSAGetLastError());
		return NULL;
	}

	//��������� ����� � ����� ������������� � ������������ ������ �������  == ���������� �������
	if(listen(socket, m_dThreadCount) == SOCKET_ERROR ) {
		m_pLogger->Error("listen() failed: " + WSAGetLastError());
		return NULL;
	}

	freeaddrinfo(addrlocal);

	return socket;
}

//������� ����� ������������ ��� ������� ������ � ������ � ������ ��������� � ������ ����������.
int CClientRoutine::WorkerThread(CClientRoutine* routine) {

	HANDLE hIOCP = routine->m_hIOCP;
	bool bSuccess = false;
	CBuffer* buffer = nullptr;
	LPWSAOVERLAPPED lpOverlapped = nullptr;

	DWORD dwIoSize = 0;
	
	while(true) {
		
        //������������ ������� ����� � ���� ������� ����� ����������, ������� ��� ������ ����� ��. ������� ��������� �����, ���� � ������� ������ ���������� �����.
		bSuccess = GetQueuedCompletionStatus(hIOCP, &dwIoSize, reinterpret_cast<PDWORD_PTR>(&buffer), &lpOverlapped, INFINITE);
		if (g_bEndServer || !buffer) {
			//������� �� ����� � ����������� ��������� ������ - � ����������� ���� ����� ��������� � ��������.
			break;
		}

		if (!bSuccess || (bSuccess && (dwIoSize == 0))) {
			//���������� � �������� ���������
			routine->RemoveSession(buffer);
			continue;
		}

		switch(buffer->IOOperation ) {
		case WriteToClient:
			//������ ������ ����� �� ������ - ���������� ����������� ������ ������ �� �������
			routine->OnClientDataSended(buffer);
			break;
		case ReadFromIOCP:
			//�������� ����� �� ������� - ���� ��� �/� ������, ������������� ���������� � �� 
			buffer->nTotalBytes = dwIoSize;
			routine->OnClientDataReceived(buffer);
			break;
		case SendToClient:
			//����� �� �� ������� �/� ������, �������������� ��, ���� ��� ���������� �������.
			routine->OnMySQLDataReceived(buffer);
			break;
		case ErrorInDB:
			routine->RemoveSession(buffer);
			break;
		default:
			routine->m_pLogger->Error("Incorrect sequence.");
		}

		buffer = nullptr;
		lpOverlapped = nullptr;
	}

	return 0;
}

void CClientRoutine::OnClientDataSended(CBuffer* buffer) {

	buffer->IOOperation = ReadFromIOCP;

	if (!RecvBufferAsync(buffer)) {

		m_pLogger->Error("Receive failed: " + WSAGetLastError());
		RemoveSession(buffer);
	}
}

void CClientRoutine::OnClientDataReceived(CBuffer* buffer) {

	buffer->IOOperation = ReadFromClient;

	AddBufferInQueue(buffer);

	m_pLogger->Write(buffer);

	PostToIOCP(buffer);
}

void CClientRoutine::OnMySQLDataReceived(CBuffer* buffer) {

	buffer->IOOperation = WriteToClient;

	if (!SendBufferAsync(buffer)) {

		m_pLogger->Error("Send failed: " + WSAGetLastError());
		RemoveSession(buffer);
	}
}

bool CClientRoutine::PostToIOCP(CBuffer* buffer) {

	size_t size = sizeof(buffer);
	return PostQueuedCompletionStatus(m_hMySQLIOCP, size, reinterpret_cast<DWORD>(buffer), &(buffer->overlapped));

}

bool CClientRoutine::RecvBufferAsync(CBuffer* buffer) {

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

bool CClientRoutine::SendBufferAsync(const CBuffer* buffer) {
	
	CBuffer* pBuffer = GetNextBuffer();

	while (pBuffer) {
		//��� ����� ������� ����� �� ��� ������ ���� ���� ��� �������� WSASaend ��� ������ �������.
		std::lock_guard<std::mutex> lock(pBuffer->m_mClientSync);

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

//���������� �������� ��� ������ ��� ������ ���������� � ��������� ����� � ������ ����������.
CBuffer* CClientRoutine::UpdateCompletionPort(const SOCKET sdClient) {

	CBuffer* buffer = new CBuffer(sdClient);
		
	m_hIOCP = CreateIoCompletionPort(reinterpret_cast<HANDLE>(sdClient), m_hIOCP, reinterpret_cast<DWORD_PTR>(buffer), 0);
	if(!m_hIOCP) {
		
		m_pLogger->Error("CreateIoCompletionPort() failed: " + GetLastError());

		if (buffer) {
			delete buffer;
		};
		return nullptr;
	}

	AddNewSession(buffer);
	AddBufferInQueue(buffer);

	return buffer;
}

void CClientRoutine::AddNewSession(CBuffer* buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	buffer->m_dSessionId = m_dSessionId++;

	std::shared_ptr<CBuffer> newBuffer;

	newBuffer.reset(buffer);
	
	m_mClients.insert(std::pair<size_t, BufferPtr>(buffer->m_dSessionId, std::move(newBuffer)));

}

CBuffer* CClientRoutine::GetNextBuffer() {

	std::lock_guard<std::mutex> lock(m_mBufferSync);
	auto removedItemIt = std::find(m_vRemovedBufferNumbers.begin(), m_vRemovedBufferNumbers.end(), m_dOutgoingSequence);

	if (removedItemIt != m_vRemovedBufferNumbers.end()) {
		m_vRemovedBufferNumbers.erase(removedItemIt);
		InterlockedIncrement(&m_dOutgoingSequence);
	}

	CBuffer* nextPtr = nullptr;
	auto it = m_mBuffer.begin();

	if (it != m_mBuffer.end() && it->second != nullptr && it->second->sequenceNumber == m_dOutgoingSequence 
		&& it->second->IOOperation == WriteToClient) {
		
		nextPtr = it->second;
		m_mBuffer.erase(it);
	}

	return nextPtr;
}

CBuffer* CClientRoutine::ProcessNextBuffer() {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	InterlockedIncrement(&m_dOutgoingSequence);

	CBuffer* nextPtr = nullptr;

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

		m_mBuffer.erase(it);
		
	}
}

void CClientRoutine::AddBufferInQueue(CBuffer* buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	InterlockedIncrement(&m_dIncomingSequence);

	buffer->sequenceNumber = m_dIncomingSequence;

	m_mBuffer.insert(std::pair<size_t, CBuffer*>(m_dIncomingSequence, buffer));

}

void CClientRoutine::RemoveSession(const CBuffer* buffer) {

	std::lock_guard<std::mutex> lock(m_mBufferSync);

	size_t sessionId = buffer->m_dSessionId;

	auto it = m_mClients.find(sessionId);
			
	if (it != m_mClients.end()) {

		RemoveBufferFromQueue(it->second);

		it->second.reset();

		m_mClients.erase(it);
	}
	
}
