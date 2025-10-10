/**
 * @file logger.h
 * @brief 日志工具 - 简单易用的日志系统
 * 
 * 提供多级别日志输出功能，支持：
 * - 控制台输出（开发调试）
 * - syslog 输出（生产环境）
 * - 时间戳自动添加
 * - 格式化字符串（printf 风格）
 * 
 * 使用示例：
 * ```cpp
 * Logger::init("my_app", false);  // 初始化，输出到控制台
 * LOG_INFO("程序启动，版本: %s", VERSION);
 * LOG_ERROR("文件打开失败: %s", filename);
 * ```
 * 
 * @author Gateway Project
 * @date 2025-10-10
 */

#ifndef GATEWAY_LOGGER_H
#define GATEWAY_LOGGER_H

#include <string>
#include <syslog.h>
#include <cstdio>

/**
 * @class Logger
 * @brief 日志管理器（单例模式）
 * 
 * 提供统一的日志输出接口，支持多种日志级别和输出方式。
 * 
 * 日志级别（从低到高）：
 * - TRACE: 跟踪信息（最详细）
 * - DEBUG: 调试信息
 * - INFO:  一般信息
 * - WARN:  警告信息
 * - ERROR: 错误信息
 * - FATAL: 致命错误
 * 
 * 输出方式：
 * - 控制台: 适合开发调试，带时间戳和颜色
 * - syslog: 适合生产环境，集成到系统日志
 */
class Logger {
public:
    /**
     * @enum Level
     * @brief 日志级别枚举
     */
    enum Level {
        TRACE = LOG_DEBUG,      ///< 跟踪级别（最详细）
        DEBUG = LOG_DEBUG,      ///< 调试级别
        INFO = LOG_INFO,        ///< 信息级别
        WARN = LOG_WARNING,     ///< 警告级别
        ERROR = LOG_ERR,        ///< 错误级别
        FATAL = LOG_CRIT        ///< 致命错误级别
    };
    
    /**
     * @brief 初始化日志系统
     * 
     * 必须在使用日志功能前调用此函数进行初始化。
     * 
     * @param name 程序名称（会出现在日志中）
     * @param use_syslog 是否使用 syslog
     *                   - true: 输出到系统日志 (/var/log/syslog)
     *                   - false: 输出到标准错误流 (stderr)
     * 
     * @note 整个程序只需初始化一次
     * 
     * @example
     * Logger::init("rs485d", false);  // 开发环境，输出到控制台
     * Logger::init("rs485d", true);   // 生产环境，输出到 syslog
     */
    static void init(const std::string& name, bool use_syslog = false) {
        instance().name_ = name;
        instance().use_syslog_ = use_syslog;
        
        if (use_syslog) {
            // 打开 syslog 连接
            // LOG_PID: 在日志中包含进程 ID
            // LOG_CONS: 如果无法写入 syslog，则写入控制台
            // LOG_LOCAL0: 使用 local0 设施（可在 /etc/rsyslog.conf 中配置）
            openlog(name.c_str(), LOG_PID | LOG_CONS, LOG_LOCAL0);
        }
    }
    
    /**
     * @brief 通用日志输出函数
     * 
     * @param level 日志级别
     * @param format printf 风格的格式化字符串
     * @param ... 可变参数
     * 
     * @note 通常不直接调用此函数，而是使用便捷宏 LOG_INFO、LOG_ERROR 等
     */
    static void log(Level level, const char* format, ...) __attribute__((format(printf, 2, 3)));
    
    /// @brief 输出 TRACE 级别日志
    static void trace(const char* format, ...) __attribute__((format(printf, 1, 2)));
    
    /// @brief 输出 DEBUG 级别日志
    static void debug(const char* format, ...) __attribute__((format(printf, 1, 2)));
    
    /// @brief 输出 INFO 级别日志
    static void info(const char* format, ...)  __attribute__((format(printf, 1, 2)));
    
    /// @brief 输出 WARN 级别日志
    static void warn(const char* format, ...)  __attribute__((format(printf, 1, 2)));
    
    /// @brief 输出 ERROR 级别日志
    static void error(const char* format, ...) __attribute__((format(printf, 1, 2)));
    
    /// @brief 输出 FATAL 级别日志（致命错误）
    static void fatal(const char* format, ...) __attribute__((format(printf, 1, 2)));
    
private:
    /// @brief 私有构造函数（单例模式）
    Logger() = default;
    
    /**
     * @brief 析构函数
     * 
     * 如果使用了 syslog，会自动关闭连接
     */
    ~Logger() {
        if (use_syslog_) {
            closelog();
        }
    }
    
    /**
     * @brief 获取单例实例
     * 
     * @return Logger& 日志管理器单例引用
     */
    static Logger& instance() {
        static Logger inst;
        return inst;
    }
    
    std::string name_;      ///< 程序名称
    bool use_syslog_ = false; ///< 是否使用 syslog
};

// ============================================================================
// 便捷宏定义
// ============================================================================

/**
 * @def LOG_TRACE(format, ...)
 * @brief 输出 TRACE 级别日志（跟踪信息）
 * 
 * 用于最详细的调试信息，如循环中的变量值。
 * 
 * @example
 * LOG_TRACE("进入函数: %s, 参数: %d", __func__, param);
 */
#define LOG_TRACE(...) Logger::trace(__VA_ARGS__)

/**
 * @def LOG_DEBUG(format, ...)
 * @brief 输出 DEBUG 级别日志（调试信息）
 * 
 * 用于调试信息，如中间变量、状态转换等。
 * 
 * @example
 * LOG_DEBUG("收到数据: %d 字节", n);
 */
#define LOG_DEBUG(...) Logger::debug(__VA_ARGS__)

/**
 * @def LOG_INFO(format, ...)
 * @brief 输出 INFO 级别日志（一般信息）
 * 
 * 用于程序运行的关键节点信息。
 * 
 * @example
 * LOG_INFO("服务启动成功，监听端口: %d", port);
 */
#define LOG_INFO(...)  Logger::info(__VA_ARGS__)

/**
 * @def LOG_WARN(format, ...)
 * @brief 输出 WARN 级别日志（警告信息）
 * 
 * 用于非致命的异常情况，程序可以继续运行。
 * 
 * @example
 * LOG_WARN("配置文件不存在，使用默认配置");
 */
#define LOG_WARN(...)  Logger::warn(__VA_ARGS__)

/**
 * @def LOG_ERROR(format, ...)
 * @brief 输出 ERROR 级别日志（错误信息）
 * 
 * 用于错误情况，可能导致功能失效。
 * 
 * @example
 * LOG_ERROR("文件打开失败: %s", strerror(errno));
 */
#define LOG_ERROR(...) Logger::error(__VA_ARGS__)

/**
 * @def LOG_FATAL(format, ...)
 * @brief 输出 FATAL 级别日志（致命错误）
 * 
 * 用于致命错误，通常会导致程序退出。
 * 
 * @example
 * LOG_FATAL("内存分配失败，无法继续运行");
 */
#define LOG_FATAL(...) Logger::fatal(__VA_ARGS__)

#endif // GATEWAY_LOGGER_H
