/**
 * @file config.h
 * @brief 配置管理系统
 * 
 * 提供统一的配置文件管理功能，支持：
 * - JSON 格式配置文件
 * - 嵌套配置项访问（点号分隔）
 * - 原子写入（防止断电损坏）
 * - 自动备份
 * - 类型安全的配置读取
 * 
 * 配置文件结构示例：
 * ```json
 * {
 *   "network": {
 *     "eth0": {
 *       "mode": "dhcp"
 *     }
 *   },
 *   "rs485": {
 *     "device": "/dev/ttyUSB0",
 *     "baudrate": 19200
 *   }
 * }
 * ```
 * 
 * 使用示例：
 * ```cpp
 * ConfigManager& config = ConfigManager::instance();
 * config.load("/opt/gw/conf/config.json");
 * 
 * // 读取配置
 * std::string device = config.get_string("rs485.device");
 * int baudrate = config.get_int("rs485.baudrate");
 * 
 * // 修改配置
 * config.set("rs485.baudrate", 38400);
 * config.save();
 * ```
 * 
 * @author Gateway Project
 * @date 2025-10-10
 */

#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#include <string>
#include <json/json.h>
#include <mutex>

/**
 * @class ConfigManager
 * @brief 配置管理器（单例模式）
 * 
 * 负责配置文件的加载、保存、查询和修改。
 * 
 * 设计特点：
 * - 单例模式：全局唯一实例
 * - 线程安全：使用互斥锁保护
 * - 原子写入：临时文件 + rename，防止断电损坏
 * - 自动备份：保存时自动创建 .backup 文件
 * - 嵌套访问：支持 "a.b.c" 形式的路径
 * 
 * 文件格式：
 * - 主配置文件：config.json
 * - 备份文件：config.json.backup
 * - 临时文件：config.json.tmp（写入时使用）
 */
class ConfigManager {
public:
    /**
     * @brief 获取单例实例
     * 
     * @return ConfigManager& 配置管理器引用
     * 
     * @note 线程安全，使用静态局部变量实现
     */
    static ConfigManager& instance() {
        static ConfigManager inst;
        return inst;
    }
    
    /**
     * @brief 加载配置文件
     * 
     * 工作流程：
     * 1. 检查文件是否存在
     * 2. 如果不存在，生成默认配置并保存
     * 3. 读取文件内容
     * 4. 解析 JSON
     * 5. 如果解析失败，使用默认配置
     * 
     * @param path 配置文件路径（默认 /opt/gw/conf/config.json）
     * @return bool true=成功, false=失败
     * 
     * @note 失败时会自动使用默认配置，不会导致程序崩溃
     * 
     * @example
     * ConfigManager& config = ConfigManager::instance();
     * if (!config.load("/opt/gw/conf/config.json")) {
     *     LOG_WARN("配置加载失败，使用默认配置");
     * }
     */
    bool load(const std::string& path = "/opt/gw/conf/config.json");
    
    /**
     * @brief 保存配置文件
     * 
     * 原子写入流程（防止断电损坏）：
     * 1. 将配置写入临时文件（config.json.tmp）
     * 2. 调用 sync() 强制落盘
     * 3. 使用 rename() 原子替换原文件
     * 4. 异步创建备份文件（config.json.backup）
     * 
     * @param path 配置文件路径（默认 /opt/gw/conf/config.json）
     * @return bool true=成功, false=失败
     * 
     * @note rename() 是原子操作，即使断电也不会损坏文件
     * 
     * @example
     * config.set("rs485.baudrate", 38400);
     * if (config.save()) {
     *     LOG_INFO("配置保存成功");
     * }
     */
    bool save(const std::string& path = "/opt/gw/conf/config.json");
    
    /**
     * @brief 获取完整配置对象
     * 
     * @return Json::Value& 配置的根节点引用
     * 
     * @note 返回的是引用，可以直接修改
     * @warning 修改后需要调用 save() 才能持久化
     */
    Json::Value& get_config() { return config_; }
    
    /**
     * @brief 获取完整配置对象（只读）
     * 
     * @return const Json::Value& 配置的根节点常量引用
     */
    const Json::Value& get_config() const { return config_; }
    
    /**
     * @brief 获取字符串配置项
     * 
     * 支持嵌套路径，如 "network.eth0.mode"
     * 
     * @param key 配置键（支持点号分隔的路径）
     * @param default_val 默认值（键不存在时返回）
     * @return std::string 配置值
     * 
     * @example
     * std::string mode = config.get_string("network.eth0.mode", "dhcp");
     * std::string device = config.get_string("rs485.device", "/dev/ttyUSB0");
     */
    std::string get_string(const std::string& key, const std::string& default_val = "") const;
    
    /**
     * @brief 获取整数配置项
     * 
     * @param key 配置键
     * @param default_val 默认值
     * @return int 配置值
     * 
     * @example
     * int baudrate = config.get_int("rs485.baudrate", 19200);
     * int port = config.get_int("protocol.modbus.port", 502);
     */
    int get_int(const std::string& key, int default_val = 0) const;
    
    /**
     * @brief 获取布尔配置项
     * 
     * @param key 配置键
     * @param default_val 默认值
     * @return bool 配置值
     * 
     * @example
     * bool enabled = config.get_bool("protocol.modbus.enabled", true);
     */
    bool get_bool(const std::string& key, bool default_val = false) const;
    
    /**
     * @brief 设置配置项
     * 
     * 支持嵌套路径，如果中间路径不存在会自动创建。
     * 
     * @param key 配置键（支持点号分隔的路径）
     * @param value 配置值（Json::Value 类型）
     * 
     * @note 设置后需要调用 save() 才能持久化
     * 
     * @example
     * config.set("rs485.baudrate", 38400);
     * config.set("protocol.modbus.enabled", true);
     * config.save();
     */
    void set(const std::string& key, const Json::Value& value);
    
    /**
     * @struct RS485Config
     * @brief RS-485 配置结构体
     * 
     * 将 JSON 配置转换为结构化的 C++ 对象，类型安全。
     */
    struct RS485Config {
        std::string device = "/dev/ttyUSB0";  ///< 串口设备路径
        int baudrate = 19200;                  ///< 波特率
        int poll_rate_ms = 20;                 ///< 采样周期（毫秒）
        int timeout_ms = 200;                  ///< 超时时间（毫秒）
        int retry_count = 3;                   ///< 重试次数
        bool simulate = false;                 ///< 是否启用模拟模式
    };
    
    /**
     * @struct ModbusConfig
     * @brief Modbus TCP 配置结构体
     */
    struct ModbusConfig {
        bool enabled = true;                   ///< 是否启用
        std::string listen_ip = "0.0.0.0";    ///< 监听地址
        int port = 502;                        ///< 监听端口
        int slave_id = 1;                      ///< 从站 ID
    };
    
    /**
     * @struct NetworkConfig
     * @brief 网络配置结构体
     */
    struct NetworkConfig {
        std::string mode = "dhcp";             ///< 模式：dhcp 或 static
        std::string ip = "192.168.1.100";     ///< IP 地址（static 模式）
        std::string netmask = "255.255.255.0"; ///< 子网掩码
        std::string gateway = "192.168.1.1";   ///< 默认网关
    };
    
    /**
     * @brief 获取 RS-485 配置
     * 
     * @return RS485Config 结构化的配置对象
     * 
     * @example
     * auto rs485_cfg = config.get_rs485_config();
     * printf("设备: %s, 波特率: %d\n", 
     *        rs485_cfg.device.c_str(), rs485_cfg.baudrate);
     */
    RS485Config get_rs485_config() const;
    
    /**
     * @brief 获取 Modbus TCP 配置
     * 
     * @return ModbusConfig 结构化的配置对象
     */
    ModbusConfig get_modbus_config() const;
    
    /**
     * @brief 获取网络配置
     * 
     * @return NetworkConfig 结构化的配置对象
     */
    NetworkConfig get_network_config() const;
    
    /**
     * @brief 恢复出厂设置
     * 
     * 将所有配置项重置为默认值。
     * 
     * @note 重置后需要调用 save() 才能持久化
     * 
     * @example
     * config.reset_to_default();
     * config.save();
     * LOG_INFO("配置已恢复出厂设置");
     */
    void reset_to_default();
    
private:
    /**
     * @brief 私有构造函数（单例模式）
     * 
     * 初始化为默认配置
     */
    ConfigManager();
    
    /// @brief 禁用析构函数（单例模式）
    ~ConfigManager() = default;
    
    /// @brief 禁用拷贝构造函数
    ConfigManager(const ConfigManager&) = delete;
    
    /// @brief 禁用赋值操作符
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    Json::Value config_;        ///< 配置的根节点
    mutable std::mutex mutex_;  ///< 互斥锁（保护并发访问）
    
    /**
     * @brief 生成默认配置
     * 
     * 包含所有模块的默认配置项。
     * 
     * @return Json::Value 默认配置对象
     */
    Json::Value get_default_config() const;
};

#endif // GATEWAY_CONFIG_H
