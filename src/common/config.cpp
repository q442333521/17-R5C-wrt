#include "config.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>

namespace fs = std::filesystem;

ConfigManager::ConfigManager() {
    // 初始化为默认配置
    config_ = get_default_config();
}

bool ConfigManager::load(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查文件是否存在
    if (!fs::exists(path)) {
        std::cerr << "Config file not found: " << path << ", using defaults" << std::endl;
        config_ = get_default_config();
        return save(path); // 保存默认配置
    }
    
    // 读取文件
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "Failed to open config file: " << path << std::endl;
        return false;
    }
    
    // 解析 JSON
    Json::CharReaderBuilder builder;
    std::string errs;
    
    if (!Json::parseFromStream(builder, ifs, &config_, &errs)) {
        std::cerr << "Failed to parse config: " << errs << std::endl;
        config_ = get_default_config();
        return false;
    }
    
    std::cout << "Config loaded from: " << path << std::endl;
    return true;
}

bool ConfigManager::save(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 创建目录（如果不存在）
    fs::path file_path(path);
    fs::create_directories(file_path.parent_path());
    
    // 写入临时文件
    std::string temp_path = path + ".tmp";
    std::ofstream ofs(temp_path);
    if (!ofs.is_open()) {
        std::cerr << "Failed to create temp config file: " << temp_path << std::endl;
        return false;
    }
    
    // 格式化输出 JSON
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(config_, &ofs);
    ofs.close();
    
    // 原子重命名
    try {
        fs::rename(temp_path, path);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to rename config file: " << e.what() << std::endl;
        return false;
    }
    
    // 异步备份（简化版，不使用线程）
    std::string backup_path = path + ".backup";
    try {
        fs::copy_file(path, backup_path, fs::copy_options::overwrite_existing);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to backup config: " << e.what() << std::endl;
    }
    
    std::cout << "Config saved to: " << path << std::endl;
    return true;
}

std::string ConfigManager::get_string(const std::string& key, const std::string& default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 支持点号分隔的嵌套路径，如 "rs485.device"
    Json::Value current = config_;
    size_t pos = 0;
    size_t found;
    
    while ((found = key.find('.', pos)) != std::string::npos) {
        std::string part = key.substr(pos, found - pos);
        if (!current.isMember(part)) {
            return default_val;
        }
        current = current[part];
        pos = found + 1;
    }
    
    std::string last_part = key.substr(pos);
    if (!current.isMember(last_part)) {
        return default_val;
    }
    
    return current[last_part].asString();
}

int ConfigManager::get_int(const std::string& key, int default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Json::Value current = config_;
    size_t pos = 0;
    size_t found;
    
    while ((found = key.find('.', pos)) != std::string::npos) {
        std::string part = key.substr(pos, found - pos);
        if (!current.isMember(part)) {
            return default_val;
        }
        current = current[part];
        pos = found + 1;
    }
    
    std::string last_part = key.substr(pos);
    if (!current.isMember(last_part)) {
        return default_val;
    }
    
    return current[last_part].asInt();
}

bool ConfigManager::get_bool(const std::string& key, bool default_val) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Json::Value current = config_;
    size_t pos = 0;
    size_t found;
    
    while ((found = key.find('.', pos)) != std::string::npos) {
        std::string part = key.substr(pos, found - pos);
        if (!current.isMember(part)) {
            return default_val;
        }
        current = current[part];
        pos = found + 1;
    }
    
    std::string last_part = key.substr(pos);
    if (!current.isMember(last_part)) {
        return default_val;
    }
    
    return current[last_part].asBool();
}

void ConfigManager::set(const std::string& key, const Json::Value& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 支持点号分隔的嵌套路径
    Json::Value* current = &config_;
    size_t pos = 0;
    size_t found;
    
    while ((found = key.find('.', pos)) != std::string::npos) {
        std::string part = key.substr(pos, found - pos);
        if (!(*current).isMember(part)) {
            (*current)[part] = Json::Value(Json::objectValue);
        }
        current = &((*current)[part]);
        pos = found + 1;
    }
    
    std::string last_part = key.substr(pos);
    (*current)[last_part] = value;
}

ConfigManager::RS485Config ConfigManager::get_rs485_config() const {
    RS485Config cfg;
    cfg.device = get_string("rs485.device", "/dev/ttyUSB0");
    cfg.baudrate = get_int("rs485.baudrate", 19200);
    cfg.poll_rate_ms = get_int("rs485.poll_rate_ms", 20);
    cfg.timeout_ms = get_int("rs485.timeout_ms", 200);
    cfg.retry_count = get_int("rs485.retry_count", 3);
    cfg.simulate = get_bool("rs485.simulate", false);
    return cfg;
}

ConfigManager::ModbusConfig ConfigManager::get_modbus_config() const {
    ModbusConfig cfg;
    cfg.enabled = get_bool("protocol.modbus.enabled", true);
    cfg.listen_ip = get_string("protocol.modbus.listen_ip", "0.0.0.0");
    cfg.port = get_int("protocol.modbus.port", 502);
    cfg.slave_id = get_int("protocol.modbus.slave_id", 1);
    return cfg;
}

ConfigManager::NetworkConfig ConfigManager::get_network_config() const {
    NetworkConfig cfg;
    cfg.mode = get_string("network.eth0.mode", "dhcp");
    cfg.ip = get_string("network.eth0.ip", "192.168.1.100");
    cfg.netmask = get_string("network.eth0.netmask", "255.255.255.0");
    cfg.gateway = get_string("network.eth0.gateway", "192.168.1.1");
    return cfg;
}

void ConfigManager::reset_to_default() {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = get_default_config();
    std::cout << "Config reset to default" << std::endl;
}

Json::Value ConfigManager::get_default_config() const {
    Json::Value root;
    
    root["version"] = "2.0";
    
    // 网络配置
    root["network"]["eth0"]["mode"] = "dhcp";
    root["network"]["eth0"]["ip"] = "192.168.1.100";
    root["network"]["eth0"]["netmask"] = "255.255.255.0";
    root["network"]["eth0"]["gateway"] = "192.168.1.1";
    
    // RS485 配置
    root["rs485"]["device"] = "/dev/ttyUSB0";
    root["rs485"]["baudrate"] = 19200;
    root["rs485"]["poll_rate_ms"] = 20;
    root["rs485"]["timeout_ms"] = 200;
    root["rs485"]["retry_count"] = 3;
    root["rs485"]["simulate"] = false;
    
    // 协议配置
    root["protocol"]["active"] = "modbus";
    
    // Modbus TCP
    root["protocol"]["modbus"]["enabled"] = true;
    root["protocol"]["modbus"]["listen_ip"] = "0.0.0.0";
    root["protocol"]["modbus"]["port"] = 502;
    root["protocol"]["modbus"]["slave_id"] = 1;
    
    // S7 (可选)
    root["protocol"]["s7"]["enabled"] = false;
    root["protocol"]["s7"]["plc_ip"] = "192.168.1.10";
    root["protocol"]["s7"]["rack"] = 0;
    root["protocol"]["s7"]["slot"] = 1;
    root["protocol"]["s7"]["db_number"] = 10;
    root["protocol"]["s7"]["update_interval_ms"] = 50;
    
    // OPC UA (可选)
    root["protocol"]["opcua"]["enabled"] = false;
    root["protocol"]["opcua"]["server_url"] = "opc.tcp://192.168.1.20:4840";
    root["protocol"]["opcua"]["security_mode"] = "None";
    root["protocol"]["opcua"]["username"] = "";
    root["protocol"]["opcua"]["password"] = "";
    
    // 系统配置
    root["system"]["log_level"] = "INFO";
    root["system"]["watchdog_timeout_s"] = 30;
    root["system"]["data_retention_days"] = 7;
    
    return root;
}
