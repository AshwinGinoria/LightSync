#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <chrono>

void Logger::setLogLevel(LogLevel new_level)
{
    level = new_level;
}

void Logger::setLogFile(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (output_stream != nullptr && output_stream != &std::cout)
    {
        (*output_stream).flush();
        output_stream->close();
        delete output_stream;
    }
    output_stream = new std::ofstream(filename, std::ios::app);
}

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
    case LOG_DEBUG:
        levelStr = "[DEBUG] ";
        break;
    case LOG_INFO:
        levelStr = "[INFO] ";
        break;
    case LOG_WARNING:
        levelStr = "[WARNING] ";
        break;
    case LOG_ERROR:
        levelStr = "[ERROR] ";
        break;
    }

    (*output_stream) << get_current_time() << " " << levelStr << message << std::endl;
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
