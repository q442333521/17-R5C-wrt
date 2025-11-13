#ifndef GATEWAY_BRIDGE_CONFIG_H
#define GATEWAY_BRIDGE_CONFIG_H

#include "types.h"
#include <json/json.h>
#include <memory>
#include <mutex>

namespace gateway {

/**
 * 配置管理器
 * 负责加载、保存和管理网关配置
 */
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    // 加载配置文件
    bool load_from_file(const std::string& filename);

    // 保存配置文件
    bool save_to_file(const std::string& filename) const;

    // 从JSON加载
    bool load_from_json(const Json::Value& json);

    // 转换为JSON
    Json::Value to_json() const;

    // 获取配置
    const GatewayConfig& get_config() const { return config_; }
    GatewayConfig& get_mutable_config() { return config_; }

    // 更新配置
    void set_config(const GatewayConfig& config);

    // 添加/删除/更新映射规则
    bool add_rule(const MappingRule& rule);
    bool remove_rule(const std::string& rule_id);
    bool update_rule(const std::string& rule_id, const MappingRule& rule);
    bool get_rule(const std::string& rule_id, MappingRule& rule) const;

    // 获取所有规则
    std::vector<MappingRule> get_all_rules() const;

    // 验证配置
    bool validate() const;

private:
    GatewayConfig config_;
    mutable std::mutex mutex_;

    // JSON解析辅助函数
    bool parse_gateway_section(const Json::Value& json);
    bool parse_modbus_rtu_section(const Json::Value& json);
    bool parse_modbus_tcp_section(const Json::Value& json);
    bool parse_s7_section(const Json::Value& json);
    bool parse_web_server_section(const Json::Value& json);
    bool parse_logging_section(const Json::Value& json);
    bool parse_mapping_rules(const Json::Value& json);

    // 解析单个映射规则
    bool parse_mapping_rule(const Json::Value& json, MappingRule& rule);
    bool parse_source_config(const Json::Value& json, ModbusRTUSource& source);
    bool parse_destination_config(const Json::Value& json, DestinationConfig& dest);
    bool parse_transform_rule(const Json::Value& json, TransformRule& transform);

    // JSON生成辅助函数
    Json::Value mapping_rule_to_json(const MappingRule& rule) const;
    Json::Value source_to_json(const ModbusRTUSource& source) const;
    Json::Value destination_to_json(const DestinationConfig& dest) const;
    Json::Value transform_to_json(const TransformRule& transform) const;
};

} // namespace gateway

#endif // GATEWAY_BRIDGE_CONFIG_H
