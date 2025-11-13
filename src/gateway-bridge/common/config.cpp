#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace gateway {

ConfigManager::ConfigManager() {
    // 设置默认配置
    config_.gateway.name = "Protocol-Gateway-R5S";
    config_.gateway.description = "Industrial Protocol Gateway";
    config_.gateway.mode = "modbus_tcp";

    config_.modbus_rtu.device = "/dev/ttyUSB0";
    config_.modbus_rtu.baudrate = 9600;
    config_.modbus_rtu.parity = 'N';
    config_.modbus_rtu.data_bits = 8;
    config_.modbus_rtu.stop_bits = 1;
    config_.modbus_rtu.timeout_ms = 1000;
    config_.modbus_rtu.retry_count = 3;

    config_.modbus_tcp.enabled = true;
    config_.modbus_tcp.listen_ip = "0.0.0.0";
    config_.modbus_tcp.port = 502;
    config_.modbus_tcp.max_connections = 32;

    config_.s7.enabled = false;
    config_.s7.plc_ip = "192.168.1.10";
    config_.s7.rack = 0;
    config_.s7.slot = 1;
    config_.s7.connection_timeout_ms = 2000;

    config_.web_server.enabled = true;
    config_.web_server.port = 8080;
    config_.web_server.auth_enabled = true;
    config_.web_server.username = "admin";
    config_.web_server.password_hash = "";

    config_.logging.level = "INFO";
    config_.logging.file = "/var/log/gateway-bridge.log";
    config_.logging.max_size_mb = 10;
    config_.logging.max_files = 5;
}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::load_from_file(const std::string& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << "Failed to open config file: " << filename << std::endl;
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;

    if (!Json::parseFromStream(builder, ifs, &root, &errs)) {
        std::cerr << "Failed to parse JSON: " << errs << std::endl;
        return false;
    }

    return load_from_json(root);
}

bool ConfigManager::save_to_file(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mutex_);

    Json::Value root = to_json();

    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &ofs);
    ofs << std::endl;

    return true;
}

bool ConfigManager::load_from_json(const Json::Value& json) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        if (json.isMember("gateway")) {
            parse_gateway_section(json["gateway"]);
        }

        if (json.isMember("modbus_rtu")) {
            parse_modbus_rtu_section(json["modbus_rtu"]);
        }

        if (json.isMember("modbus_tcp")) {
            parse_modbus_tcp_section(json["modbus_tcp"]);
        }

        if (json.isMember("s7")) {
            parse_s7_section(json["s7"]);
        }

        if (json.isMember("web_server")) {
            parse_web_server_section(json["web_server"]);
        }

        if (json.isMember("logging")) {
            parse_logging_section(json["logging"]);
        }

        if (json.isMember("mapping_rules")) {
            parse_mapping_rules(json["mapping_rules"]);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during config parsing: " << e.what() << std::endl;
        return false;
    }
}

Json::Value ConfigManager::to_json() const {
    Json::Value root;

    root["version"] = "1.0";

    // Gateway section
    root["gateway"]["name"] = config_.gateway.name;
    root["gateway"]["description"] = config_.gateway.description;
    root["gateway"]["mode"] = config_.gateway.mode;

    // Modbus RTU section
    root["modbus_rtu"]["device"] = config_.modbus_rtu.device;
    root["modbus_rtu"]["baudrate"] = config_.modbus_rtu.baudrate;
    root["modbus_rtu"]["parity"] = std::string(1, config_.modbus_rtu.parity);
    root["modbus_rtu"]["data_bits"] = config_.modbus_rtu.data_bits;
    root["modbus_rtu"]["stop_bits"] = config_.modbus_rtu.stop_bits;
    root["modbus_rtu"]["timeout_ms"] = config_.modbus_rtu.timeout_ms;
    root["modbus_rtu"]["retry_count"] = config_.modbus_rtu.retry_count;

    // Modbus TCP section
    root["modbus_tcp"]["enabled"] = config_.modbus_tcp.enabled;
    root["modbus_tcp"]["listen_ip"] = config_.modbus_tcp.listen_ip;
    root["modbus_tcp"]["port"] = config_.modbus_tcp.port;
    root["modbus_tcp"]["max_connections"] = config_.modbus_tcp.max_connections;

    // S7 section
    root["s7"]["enabled"] = config_.s7.enabled;
    root["s7"]["plc_ip"] = config_.s7.plc_ip;
    root["s7"]["rack"] = config_.s7.rack;
    root["s7"]["slot"] = config_.s7.slot;
    root["s7"]["connection_timeout_ms"] = config_.s7.connection_timeout_ms;

    // Web server section
    root["web_server"]["enabled"] = config_.web_server.enabled;
    root["web_server"]["port"] = config_.web_server.port;
    root["web_server"]["auth_enabled"] = config_.web_server.auth_enabled;
    root["web_server"]["username"] = config_.web_server.username;
    root["web_server"]["password_hash"] = config_.web_server.password_hash;

    // Logging section
    root["logging"]["level"] = config_.logging.level;
    root["logging"]["file"] = config_.logging.file;
    root["logging"]["max_size_mb"] = config_.logging.max_size_mb;
    root["logging"]["max_files"] = config_.logging.max_files;

    // Mapping rules
    Json::Value rules_array(Json::arrayValue);
    for (const auto& rule : config_.mapping_rules) {
        rules_array.append(mapping_rule_to_json(rule));
    }
    root["mapping_rules"] = rules_array;

    return root;
}

void ConfigManager::set_config(const GatewayConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

bool ConfigManager::add_rule(const MappingRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查ID是否已存在
    for (const auto& existing_rule : config_.mapping_rules) {
        if (existing_rule.rule_id == rule.rule_id) {
            return false;  // ID已存在
        }
    }

    config_.mapping_rules.push_back(rule);
    return true;
}

bool ConfigManager::remove_rule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::remove_if(config_.mapping_rules.begin(),
                             config_.mapping_rules.end(),
                             [&rule_id](const MappingRule& r) {
                                 return r.rule_id == rule_id;
                             });

    if (it != config_.mapping_rules.end()) {
        config_.mapping_rules.erase(it, config_.mapping_rules.end());
        return true;
    }

    return false;
}

bool ConfigManager::update_rule(const std::string& rule_id, const MappingRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& existing_rule : config_.mapping_rules) {
        if (existing_rule.rule_id == rule_id) {
            existing_rule = rule;
            return true;
        }
    }

    return false;
}

bool ConfigManager::get_rule(const std::string& rule_id, MappingRule& rule) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& existing_rule : config_.mapping_rules) {
        if (existing_rule.rule_id == rule_id) {
            rule = existing_rule;
            return true;
        }
    }

    return false;
}

std::vector<MappingRule> ConfigManager::get_all_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_.mapping_rules;
}

bool ConfigManager::validate() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 验证Modbus RTU配置
    if (config_.modbus_rtu.device.empty()) {
        std::cerr << "Modbus RTU device is empty" << std::endl;
        return false;
    }

    if (config_.modbus_rtu.baudrate <= 0) {
        std::cerr << "Invalid baudrate: " << config_.modbus_rtu.baudrate << std::endl;
        return false;
    }

    // 验证端口
    if (config_.modbus_tcp.enabled && (config_.modbus_tcp.port <= 0 || config_.modbus_tcp.port > 65535)) {
        std::cerr << "Invalid Modbus TCP port: " << config_.modbus_tcp.port << std::endl;
        return false;
    }

    if (config_.web_server.enabled && (config_.web_server.port <= 0 || config_.web_server.port > 65535)) {
        std::cerr << "Invalid web server port: " << config_.web_server.port << std::endl;
        return false;
    }

    // 验证映射规则
    for (const auto& rule : config_.mapping_rules) {
        if (rule.rule_id.empty()) {
            std::cerr << "Empty rule ID" << std::endl;
            return false;
        }
        if (rule.source.poll_interval_ms <= 0) {
            std::cerr << "Invalid poll interval for rule: " << rule.rule_id << std::endl;
            return false;
        }
    }

    return true;
}

// 私有辅助函数实现
bool ConfigManager::parse_gateway_section(const Json::Value& json) {
    if (json.isMember("name")) config_.gateway.name = json["name"].asString();
    if (json.isMember("description")) config_.gateway.description = json["description"].asString();
    if (json.isMember("mode")) config_.gateway.mode = json["mode"].asString();
    return true;
}

bool ConfigManager::parse_modbus_rtu_section(const Json::Value& json) {
    if (json.isMember("device")) config_.modbus_rtu.device = json["device"].asString();
    if (json.isMember("baudrate")) config_.modbus_rtu.baudrate = json["baudrate"].asInt();
    if (json.isMember("parity")) {
        std::string parity = json["parity"].asString();
        config_.modbus_rtu.parity = parity.empty() ? 'N' : parity[0];
    }
    if (json.isMember("data_bits")) config_.modbus_rtu.data_bits = json["data_bits"].asInt();
    if (json.isMember("stop_bits")) config_.modbus_rtu.stop_bits = json["stop_bits"].asInt();
    if (json.isMember("timeout_ms")) config_.modbus_rtu.timeout_ms = json["timeout_ms"].asInt();
    if (json.isMember("retry_count")) config_.modbus_rtu.retry_count = json["retry_count"].asInt();
    return true;
}

bool ConfigManager::parse_modbus_tcp_section(const Json::Value& json) {
    if (json.isMember("enabled")) config_.modbus_tcp.enabled = json["enabled"].asBool();
    if (json.isMember("listen_ip")) config_.modbus_tcp.listen_ip = json["listen_ip"].asString();
    if (json.isMember("port")) config_.modbus_tcp.port = json["port"].asInt();
    if (json.isMember("max_connections")) config_.modbus_tcp.max_connections = json["max_connections"].asInt();
    return true;
}

bool ConfigManager::parse_s7_section(const Json::Value& json) {
    if (json.isMember("enabled")) config_.s7.enabled = json["enabled"].asBool();
    if (json.isMember("plc_ip")) config_.s7.plc_ip = json["plc_ip"].asString();
    if (json.isMember("rack")) config_.s7.rack = json["rack"].asInt();
    if (json.isMember("slot")) config_.s7.slot = json["slot"].asInt();
    if (json.isMember("connection_timeout_ms")) config_.s7.connection_timeout_ms = json["connection_timeout_ms"].asInt();
    return true;
}

bool ConfigManager::parse_web_server_section(const Json::Value& json) {
    if (json.isMember("enabled")) config_.web_server.enabled = json["enabled"].asBool();
    if (json.isMember("port")) config_.web_server.port = json["port"].asInt();
    if (json.isMember("auth_enabled")) config_.web_server.auth_enabled = json["auth_enabled"].asBool();
    if (json.isMember("username")) config_.web_server.username = json["username"].asString();
    if (json.isMember("password_hash")) config_.web_server.password_hash = json["password_hash"].asString();
    return true;
}

bool ConfigManager::parse_logging_section(const Json::Value& json) {
    if (json.isMember("level")) config_.logging.level = json["level"].asString();
    if (json.isMember("file")) config_.logging.file = json["file"].asString();
    if (json.isMember("max_size_mb")) config_.logging.max_size_mb = json["max_size_mb"].asInt();
    if (json.isMember("max_files")) config_.logging.max_files = json["max_files"].asInt();
    return true;
}

bool ConfigManager::parse_mapping_rules(const Json::Value& json) {
    if (!json.isArray()) {
        return false;
    }

    config_.mapping_rules.clear();

    for (const auto& rule_json : json) {
        MappingRule rule;
        if (parse_mapping_rule(rule_json, rule)) {
            config_.mapping_rules.push_back(rule);
        }
    }

    return true;
}

bool ConfigManager::parse_mapping_rule(const Json::Value& json, MappingRule& rule) {
    if (json.isMember("rule_id")) rule.rule_id = json["rule_id"].asString();
    if (json.isMember("description")) rule.description = json["description"].asString();
    if (json.isMember("enabled")) rule.enabled = json["enabled"].asBool();
    else rule.enabled = true;

    if (json.isMember("source")) {
        parse_source_config(json["source"], rule.source);
    }

    if (json.isMember("destination")) {
        parse_destination_config(json["destination"], rule.destination);
    }

    if (json.isMember("transform")) {
        parse_transform_rule(json["transform"], rule.transform);
    }

    return true;
}

bool ConfigManager::parse_source_config(const Json::Value& json, ModbusRTUSource& source) {
    if (json.isMember("slave_id")) source.slave_id = json["slave_id"].asInt();
    if (json.isMember("function_code")) source.function_code = json["function_code"].asInt();
    if (json.isMember("start_address")) source.start_address = json["start_address"].asInt();
    if (json.isMember("register_count")) source.register_count = json["register_count"].asInt();

    if (json.isMember("data_type")) {
        source.data_type = string_to_data_type(json["data_type"].asString());
    }

    if (json.isMember("byte_order")) {
        source.byte_order = string_to_byte_order(json["byte_order"].asString());
    }

    if (json.isMember("poll_interval_ms")) source.poll_interval_ms = json["poll_interval_ms"].asInt();
    else source.poll_interval_ms = 100;  // 默认100ms

    if (json.isMember("timeout_ms")) source.timeout_ms = json["timeout_ms"].asInt();
    else source.timeout_ms = 1000;

    if (json.isMember("retry_count")) source.retry_count = json["retry_count"].asInt();
    else source.retry_count = 3;

    return true;
}

bool ConfigManager::parse_destination_config(const Json::Value& json, DestinationConfig& dest) {
    if (!json.isMember("protocol")) {
        return false;
    }

    std::string protocol_str = json["protocol"].asString();
    if (protocol_str == "modbus_tcp") {
        dest.protocol = ProtocolType::MODBUS_TCP;

        if (json.isMember("slave_id")) dest.modbus_tcp.slave_id = json["slave_id"].asInt();
        if (json.isMember("function_code")) dest.modbus_tcp.function_code = json["function_code"].asInt();
        if (json.isMember("start_address")) dest.modbus_tcp.start_address = json["start_address"].asInt();
        if (json.isMember("data_type")) {
            dest.modbus_tcp.data_type = string_to_data_type(json["data_type"].asString());
        }
        if (json.isMember("byte_order")) {
            dest.modbus_tcp.byte_order = string_to_byte_order(json["byte_order"].asString());
        }
    } else if (protocol_str == "s7") {
        dest.protocol = ProtocolType::S7;

        if (json.isMember("db_number")) dest.s7.db_number = json["db_number"].asInt();
        if (json.isMember("start_byte")) dest.s7.start_byte = json["start_byte"].asInt();
        if (json.isMember("bit_offset")) dest.s7.bit_offset = json["bit_offset"].asInt();
        else dest.s7.bit_offset = 0;

        if (json.isMember("data_type")) {
            dest.s7.data_type = string_to_data_type(json["data_type"].asString());
        }
        if (json.isMember("byte_order")) {
            dest.s7.byte_order = string_to_byte_order(json["byte_order"].asString());
        }
    }

    return true;
}

bool ConfigManager::parse_transform_rule(const Json::Value& json, TransformRule& transform) {
    if (json.isMember("operation")) {
        std::string op_str = json["operation"].asString();
        if (op_str == "none") transform.operation = TransformOperation::NONE;
        else if (op_str == "scale") transform.operation = TransformOperation::SCALE;
        else if (op_str == "expression") transform.operation = TransformOperation::EXPRESSION;
        else if (op_str == "lookup") transform.operation = TransformOperation::LOOKUP;
        else transform.operation = TransformOperation::NONE;
    } else {
        transform.operation = TransformOperation::NONE;
    }

    if (json.isMember("scale")) transform.scale = json["scale"].asDouble();
    else transform.scale = 1.0;

    if (json.isMember("offset")) transform.offset = json["offset"].asDouble();
    else transform.offset = 0.0;

    if (json.isMember("expression")) transform.expression = json["expression"].asString();

    if (json.isMember("min_value")) transform.min_value = json["min_value"].asDouble();
    if (json.isMember("max_value")) transform.max_value = json["max_value"].asDouble();
    if (json.isMember("clamp_enabled")) transform.clamp_enabled = json["clamp_enabled"].asBool();
    else transform.clamp_enabled = false;

    return true;
}

Json::Value ConfigManager::mapping_rule_to_json(const MappingRule& rule) const {
    Json::Value json;

    json["rule_id"] = rule.rule_id;
    json["description"] = rule.description;
    json["enabled"] = rule.enabled;

    json["source"] = source_to_json(rule.source);
    json["destination"] = destination_to_json(rule.destination);
    json["transform"] = transform_to_json(rule.transform);

    return json;
}

Json::Value ConfigManager::source_to_json(const ModbusRTUSource& source) const {
    Json::Value json;

    json["protocol"] = "modbus_rtu";
    json["slave_id"] = source.slave_id;
    json["function_code"] = source.function_code;
    json["start_address"] = source.start_address;
    json["register_count"] = source.register_count;
    json["data_type"] = data_type_to_string(source.data_type);
    json["byte_order"] = byte_order_to_string(source.byte_order);
    json["poll_interval_ms"] = source.poll_interval_ms;
    json["timeout_ms"] = source.timeout_ms;
    json["retry_count"] = source.retry_count;

    return json;
}

Json::Value ConfigManager::destination_to_json(const DestinationConfig& dest) const {
    Json::Value json;

    if (dest.protocol == ProtocolType::MODBUS_TCP) {
        json["protocol"] = "modbus_tcp";
        json["slave_id"] = dest.modbus_tcp.slave_id;
        json["function_code"] = dest.modbus_tcp.function_code;
        json["start_address"] = dest.modbus_tcp.start_address;
        json["data_type"] = data_type_to_string(dest.modbus_tcp.data_type);
        json["byte_order"] = byte_order_to_string(dest.modbus_tcp.byte_order);
    } else if (dest.protocol == ProtocolType::S7) {
        json["protocol"] = "s7";
        json["db_number"] = dest.s7.db_number;
        json["start_byte"] = dest.s7.start_byte;
        json["bit_offset"] = dest.s7.bit_offset;
        json["data_type"] = data_type_to_string(dest.s7.data_type);
        json["byte_order"] = byte_order_to_string(dest.s7.byte_order);
    }

    return json;
}

Json::Value ConfigManager::transform_to_json(const TransformRule& transform) const {
    Json::Value json;

    switch (transform.operation) {
        case TransformOperation::NONE:
            json["operation"] = "none";
            break;
        case TransformOperation::SCALE:
            json["operation"] = "scale";
            break;
        case TransformOperation::EXPRESSION:
            json["operation"] = "expression";
            break;
        case TransformOperation::LOOKUP:
            json["operation"] = "lookup";
            break;
    }

    json["scale"] = transform.scale;
    json["offset"] = transform.offset;

    if (!transform.expression.empty()) {
        json["expression"] = transform.expression;
    }

    json["min_value"] = transform.min_value;
    json["max_value"] = transform.max_value;
    json["clamp_enabled"] = transform.clamp_enabled;

    return json;
}

} // namespace gateway
