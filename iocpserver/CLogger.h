#pragma once
#include <memory>
#include <fstream>
#include <mutex>

class CParser;
typedef std::shared_ptr<CParser> PrserPtr;

class CLogger;
typedef std::shared_ptr<CLogger> LoggerPtr;

class CLogger
{
public:
	//инстанцирует логгер если он еще не создан.
	static LoggerPtr GetLogger();
	void Write(const char* log, const size_t length);
	~CLogger();

private:
	CLogger();
	const std::string GetCurrentDateTime();
	size_t getMessageEndPosition(const char* log, const size_t length);

private:
	static std::string FILE_NAME;
	static LoggerPtr m_pLogger;

	PrserPtr m_pParser;
	std::ofstream m_stream;
	std::mutex m_mLogSync;
	
};