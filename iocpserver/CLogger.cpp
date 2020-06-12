#include <ctime>
#include <string>
#include <sstream>
#include <thread>
#include "CLogger.h"
#include "CParser.h"

std::string CLogger::FILE_NAME = "log.txt";
LoggerPtr CLogger::m_pLogger = nullptr;

CLogger::CLogger() {
	m_pParser.reset(new CParser());
	m_stream.open(CLogger::FILE_NAME, std::fstream::out | std::fstream::app | std::fstream::trunc);
}

LoggerPtr CLogger::GetLogger()
{
	if (!m_pLogger) {
		m_pLogger.reset(new CLogger());
	}
	return m_pLogger;
}

CLogger::~CLogger() {
	m_stream.close();
}

size_t CLogger::getMessageEndPosition(const char* log, const size_t length) {

	char* oendOfMySQLMessage = const_cast<char*>(log + length);
	size_t pos = length;

	while (pos > 0) {

		if (*oendOfMySQLMessage-- != MYSQL_EOF_MARKER) {
			return pos;
		}
		pos--;
	}

	return 0;
}
const std::string CLogger::GetCurrentDateTime() {
	time_t     now = time(0);
	struct tm  tstruct;
	char       buf[80];
	localtime_s(&tstruct, &now);

	strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);

	return buf;
}


void CLogger::Write(const char* log, const size_t length) {

	std::lock_guard<std::mutex> lock(m_pLogger->m_mLogSync);

	int packetLength = PACKET_LEN(log);
	char* packetMessage = PACKET_MSG(log);
	char cmd = MYSQL_COMMAND(log);

	size_t messageLength = getMessageEndPosition(packetMessage, packetLength > length ? length : packetLength);

	std::string outputString(packetMessage, messageLength);
	
	std::stringstream ss;

	ss << GetCurrentDateTime() << " [" << std::this_thread::get_id() << "] " + outputString + "\n";

	m_pLogger->m_stream.write(ss.str().c_str(), ss.str().length());
	
}