#include "CClientContext.h"

CClientContext::CClientContext(SOCKET clientSocket) {

	m_pIOContext.reset(new CIOContext(clientSocket));
}


CClientContext::~CClientContext() {

	m_pIOContext.reset();
}

