#ifndef CUSTOM_CLogger_H
#define CUSTOM_CLogger_H

#include <fstream>
#include <iostream>
#include <cstdarg>
#include <string>

#define LOGGER CLogger::GetLogger()
#define LOG_DEBUG CLogger::LogLevel::DEBUG
#define LOG_INFO CLogger::LogLevel::INFO
#define LOG_WARNING CLogger::LogLevel::WARNING
#define LOG_ERROR CLogger::LogLevel::ERR
/**
*   Singleton Logger Class.
*/
class CLogger
{
public:
    /**
    *   Logs a message
    *   @param sMessage message to be logged.
    */
   	enum LogLevel { DEBUG, INFO, WARNING, ERR };

    void Log(LogLevel level, const std::string& sMessage);
    /**
    *   Variable Length Logger function
    *   @param format string for the message to be logged.
    */
    void Log(LogLevel level, const char * format, ...);
    /**
    *   Funtion to create the instance of logger class.
    *   @return singleton object of Clogger class..
    */
    static CLogger* GetLogger();

private:
    /**
    *    Default constructor for the Logger class.
    */
    CLogger();
    /**
    *   copy constructor for the Logger class.
    */
    CLogger(const CLogger&){};             // copy constructor is private
    /**
    *   Minimum Log Level.
    **/
    static const LogLevel m_MinLevel;
    /**
    *   Log folder path.
    **/
    static const std::string m_sDirectory;
    /**
    *   Log file name.
    **/
    static const std::string m_sFileName;
    /**
    /**
    *   Log file path.
    **/
    static const std::string m_sFilePath;
    /**
    *   Singleton logger class object pointer.
    **/
    static CLogger* m_pThis;
    /**
    *   Log file stream object.
    **/
    static std::ofstream m_Logfile;
};

#endif