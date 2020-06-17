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
	static LoggerPtr GetInstance();

	void Write(CBuffer* buffer);

	void Error(const std::string& error);

	~CLogger();

private:
	CLogger();

	const std::string GetCurrentDateTime();

private:
	static std::string LOG_FILE_NAME;
	static std::string ERROR_FILE_NAME;

	static LoggerPtr m_pLogger;

	PrserPtr m_pParser;

	std::ofstream m_sLogSream;
	std::ofstream m_sErrorSream;

	std::mutex m_mLogSync;
	std::mutex m_mErrorSync;
};