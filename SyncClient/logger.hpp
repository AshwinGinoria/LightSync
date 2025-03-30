#pragma once

#include <string>
#include <vector>
#include <iomanip>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iostream>
#include <fstream>

#define LOGGER Logger::getInstance()

enum LogLevel
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};

class Logger
{
public:
    static Logger &getInstance();

    void setLogLevel(LogLevel);
    void setLogFile(const std::string);

    template <typename... Args>
    void info(const std::string &, Args...);

    template <typename... Args>
    void debug(const std::string &, Args...);

    template <typename... Args>
    void warn(const std::string &, Args...);

    template <typename... Args>
    void error(const std::string &, Args...);

private:
    static std::atomic<LogLevel> level;
    static std::mutex logMutex;
    static std::ostream output_stream*;

    Logger() = default;

    static void log(LogLevel, const std::string &);

    static std::string get_current_time();

    template <typename... Args>
    static std::string format_string(const std::string &, Args...);

    template <typename T, typename... Args>
    static void format_helper(std::ostringstream &, const std::string &, const std::vector<T> &, Args...);

    template <typename T, typename... Args>
    static void format_helper(std::ostringstream &, const std::string &, T, Args...);

    static void format_helper(std::ostringstream &, const std::string &);

    // Delete copy constructor and assignment operator to prevent copying
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
};

template <typename... Args>
void Logger::info(const std::string &format, Args... args)
{
    if (this->level <= LOG_INFO)
        log(LOG_INFO, format_string(format, args...));
}

template <typename... Args>
void Logger::debug(const std::string &format, Args... args)
{
    if (this->level <= LOG_DEBUG)
        log(LOG_DEBUG, format_string(format, args...));
}

template <typename... Args>
void Logger::warn(const std::string &format, Args... args)
{
    if (this->level <= LOG_WARNING)
        log(LOG_WARNING, format_string(format, args...));
}

template <typename... Args>
void Logger::error(const std::string &format, Args... args)
{
    if (this->level <= LOG_ERROR)
        log(LOG_ERROR, format_string(format, args...));
}

template <typename... Args>
std::string Logger::format_string(const std::string &format, Args... args)
{
    std::ostringstream oss;
    format_helper(oss, format, args...);
    return oss.str();
}

template <typename T, typename... Args>
void Logger::format_helper(std::ostringstream &oss, const std::string &format, T value, Args... args)
{
    size_t pos = format.find("{}");
    if (pos != std::string::npos)
    {
        oss << format.substr(0, pos) << value;
        format_helper(oss, format.substr(pos + 2), args...);
    }
    else
    {
        throw std::invalid_argument("Too many args for format string");
    }
}

// Format Helper handle vectors separately
template <typename T, typename... Args>
void Logger::format_helper(std::ostringstream &oss, const std::string &format, const std::vector<T> &value, Args... args)
{
    size_t pos = format.find("{}");
    if (pos != std::string::npos)
    {
        oss << format.substr(0, pos) << "[";
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (i != 0)
                oss << ", ";
            oss << static_cast<int>(value[i]);
        }
        oss << "]";
        format_helper(oss, format.substr(pos + 2));
    }
    else
    {
        throw std::invalid_argument("Too many args for format string");
    }
}
