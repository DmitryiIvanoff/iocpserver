#pragma once
#include "CClientContext.h"

class IServer {
public:
	virtual ~IServer() {};

	HANDLE getIOCP();

private:

	virtual SOCKET createSocket(std::string port, bool isListenSocket) = 0;

	virtual void updateCompletionPort(ClientContextPtr& context, SOCKET sdClient, SOCKET sdMySQL, etIOOperation operation) = 0;
	//
	// bAddToList is FALSE for listening socket, and TRUE for connection sockets.
	// As we maintain the context for listening socket in a global structure, we
	// don't need to add it to the list.
	//

	virtual void removeIOContext(ClientContextPtr lpPerSocketContext) = 0;

	virtual void addIOContext(ClientContextPtr lpPerSocketContext) = 0;

	virtual bool recvBuffer(SOCKET recvSocket, IOContextPtr data, size_t id) = 0;

	virtual bool sendBuffer(SOCKET sendSocket, IOContextPtr data, size_t id) = 0;

};