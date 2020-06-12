#include "CClientContext.h"

CClientContext::CClientContext(SOCKET clientSocket, SOCKET mySQLSocket, etIOOperation operationId) {

	m_pIOContext.reset(new CIOContext(clientSocket, mySQLSocket, operationId));
}


CClientContext::~CClientContext() {

	m_pIOContext.reset();
}

