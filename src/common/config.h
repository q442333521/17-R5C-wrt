#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#include <string>
#include <json/json.h>
#include <mutex>

/**
 * Configuration Manager
 * 配置管理器 - 负责读取、保存和验证配置
 */
class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager inst;
        return inst;
    }
    
    // 加载配置文件
    bool load(const std::string& path = "/opt/gw/conf/config.json");
    
    // 保存配置文件（原子写入 + 备份）
    bool save(const std::string& path = "/opt/gw/conf/config.json");
    
    // 获取配置值
    Json::Value& get_config() { return config_; }
    const Json::Value& get_config() const { return config_; }
    
    // 获取特定配置项
    std::string get_string(const std::string& key, const std::string& default_val = "") const;
    int get_int(const std::string& key, int default_val = 0) const;
    bool get_bool(const std::string& key, bool default_val = false) const;
    
    // 设置配置项
    void set(const std::string& key, const Json::Value& value);
    
    // RS485 配置
    struct RS485Config {
        std::string device = "/dev/ttyUSB0";
        int baudrate = 19200;
        int poll_rate_ms = 20;
        int timeout_ms = 200;
        int retry_count = 3;
    };
    
    // Modbus TCP 配置
    struct ModbusConfig {
        bool enabled = true;
        std::string listen_ip = "0.0.0.0";
        int port = 502;
        int slave_id = 1;
    };
    
    // 网络配置
    struct NetworkConfig {
        std::string mode = "dhcp";  // dhcp 或 static
        std::string ip = "192.168.1.100";
        std::string netmask = "255.255.255.0";
        std::string gateway = "192.168.1.1";
    };
    
    // 获取结构化配置
    RS485Config get_rs485_config() const;
    ModbusConfig get_modbus_config() const;
    NetworkConfig get_network_config() const;
    
    // 恢复出厂设置
    void reset_to_default();
    
private:
    ConfigManager();
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    Json::Value config_;
    mutable std::mutex mutex_;
    
    // 生成默认配置
    Json::Value get_default_config() const;
};

#endif // GATEWAY_CONFIG_H
