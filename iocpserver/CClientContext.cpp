#include "CClientContext.h"

CClientContext::CClientContext(SOCKET clientSocket) {

	m_pIOContext.reset(new CBuffer(clientSocket));
}


CClientContext::~CClientContext() {

	m_pIOContext.reset();
}

