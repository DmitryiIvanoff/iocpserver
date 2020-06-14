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
	CBuffer(SOCKET clientSocket) :nTotalBytes(0),
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

	~CBuffer() {

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
	//структура WSAOVERLAPPED для асинхронных операций с ссокетом
	WSAOVERLAPPED               overlapped;	
	//буфер
	char                        buffer[MAX_BUFF_SIZE];
	//указательна на буфер и его длина
	WSABUF                      wsabuf;
	//сколько байт считали с сокета
	int                         nTotalBytes;
	//сколько байт записали в сокет
	int                         nSentBytes;
	//код операции
	enIOOperation                IOOperation;
	/*Эта переменная характеризует последовательность пакетов: при асинхронной записи в очередь порта завершения из разных потоков win не гарантирует что сообщение будет отправлено
	 именно в тот сокет в который предназначалось, это может привести к тому что данные будут посланы другому клиенту или к порче данных, т.к.
	 клиенты будут считывать данные в том порядке в котором они записали данные в сокет - тут работает очередь, допустим в очередь стали несколько 
	 клиентов (записали данные в сокеты): 1,2,3,4,5. Для клиента 3 операция выполняется быстрее, тогда получится что в очередь порта завершения запишутся 
	 результаты в следующем порядке: 3,1,2,4,5. Клиенты будут считывать данные в порядке очереди, тогда получится что клиент 1 считает данные, предназначенные для клиента 3, 
	 клиент 2 - для 1 и т.д. Для предотвращения такого поведения мы должны создать очередь и следить чтобы асинхронная запись в порт завершения происходиля в порядке чтения
	 пакетов отклиентов из него.*/
	size_t sequenceNumber;
	
	SOCKET m_dClientSocket;
	
	SOCKET m_dMySQLSocket;
	//Мы должны следить за тем чтобы за раз выполнялась только одна операция асинхронной записи в порт завершения для определенного сокета чтобы данные не перемешались.
	std::mutex m_mClientSync;
	
	size_t m_dSessionId;
};
typedef std::shared_ptr<CBuffer> BufferPtr;

//Класс хранения данных связанных с каждым сокетом ассоциированным с портом завершения
struct CClientContext {
public:
	CClientContext() = delete;

	CClientContext(SOCKET clientSocket);
	~CClientContext();

	BufferPtr  m_pIOContext;
};

typedef std::shared_ptr<CClientContext> ClientContextPtr;

