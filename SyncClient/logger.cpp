#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <iomanip>

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
    static Logger* getInstance()
    {
        static Logger instance;
        return &instance;
    }

    template <typename... Args>
    void info(const std::string &format, Args... args)
    {
        if (this->level <= INFO)
            log("INFO", format_string(format, args...));
    }

    template <typename... Args>
    void debug(const std::string &format, Args... args)
    {
        if (this->level <= DEBUG)
            log("DEBUG", format_string(format, args...));
    }

    template <typename... Args>
    void warning(const std::string &format, Args... args)
    {
        if (this->level <= WARNING)
            log("WARNING", format_string(format, args...));
    }

    template <typename... Args>
    void error(const std::string &format, Args... args)
    {
        if (this->level <= ERROR)
            log("ERROR", format_string(format, args...));
    }

private:
    const LogLevel level;

    Logger() : level(DEBUG) {}

    static void log(const char *level, const std::string &message)
    {
        std::cout << format_string("{} [{}]: {}", get_current_time(), level, message) << std::endl;
    }

    static std::string get_current_time()
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%dT%H:%M:%S");
        return ss.str();
    }

    template <typename... Args>
    static std::string format_string(const std::string &format, Args... args)
    {
        std::ostringstream oss;
        format_helper(oss, format, args...);
        return oss.str();
    }

    template <typename T, typename... Args>
    static void format_helper(std::ostringstream &oss, const std::string &format, T value, Args... args)
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

    template <typename T, typename... Args>
    static void format_helper(std::ostringstream &oss, const std::string &format, const std::vector<T> &value, Args... args)
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

    static void format_helper(std::ostringstream &oss, const std::string &format)
    {
        oss << format;
    }

    // Delete copy constructor and assignment operator to prevent copying
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
};