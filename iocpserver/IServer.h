#pragma once
#include "CClientContext.h"

class IServer {
public:
	virtual ~IServer() {};

	HANDLE GetIOCP() const;

	void SetHelperIOCP(HANDLE compPort);

private://TODO: толку от интерфейса для закрытых методов нет, убрать все закрытые методы отсюда

	//virtual SOCKET CreateSocket(std::string port, bool isListenSocket) = 0;

	//virtual void UpdateCompletionPort(ClientContextPtr& context, SOCKET sdClient, SOCKET sdMySQL, etIOOperation operation) = 0;
	////
	//// bAddToList is FALSE for listening socket, and TRUE for connection sockets.
	//// As we maintain the context for listening socket in a global structure, we
	//// don't need to add it to the list.
	////

	////virtual void RemoveIOContext(ClientContextPtr lpPerSocketContext) = 0;//TODO: убрать т.к. юзается только в клиент сервере

	////virtual void AddIOContext(ClientContextPtr lpPerSocketContext) = 0;//TODO: убрать т.к. юзается только в клиент сервере

	//virtual bool RecvBuffer(SOCKET recvSocket, IOContextPtr data, size_t id) = 0;

	//virtual bool SendBuffer(SOCKET sendSocket, IOContextPtr data, size_t id) = 0;

};