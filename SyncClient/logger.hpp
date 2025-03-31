#pragma once

#include <atomic>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#define LOGGER Logger::getInstance()

enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERROR };

class LogSink {
  public:
    virtual ~LogSink() = default;
    virtual void write(const std::string &message) = 0;
};

class ConsoleSink : public LogSink {
    void write(const std::string &message) override {
        std::cout << message << std::endl;
    }
};

class FileSink : public LogSink {
  public:
    // Constructor takes a file path and opens the file
    explicit FileSink(const std::string &filepath) : file(filepath) {
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open log file: " + filepath);
        }
    }

    // Destructor ensures the file is closed
    ~FileSink() override {
        if (file.is_open()) {
            file.close();
        }
    }

    // Write message to the file
    void write(const std::string &message) override {
        if (file.is_open()) {
            file << message << std::endl;
        }
    }

  private:
    std::ofstream file; // File stream for writing
};

class Logger {
  public:
    // Singleton instance of Logger
    static Logger &getInstance();

    // Delete copy constructor and assignment operator to prevent copying
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // Set the minimum log level for logging
    // This will filter out messages below the specified level
    void setLogLevel(LogLevel);

    // Add a log sink to the logger
    void addSink(LogSink &sink);

    // Set logging format
    void setFormat(const std::string);

    // Log Info
    template <typename... Args> void info(const std::string &, Args...);

    // Log Debug
    template <typename... Args> void debug(const std::string &, Args...);

    // Log Warning
    template <typename... Args> void warn(const std::string &, Args...);

    // Log Error
    template <typename... Args> void error(const std::string &, Args...);

  private:
    std::atomic<LogLevel> minLevel;          // Current log level
    std::vector<LogSink> sinks;              // List of log sinks
    std::mutex logMutex;                     // Mutex for thread safety
    static const int maxMessageLength = 120; // Max message length

    struct LogContext {
        std::string timestamp;
        LogLevel level;
        std::string message;
    };

    // default constructor
    Logger() = default;

    // base log function
    void log(LogLevel, const std::string &);
    // formats lon entry as per given template
    std::function<std::string(const LogContext &)> formatter;

    // helper: convert LogLevel to string
    static std::string levelToString(const LogLevel);
    // helper: get current time
    static std::string getCurrentTime();
};

template <typename... Args>
void Logger::info(const std::string &message, Args... args) {
    if (minLevel <= LOG_INFO) log(LOG_INFO, std::format(message, args...));
}

template <typename... Args>
void Logger::debug(const std::string &message, Args... args) {
    if (minLevel <= LOG_DEBUG) log(LOG_DEBUG, std::format(message, args...));
}

template <typename... Args>
void Logger::warn(const std::string &message, Args... args) {
    if (minLevel <= LOG_WARNING)
        log(LOG_WARNING, std::format(message, args...));
}

template <typename... Args>
void Logger::error(const std::string &message, Args... args) {
    if (minLevel <= LOG_ERROR) log(LOG_ERROR, std::format(message, args...));
}
