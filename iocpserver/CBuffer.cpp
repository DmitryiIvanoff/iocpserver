#include "CBuffer.h"

CBuffer::CBuffer(SOCKET clientSocket) :
	nTotalBytes(0),
	nSentBytes(0),
	IOOperation(AcceptClient),
	m_dClientSocket(clientSocket),
	m_dMySQLSocket(INVALID_SOCKET),
	m_dSessionId(0)
{
	overlapped.Internal = 0;
	overlapped.InternalHigh = 0;
	overlapped.Offset = 0;
	overlapped.OffsetHigh = 0;
	overlapped.hEvent = nullptr;
	wsabuf.buf = buffer;
	wsabuf.len = MAX_BUFF_SIZE;
	std::fill(wsabuf.buf, wsabuf.buf + wsabuf.len, 0);
}

CBuffer::~CBuffer() {

	LINGER  lingerStruct;
	lingerStruct.l_onoff = 1;
	lingerStruct.l_linger = 0;

	if (m_dClientSocket != INVALID_SOCKET) {
		setsockopt(m_dClientSocket, SOL_SOCKET, SO_LINGER, (char *)&lingerStruct, sizeof(lingerStruct));
		closesocket(m_dClientSocket);
		m_dClientSocket = INVALID_SOCKET;
	}
	if (m_dMySQLSocket != INVALID_SOCKET) {
		setsockopt(m_dMySQLSocket, SOL_SOCKET, SO_LINGER, (char *)&lingerStruct, sizeof(lingerStruct));
		closesocket(m_dMySQLSocket);
		m_dMySQLSocket = INVALID_SOCKET;
	}

	while (!m_mClientSync.try_lock()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	m_mClientSync.unlock();
}

