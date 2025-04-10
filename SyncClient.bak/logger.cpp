#include "logger.hpp"
#include <chrono>
#include <sstream>

// Singleton instance of logger
Logger &Logger::getInstance() {
    static Logger instance;
    return instance;
}

// Defaul Constructor
Logger::Logger() {
    setFormat("{timestamp} [{level}] {message}");
    setLogLevel(LOG_DEBUG);
}

// Update the log level that nees to be put in the sink
void Logger::setLogLevel(LogLevel level) {
    minLevel = level;
}

// Add an output sink
void Logger::addSink(std::unique_ptr<LogSink> sink) {
    sinks.push_back(std::move(sink));
}

// Set the format for log lines
void Logger::setFormat(const std::string formatString) {
    std::vector<std::function<std::string(const LogContext &)>> parts;

    size_t pos = 0;
    while (pos < formatString.size()) {
        if (pos + 1 < formatString.size() && formatString[pos] == '{' &&
            formatString[pos + 1] != '{') {
            size_t end = formatString.find('}', pos);
            if (end == std::string::npos) {
                std::string literal = formatString.substr(pos);
                parts.push_back(
                    [literal](const LogContext &) { return literal; });
                break;
            }

            std::string placeholder =
                formatString.substr(pos + 1, end - pos - 1);
            if (placeholder == "timestamp") {
                parts.push_back(
                    [](const LogContext &ctx) { return ctx.timestamp; });
            } else if (placeholder == "level") {
                parts.push_back([this](const LogContext &ctx) {
                    return levelToString(ctx.level);
                });
            } else if (placeholder == "message") {
                parts.push_back(
                    [](const LogContext &ctx) { return ctx.message; });
            } else {
                std::string unknown = "{" + placeholder + "}";
                parts.push_back(
                    [unknown](const LogContext &) { return unknown; });
            }
            pos = end + 1;
        } else {
            size_t next = formatString.find('{', pos);
            if (next == std::string::npos) next = formatString.size();
            std::string literal = formatString.substr(pos, next - pos);
            parts.push_back([literal](const LogContext &) { return literal; });
            pos = next;
        }
    }

    formatter = [parts = std::move(parts)](const LogContext &context) {
        std::string result;
        for (const auto &part : parts) {
            result += part(context);
        }
        return result;
    };
}

// Log Entry
void Logger::log(LogLevel level, const std::string &message) {
    std::lock_guard<std::mutex> lock(getInstance().logMutex);
    LogContext logContext{getCurrentTime(), level, message};

    std::string formatted_log = formatter(logContext);

    for (const auto &sink : sinks) sink->write(formatted_log);
}

std::string Logger::levelToString(const LogLevel level) {
    switch (level) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO: return "INFO";
    case LOG_WARNING: return "WARNING";
    case LOG_ERROR: return "ERROR";
    }

    return "UNKNOWN";
}

// Get current time as a string
std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_tm = *std::localtime(&now_time_t);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
