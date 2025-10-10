#ifndef GATEWAY_LOGGER_H
#define GATEWAY_LOGGER_H

#include <string>
#include <syslog.h>
#include <cstdio>

/**
 * Simple Logger
 * 简单日志工具，支持 syslog 和控制台输出
 */
class Logger {
public:
    enum Level {
        TRACE = LOG_DEBUG,
        DEBUG = LOG_DEBUG,
        INFO = LOG_INFO,
        WARN = LOG_WARNING,
        ERROR = LOG_ERR,
        FATAL = LOG_CRIT
    };
    
    static void init(const std::string& name, bool use_syslog = false) {
        instance().name_ = name;
        instance().use_syslog_ = use_syslog;
        
        if (use_syslog) {
            openlog(name.c_str(), LOG_PID | LOG_CONS, LOG_LOCAL0);
        }
    }
    
    static void log(Level level, const char* format, ...) __attribute__((format(printf, 2, 3)));
    
    static void trace(const char* format, ...) __attribute__((format(printf, 1, 2)));
    static void debug(const char* format, ...) __attribute__((format(printf, 1, 2)));
    static void info(const char* format, ...)  __attribute__((format(printf, 1, 2)));
    static void warn(const char* format, ...)  __attribute__((format(printf, 1, 2)));
    static void error(const char* format, ...) __attribute__((format(printf, 1, 2)));
    static void fatal(const char* format, ...) __attribute__((format(printf, 1, 2)));
    
private:
    Logger() = default;
    ~Logger() {
        if (use_syslog_) {
            closelog();
        }
    }
    
    static Logger& instance() {
        static Logger inst;
        return inst;
    }
    
    std::string name_;
    bool use_syslog_ = false;
};

// 便捷宏
#define LOG_TRACE(...) Logger::trace(__VA_ARGS__)
#define LOG_DEBUG(...) Logger::debug(__VA_ARGS__)
#define LOG_INFO(...)  Logger::info(__VA_ARGS__)
#define LOG_WARN(...)  Logger::warn(__VA_ARGS__)
#define LOG_ERROR(...) Logger::error(__VA_ARGS__)
#define LOG_FATAL(...) Logger::fatal(__VA_ARGS__)

#endif // GATEWAY_LOGGER_H
