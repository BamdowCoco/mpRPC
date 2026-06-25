#include "logger.h"
#include <thread>
#include <ctime>
#include <fstream>
#include <iostream>
#include <cstdlib>

std::string Logger::levelToString(Level level)
{
    static constexpr std::string_view names[] = {
        "INFO", "ERROR"
    };
    int idx = static_cast<int>(level);
    if(idx<0 || idx>static_cast<int>(std::size(names))) {
        return "UNKNOWN";
    }
    return std::string(names[idx]);
}

Logger::Logger()
{
    m_logLevel = INFO;
    // 启动写日志线程
    std::thread writeLogTask([this]() {
        while (true) {
            // 获取当前日期 从队列取日志 写入相应的日志文件中
            // 2026-6-12.log

            // 转换为tm结构体
            std::time_t now = time(nullptr);
            std::tm now_tm;
            localtime_r(&now, &now_tm);

            char filePath[128];
            snprintf(filePath, sizeof(filePath), "%d-%d-%d.log", now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday);

            char timeBuf[16]; // 2+1+2+1+2 = 8
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", now_tm.tm_hour, now_tm.tm_min, now_tm.tm_sec);

            // 以末尾追加的模式 打开文件
            std::ofstream file(filePath, std::ios::app);
            if (!file) {
                std::cerr << "failed to create file!" << std::endl;
                exit(EXIT_FAILURE);
            }

            // 取出当前所有日志
            std::queue<std::string> logs;
            m_lockQue.drain(logs);

            if(logs.empty()) {
                // 当前无日志 等待一下
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            while(!logs.empty()) {
                file << '[' << timeBuf << "] "
                    //  << '[' << levelToString(m_logLevel) << "] "
                     << logs.front() << std::endl;
                logs.pop();
            }

            file.close();
        }
    });
    // 分离线程
    writeLogTask.detach();
}

Logger& Logger::getInstance()
{
    static Logger logger;
    return logger;
}

void Logger::setLogLevel(Level level)
{
    m_logLevel = level;
}

Logger::Level Logger::getLogLevel()
{
    return m_logLevel;
}

// 外部接口-把日志写入lockQueue缓冲区中
void Logger::log(std::string msg)
{
    m_lockQue.push(msg);
    LOG(msg);
}
