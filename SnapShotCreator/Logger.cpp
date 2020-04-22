#include "Logger.h"
#include "utils.h"

const std::string   CLogger::m_sFileName = "SnapShotCreator_"+CurrentDateTime("%F")+".log";
CLogger*            CLogger::m_pThis = NULL;
std::ofstream       CLogger::m_Logfile;
const char*         fmt = "%Y-%m-%d %H:%M:%S";

CLogger::CLogger() {}
CLogger* CLogger::GetLogger(){
    if (m_pThis == NULL){
        m_pThis = new CLogger();
        m_Logfile.open(m_sFileName.c_str(), std::ios_base::app | std::ios_base::out);
    }
    return m_pThis;
}

void CLogger::Log(LogLevel level, const char * format, ...)
{
    char* sMessage = NULL;
    int nLength = 0;
    va_list args;
    va_start(args, format);
    //  Return the number of characters in the string referenced the list of arguments.
    // _vscprintf doesn't count terminating '\0' (that's why +1)
    nLength = _vscprintf(format, args) + 1;
    sMessage = new char[nLength];
    vsprintf_s(sMessage, nLength, format, args);
    //vsprintf(sMessage, format, args);
    std::string logLevelName =
        level == DEBUG   ?   " [DEBUG  ] " :
        level == INFO    ?   " [INFO   ] " :
        level == WARNING ?   " [WARNING] " :
        /*level == ERROR*/   " [ERROR  ] ";
    m_Logfile << CurrentDateTime(fmt) << logLevelName;
    m_Logfile << sMessage << "\n";
    va_end(args);

    delete [] sMessage;
}

void CLogger::Log(LogLevel level, const std::string& sMessage)
{
    std::string logLevelName =
        level == DEBUG   ?   " [DEBUG  ] " :
        level == INFO    ?   " [INFO   ] " :
        level == WARNING ?   " [WARNING] " :
        /*level == ERROR*/   " [ERROR  ] ";
    m_Logfile <<  CurrentDateTime(fmt) << logLevelName;
    m_Logfile << sMessage << "\n";
}

CLogger& CLogger::operator<<(const std::string& sMessage)
{
    m_Logfile << "\n" << CurrentDateTime(fmt) << ":\t";
    m_Logfile << sMessage << "\n";
    return *this;
}
