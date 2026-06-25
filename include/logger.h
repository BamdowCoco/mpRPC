#pragma once
#include <iostream>

#define DEBUG

#ifdef DEBUG
#define LOG(str)                                     \
    std::cout << __FILE__ << ":" << __LINE__ << ": " \
              << str                                 \
              << std::endl
#define DEBUG_CODE(code)              \
    do {                              \
        code;                         \
    } while (0) 
#else
    #define LOG(str) (void*)0
    #define DEBUG_CODE(code) (void*)0
#endif // DEBUG

// mprpc框架提供的日志模块

#include "lock_queue.h"
#include <string>

class Logger
{
public:
    enum Level
    {
        INFO,
        ERROR
    };
    static std::string levelToString(Level level);

    static Logger& getInstance();

    void setLogLevel(Level level);
    Level getLogLevel();
    
    // 外部接口-把日志写入lockQueue缓冲区中
    void log(std::string msg);

private:
    Logger();
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;

    Level m_logLevel;
    LockQueue<std::string> m_lockQue;
};

// 定义宏
#define LOG_WITH_FILE_LINE(level, msgformat, ...) \
    do { \
        Logger& logger = Logger::getInstance(); \
        logger.setLogLevel(Logger::level); \
        char buf[4096] = {0}; \
        snprintf(buf, sizeof(buf),"[%s] [%s:%d] " msgformat, \
                Logger::levelToString(Logger::level).c_str(), \
                __FILE__, __LINE__, ##__VA_ARGS__); \
        logger.log(buf); \
    } while(0)

#define LOG_INFO(msgformat, ...) LOG_WITH_FILE_LINE(INFO, msgformat, ##__VA_ARGS__)

#define LOG_ERROR(msgformat, ...) LOG_WITH_FILE_LINE(ERROR, msgformat, ##__VA_ARGS__)