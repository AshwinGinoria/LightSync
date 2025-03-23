#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <chrono>

Logger &Logger::getInstance()
{
    static Logger instance;
    return instance;
}

void Logger::log(LogLevel levelType, const std::string &message)
{
    std::lock_guard<std::mutex> lock(getInstance().logMutex);

    std::string levelStr;
    switch (levelType)
    {
    case DEBUG:
        levelStr = "[DEBUG] ";
        break;
    case INFO:
        levelStr = "[INFO] ";
        break;
    case WARNING:
        levelStr = "[WARNING] ";
        break;
    case ERROR:
        levelStr = "[ERROR] ";
        break;
    }

    std::cout << get_current_time() << " " << levelStr << message << std::endl;
}

// Get current time as a string
std::string Logger::get_current_time()
{
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_tm = *std::localtime(&now_time_t);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// Format Helper (Base Case)
void Logger::format_helper(std::ostringstream &oss, const std::string &message)
{
    oss << message;
}
