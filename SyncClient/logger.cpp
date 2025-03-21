#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <atomic>

/* ---------------- Interface ------------------ */

enum LogLevel
{
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

class Logger
{
public:
    static Logger &getInstance();

    template <typename... Args>
    void info(const std::string &, Args...);

    template <typename... Args>
    void debug(const std::string &, Args...);

    template <typename... Args>
    void warn(const std::string &, Args...);

    template <typename... Args>
    void error(const std::string &, Args...);

private:
    const std::atomic<LogLevel> level;
    std::mutex logMutex;

    Logger() : level(INFO) {}; // Default log level is info

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

/* ---------------- Implementation ------------------ */

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

template <typename... Args>
void Logger::info(const std::string &format, Args... args)
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (this->level <= INFO)
        log(INFO, format_string(format, args...));
}

template <typename... Args>
void Logger::debug(const std::string &format, Args... args)
{
    if (this->level <= DEBUG)
        log(DEBUG, format_string(format, args...));
}

template <typename... Args>
void Logger::warn(const std::string &format, Args... args)
{
    if (this->level <= WARNING)
        log(WARNING, format_string(format, args...));
}

template <typename... Args>
void Logger::error(const std::string &format, Args... args)
{
    if (this->level <= ERROR)
        log(ERROR, format_string(format, args...));
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

// Format Helper (Base Case)
void Logger::format_helper(std::ostringstream &oss, const std::string &message)
{
    oss << message;
}
