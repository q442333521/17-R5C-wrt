#include "mapping_engine.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace gateway {

MappingEngine::MappingEngine(std::shared_ptr<ModbusRTUMaster> rtu_master,
                             std::shared_ptr<ModbusTCPServer> tcp_server,
                             std::shared_ptr<S7Client> s7_client)
    : rtu_master_(rtu_master)
    , tcp_server_(tcp_server)
    , s7_client_(s7_client)
    , running_(false)
{
}

MappingEngine::~MappingEngine() {
    stop();
}

bool MappingEngine::load_rules(const std::vector<MappingRule>& rules) {
    std::lock_guard<std::mutex> lock(mutex_);

    rules_.clear();
    for (const auto& rule : rules) {
        rules_[rule.rule_id] = rule;
    }

    return true;
}

bool MappingEngine::add_rule(const MappingRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (rules_.find(rule.rule_id) != rules_.end()) {
        return false;  // 规则已存在
    }

    rules_[rule.rule_id] = rule;

    // 如果引擎正在运行且规则已启用，启动工作线程
    if (running_ && rule.enabled) {
        worker_threads_[rule.rule_id] = std::thread(&MappingEngine::rule_worker, this, rule.rule_id);
    }

    return true;
}

bool MappingEngine::remove_rule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rules_.find(rule_id);
    if (it == rules_.end()) {
        return false;
    }

    // 停止工作线程
    auto thread_it = worker_threads_.find(rule_id);
    if (thread_it != worker_threads_.end()) {
        // TODO: 优雅地停止线程
        if (thread_it->second.joinable()) {
            thread_it->second.join();
        }
        worker_threads_.erase(thread_it);
    }

    rules_.erase(it);
    return true;
}

bool MappingEngine::update_rule(const std::string& rule_id, const MappingRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rules_.find(rule_id);
    if (it == rules_.end()) {
        return false;
    }

    // 保留运行状态
    MappingStatus old_status = it->second.status;
    it->second = rule;
    it->second.status = old_status;

    return true;
}

bool MappingEngine::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return true;
    }

    running_ = true;

    // 为每个启用的规则创建工作线程
    for (const auto& pair : rules_) {
        if (pair.second.enabled) {
            worker_threads_[pair.first] = std::thread(&MappingEngine::rule_worker, this, pair.first);
        }
    }

    std::cout << "Mapping engine started with " << worker_threads_.size() << " rules" << std::endl;
    return true;
}

void MappingEngine::stop() {
    running_ = false;

    // 等待所有工作线程结束
    for (auto& pair : worker_threads_) {
        if (pair.second.joinable()) {
            pair.second.join();
        }
    }

    worker_threads_.clear();
    std::cout << "Mapping engine stopped" << std::endl;
}

bool MappingEngine::is_running() const {
    return running_;
}

bool MappingEngine::sync_rule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rules_.find(rule_id);
    if (it == rules_.end()) {
        return false;
    }

    return execute_rule_once(it->second);
}

std::vector<MappingRule> MappingEngine::get_all_rules() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<MappingRule> result;
    for (const auto& pair : rules_) {
        result.push_back(pair.second);
    }
    return result;
}

bool MappingEngine::get_rule_status(const std::string& rule_id, MappingStatus& status) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rules_.find(rule_id);
    if (it == rules_.end()) {
        return false;
    }

    status = it->second.status;
    return true;
}

void MappingEngine::rule_worker(const std::string& rule_id) {
    std::cout << "Worker thread started for rule: " << rule_id << std::endl;

    while (running_) {
        MappingRule rule;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = rules_.find(rule_id);
            if (it == rules_.end() || !it->second.enabled) {
                break;
            }
            rule = it->second;
        }

        // 执行数据同步
        bool success = execute_rule_once(rule);

        // 更新状态
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = rules_.find(rule_id);
            if (it != rules_.end()) {
                if (success) {
                    it->second.status.read_count++;
                    it->second.status.write_count++;
                } else {
                    it->second.status.error_count++;
                }
                it->second.status.is_healthy = success;

                // 更新时间戳
                auto now = std::chrono::system_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                it->second.status.last_update_ms = ms;
            }
        }

        // 等待下一次轮询
        std::this_thread::sleep_for(
            std::chrono::milliseconds(rule.source.poll_interval_ms));
    }

    std::cout << "Worker thread stopped for rule: " << rule_id << std::endl;
}

bool MappingEngine::execute_rule_once(MappingRule& rule) {
    // 1. 从RTU读取数据
    double value;
    if (!read_from_rtu(rule.source, value)) {
        return false;
    }

    // 2. 应用数据转换
    double transformed_value = DataConverter::transform_value(value, rule.transform);

    // 3. 记录最后的值
    rule.status.last_value = transformed_value;

    // 4. 写入目标
    return write_to_destination(rule.destination, transformed_value);
}

bool MappingEngine::read_from_rtu(const ModbusRTUSource& source, double& value) {
    if (!rtu_master_ || !rtu_master_->is_connected()) {
        std::cerr << "RTU master not connected" << std::endl;
        return false;
    }

    std::vector<uint16_t> registers;
    bool success = false;

    // 根据功能码读取
    switch (source.function_code) {
        case 3:  // 读保持寄存器
            success = rtu_master_->read_holding_registers(
                source.slave_id, source.start_address, source.register_count, registers);
            break;
        case 4:  // 读输入寄存器
            success = rtu_master_->read_input_registers(
                source.slave_id, source.start_address, source.register_count, registers);
            break;
        default:
            std::cerr << "Unsupported function code: " << source.function_code << std::endl;
            return false;
    }

    if (!success) {
        std::cerr << "Failed to read from RTU: " << rtu_master_->get_last_error() << std::endl;
        return false;
    }

    // 转换为数值
    value = DataConverter::registers_to_value(registers, source.data_type, source.byte_order);
    return true;
}

bool MappingEngine::write_to_destination(const DestinationConfig& dest, double value) {
    switch (dest.protocol) {
        case ProtocolType::MODBUS_TCP:
            return write_to_modbus_tcp(dest.modbus_tcp, value);
        case ProtocolType::S7:
            return write_to_s7(dest.s7, value);
        default:
            std::cerr << "Unsupported destination protocol" << std::endl;
            return false;
    }
}

bool MappingEngine::write_to_modbus_tcp(const ModbusTCPDestination& dest, double value) {
    if (!tcp_server_ || !tcp_server_->is_running()) {
        std::cerr << "TCP server not running" << std::endl;
        return false;
    }

    // 转换为寄存器
    std::vector<uint16_t> registers = DataConverter::value_to_registers(
        value, dest.data_type, dest.byte_order);

    // 写入寄存器映射
    bool success = false;
    switch (dest.function_code) {
        case 6:  // 写单个寄存器
            if (!registers.empty()) {
                std::vector<uint16_t> single_reg = {registers[0]};
                success = tcp_server_->set_holding_registers(dest.start_address, single_reg);
            }
            break;
        case 16:  // 写多个寄存器
            success = tcp_server_->set_holding_registers(dest.start_address, registers);
            break;
        default:
            std::cerr << "Unsupported function code: " << dest.function_code << std::endl;
            return false;
    }

    if (!success) {
        std::cerr << "Failed to write to TCP server: " << tcp_server_->get_last_error() << std::endl;
    }

    return success;
}

bool MappingEngine::write_to_s7(const S7Destination& dest, double value) {
    if (!s7_client_ || !s7_client_->is_connected()) {
        std::cerr << "S7 client not connected" << std::endl;
        return false;
    }

    bool success = false;

    // 根据数据类型写入
    switch (dest.data_type) {
        case DataType::FLOAT:
            success = s7_client_->write_db_real(dest.db_number, dest.start_byte,
                                                static_cast<float>(value));
            break;
        case DataType::INT16:
            success = s7_client_->write_db_int(dest.db_number, dest.start_byte,
                                               static_cast<int16_t>(value));
            break;
        case DataType::UINT16:
            success = s7_client_->write_db_word(dest.db_number, dest.start_byte,
                                                static_cast<uint16_t>(value));
            break;
        case DataType::INT32:
        case DataType::UINT32:
            success = s7_client_->write_db_dword(dest.db_number, dest.start_byte,
                                                 static_cast<uint32_t>(value));
            break;
        default:
            std::cerr << "Unsupported S7 data type" << std::endl;
            return false;
    }

    if (!success) {
        std::cerr << "Failed to write to S7: " << s7_client_->get_last_error() << std::endl;
    }

    return success;
}

} // namespace gateway
