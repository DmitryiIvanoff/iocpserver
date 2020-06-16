#include <ctime>
#include <string>
#include <sstream>
#include <iostream>
#include <thread>
#include "CLogger.h"
#include "CParser.h"

std::string CLogger::FILE_NAME = "log.txt";
LoggerPtr CLogger::m_pLogger = nullptr;

CLogger::CLogger() {
	m_pParser.reset(new CParser());
	m_stream.open(CLogger::FILE_NAME, std::fstream::out | std::fstream::app);
}

LoggerPtr CLogger::GetInstance()
{
	if (!m_pLogger) {
		m_pLogger.reset(new CLogger());
	}
	return m_pLogger;
}

CLogger::~CLogger() {
	m_stream.close();
	m_pParser.reset();
	m_pLogger.reset();
}

const std::string CLogger::GetCurrentDateTime() {
	time_t     now = time(0);
	struct tm  tstruct;
	char       buf[80];
	localtime_s(&tstruct, &now);

	strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);

	return buf;
}


void CLogger::Write(BufferPtr buffer) {

	std::lock_guard<std::mutex> lock(m_pLogger->m_mLogSync);
	
	const char* log = buffer->buffer;
	size_t length = buffer->nTotalBytes;
	enIOOperation opCode = buffer->IOOperation;

	//Парсим только SQL-запросы от клиента
	if (opCode != ReadFromClient)
	{
		return;
	}

	std::string sLog(log, length);
	m_pParser->Parse(sLog);
	
	std::stringstream ss;

	ss << GetCurrentDateTime() << " [" << std::this_thread::get_id() << ":" << buffer->m_dSessionId << "]" << sLog << "\n";

	if (m_pLogger->m_stream.is_open()) {
		m_pLogger->m_stream << ss.rdbuf();
	}
	else
	{
		std::cout << "Error: file not opened" << std::endl;
	}
	
	
}