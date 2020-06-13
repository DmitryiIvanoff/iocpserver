#pragma once
#include <winsock2.h>
#include <memory>
#include <mutex>

constexpr auto MAX_BUFF_SIZE = 8192;

enum etIOOperation {
	AcceptClient,
	SendToClient,
	ReadFromClient,
	WriteToClient,
	PartialBuffer
};


struct CIOContext {
	CIOContext() = delete;
	CIOContext(SOCKET clientSocket) :nTotalBytes(0),
		nSentBytes(0),
		IOOperation(AcceptClient),
		m_dClientSocket(clientSocket),
		m_dMySQLSocket(INVALID_SOCKET)
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

	~CIOContext() {

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
	//��������� WSAOVERLAPPED ��� ����������� �������� � ��������
	WSAOVERLAPPED               overlapped;	
	//�����
	char                        buffer[MAX_BUFF_SIZE];
	//����������� �� ����� � ��� �����
	WSABUF                      wsabuf;
	//������� ���� ������� � ������
	int                         nTotalBytes;
	//������� ���� �������� � �����
	int                         nSentBytes;
	//��� ��������
	etIOOperation                IOOperation;
	//��� ���������� ������������� ������������������ �������: ��� ����������� ������ � ������� ����� ���������� �� ������ ������� win �� ����������� ��� ��������� ����� ����������
	//������ � ��� ����� � ������� ���������������, ��� ����� �������� � ���� ��� ������ ����� ������� ������� ������� ��� � ������������� ������, �.�.
	//������� ����� ��������� ������ � ��� ������� � ������� ��� �������� ������ � ����� - ��� �������� �������, �������� � ������� ����� ��������� 
	//�������� (�������� ������ � ������): 1,2,3,4,5. ��� ������� 3 �������� ����������� �������, ����� ��������� ��� � ������� ����� ���������� ��������� 
	//���������� � ��������� �������: 3,1,2,4,5. ������� ����� ��������� ������ � ������� �������, ����� ��������� ��� ������ 1 ������� ������, ��������������� ��� ������� 3, 
	//������ 2 - ��� 1 � �.�. ��� �������������� ������ ��������� �� ������ ������� ������� � ������� ����� ����������� ������ � ���� ���������� ����������� � ���������� �������.
	size_t sequenceNumber;
	
	SOCKET m_dClientSocket;
	
	SOCKET m_dMySQLSocket;
	//�� ������ ������� �� ��� ����� �� ��� ����������� ������ ���� �������� ����������� ������ � ���� ���������� ��� ������������� ������ ����� ������ �� ������������.
	std::mutex m_mClientSync;
};
typedef std::shared_ptr<CIOContext> IOContextPtr;

//����� �������� ������ ��������� � ������ ������� ��������������� � ������ ����������
//TODO: ������� ��� ���� ��������� � �������� ������ ��� ��������� � ���
struct CClientContext {
public:
	CClientContext() = delete;

	CClientContext(SOCKET clientSocket);
	~CClientContext();

	size_t m_dSessionId;

	IOContextPtr  m_pIOContext;
};

typedef std::shared_ptr<CClientContext> ClientContextPtr;

