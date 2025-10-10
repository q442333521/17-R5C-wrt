#include "logger.h"
#include <cstdarg>
#include <ctime>
#include <cstring>

static const char* level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char* level_to_string(Logger::Level level) {
    if (level == Logger::TRACE) {
        return level_strings[0];
    }
    switch (level) {
        case Logger::DEBUG: return level_strings[1];
        case Logger::INFO:  return level_strings[2];
        case Logger::WARN:  return level_strings[3];
        case Logger::ERROR: return level_strings[4];
        case Logger::FATAL: return level_strings[5];
        default: return "UNKNOWN";
    }
}

void Logger::log(Level level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (instance().use_syslog_) {
        // 输出到 syslog
        vsyslog(level, format, args);
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
        vsyslog(TRACE, format, args);
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
        vsyslog(DEBUG, format, args);
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
        vsyslog(INFO, format, args);
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
        vsyslog(WARN, format, args);
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
        vsyslog(ERROR, format, args);
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
        vsyslog(FATAL, format, args);
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
