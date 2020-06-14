#pragma once
#include <fstream>
#include <memory>
#include <mutex>
#include "CBuffer.h"


class CParser;
typedef std::shared_ptr<CParser> PrserPtr;

class CLogger;
typedef std::shared_ptr<CLogger> LoggerPtr;

class CLogger
{
public:
	//инстанцирует логгер если он еще не создан.
	static LoggerPtr GetLogger();
	void Write(BufferPtr buffer);
	~CLogger();

private:
	CLogger();
	const std::string GetCurrentDateTime();

private:
	static std::string FILE_NAME;
	static LoggerPtr m_pLogger;

	PrserPtr m_pParser;
	std::ofstream m_stream;
	std::mutex m_mLogSync;
	
};