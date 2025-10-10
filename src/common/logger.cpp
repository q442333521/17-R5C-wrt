#include "logger.h"
#include <cstdarg>
#include <ctime>
#include <cstring>

static const char* level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char* level_to_string(Logger::Level level) {
    const int index = static_cast<int>(level);
    if (index >= 0 && index < 6) {
        return level_strings[index];
    }
    return "UNKNOWN";
}

void Logger::log(Level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        // 输出到 syslog
        vsyslog(Logger::to_syslog_priority(level), format, args);
    } else {
        // 输出到控制台
        // 获取当前时间
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        
        // 打印日志头
        fprintf(stderr, "[%s] [%s] [%s] ", 
                time_buf, 
                level_to_string(level),
                instance().name_.c_str());
        
        // 打印日志内容
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}

void Logger::trace(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        vsyslog(Logger::to_syslog_priority(Logger::TRACE), format, args);
    } else {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(stderr, "[%s] [TRACE] [%s] ", time_buf, instance().name_.c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}

void Logger::debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        vsyslog(Logger::to_syslog_priority(Logger::DEBUG), format, args);
    } else {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(stderr, "[%s] [DEBUG] [%s] ", time_buf, instance().name_.c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}

void Logger::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        vsyslog(Logger::to_syslog_priority(Logger::INFO), format, args);
    } else {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(stderr, "[%s] [INFO ] [%s] ", time_buf, instance().name_.c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}

void Logger::warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        vsyslog(Logger::to_syslog_priority(Logger::WARN), format, args);
    } else {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(stderr, "[%s] [WARN ] [%s] ", time_buf, instance().name_.c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}

void Logger::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        vsyslog(Logger::to_syslog_priority(Logger::ERROR), format, args);
    } else {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(stderr, "[%s] [ERROR] [%s] ", time_buf, instance().name_.c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}

void Logger::fatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        vsyslog(Logger::to_syslog_priority(Logger::FATAL), format, args);
    } else {
        time_t now = time(nullptr);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);
        fprintf(stderr, "[%s] [FATAL] [%s] ", time_buf, instance().name_.c_str());
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}
