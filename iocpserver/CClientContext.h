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
	CIOContext(SOCKET clientSocket, SOCKET mySQLSocket, etIOOperation operationId) :nTotalBytes(0),
		nSentBytes(0),
		IOOperation(operationId),
		m_dClientSocket(clientSocket),
		m_dMySQLSocket(mySQLSocket)
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
	///структура WSAOVERLAPPED для асинхронных операций с ссокетом
	WSAOVERLAPPED               overlapped;	
	///буфер
	char                        buffer[MAX_BUFF_SIZE];
	///указательна на буфер и его длина
	WSABUF                      wsabuf;
	///сколько байт считали с сокета
	int                         nTotalBytes;
	///сколько байт записали в сокет
	int                         nSentBytes;
	///код операции
	etIOOperation                IOOperation;
	//номер последовательности (при вызове асинхронной заприсия в сокет: WSAsend из разных потоков win не гарантирует что скоет будет заполнен в порядке вызовов этой функции, это
	//может привести к непредсказуемому порядку буфера если WSAsend будет вызвн одновременно из разных потоков. Эта переменная характеризует последовательность пакетов считаных с сокета)
	size_t sequenceNumber;
	//сокет клиента ассоциированный с портом завершения, который вернула функция WSAAccept в рутине Start
	//дальнейшее взаимодействие с клиентом происходит через этот сокет.
	SOCKET m_dClientSocket;
	//
	SOCKET m_dMySQLSocket;

	std::mutex m_mClientSync;
};
typedef std::shared_ptr<CIOContext> IOContextPtr;

//Класс хранения данных связанных с каждым сокетом ассоциированным с портом завершения
//TODO: сделать все поля закрытыми и добавить методы для обращения к ним
struct CClientContext {
public:
	CClientContext() = delete;

	CClientContext(SOCKET clientSocket, SOCKET mySQLSocket, etIOOperation operationId);
	~CClientContext();

	size_t m_dSessionId;

	IOContextPtr  m_pIOContext;
};

typedef std::shared_ptr<CClientContext> ClientContextPtr;

