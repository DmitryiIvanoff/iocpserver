#pragma once
#include <winsock2.h>
#include <memory>
#include <mutex>

constexpr auto MAX_BUFF_SIZE = 8192;

enum enIOOperation {
	AcceptClient,
	SendToClient,
	ReadFromClient,
	WriteToClient,
	ReadFromIOCP
};


struct CBuffer {

	CBuffer() = delete;

	CBuffer(SOCKET clientSocket);

	~CBuffer();

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
	enIOOperation                IOOperation;
	/*��� ���������� ������������� ������������������ �������: ��� ����������� ������ � ������� ����� ���������� �� ������ ������� win �� ����������� ��� ��������� ����� ����������
	 ������ � ��� ����� � ������� ���������������, ��� ����� �������� � ���� ��� ������ ����� ������� ������� ������� ��� � ������������� ������, �.�.
	 ������� ����� ��������� ������ � ��� ������� � ������� ��� �������� ������ � ����� - ��� �������� �������, �������� � ������� ����� ��������� 
	 �������� (�������� ������ � ������): 1,2,3,4,5. ��� ������� 3 �������� ����������� �������, ����� ��������� ��� � ������� ����� ���������� ��������� 
	 ���������� � ��������� �������: 3,1,2,4,5. ������� ����� ��������� ������ � ������� �������, ����� ��������� ��� ������ 1 ������� ������, ��������������� ��� ������� 3, 
	 ������ 2 - ��� 1 � �.�. ��� �������������� ������ ��������� �� ������ ������� ������� � ������� ����� ����������� ������ � ���� ���������� ����������� � ������� ������
	 ������� ���������� �� ����.*/
	size_t sequenceNumber;
	
	SOCKET m_dClientSocket;
	
	SOCKET m_dMySQLSocket;
	//�� ������ ������� �� ��� ����� �� ��� ����������� ������ ���� �������� ����������� ������ � ���� ���������� ��� ������������� ������ ����� ������ �� ������������.
	std::mutex m_mClientSync;
	
	size_t m_dSessionId;
};
typedef std::shared_ptr<CBuffer> BufferPtr;
