#include <ctime>
#include <string>
#include <sstream>
#include <iostream>
#include <thread>
#include "CLogger.h"
#include "CParser.h"

std::string CLogger::LOG_FILE_NAME = "log.txt";
std::string CLogger::ERROR_FILE_NAME = "log.txt";
LoggerPtr CLogger::m_pLogger = nullptr;

CLogger::CLogger() {
	m_pParser.reset(new CParser());
	
	m_sLogSream.open(CLogger::LOG_FILE_NAME, std::fstream::out | std::fstream::app);

	m_sErrorSream.open(CLogger::ERROR_FILE_NAME, std::fstream::out | std::fstream::app);
}

LoggerPtr CLogger::GetInstance()
{
	if (!m_pLogger) {
		m_pLogger.reset(new CLogger());
	}
	return m_pLogger;
}

CLogger::~CLogger() {
	m_sLogSream.close();
	m_sErrorSream.close();

	while (!m_mLogSync.try_lock()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	m_mLogSync.unlock();

	while (!m_mErrorSync.try_lock()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	m_mErrorSync.unlock();

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

void CLogger::Error(const std::string& error) {
	std::lock_guard<std::mutex> lock(m_pLogger->m_mErrorSync);

	std::stringstream ss;

	ss << GetCurrentDateTime() << " [" << std::this_thread::get_id() << ":UND]" << error << "\n";

	if (m_pLogger->m_sErrorSream.is_open()) {
		m_pLogger->m_sErrorSream << ss.rdbuf();
	}
	else
	{
		std::cout << "Error log: file not opened" << std::endl;
	}
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

	if (m_pLogger->m_sLogSream.is_open()) {
		m_pLogger->m_sLogSream << ss.rdbuf();
	}
	else
	{
		Error("Error: file not opened");
	}
}