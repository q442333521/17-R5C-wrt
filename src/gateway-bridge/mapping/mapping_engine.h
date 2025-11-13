#ifndef GATEWAY_BRIDGE_MAPPING_ENGINE_H
#define GATEWAY_BRIDGE_MAPPING_ENGINE_H

#include "../common/types.h"
#include "../common/config.h"
#include "../protocols/modbus_rtu_master.h"
#include "../protocols/modbus_tcp_server.h"
#include "../protocols/s7_client.h"
#include "data_converter.h"

#include <memory>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>

namespace gateway {

/**
 * 数据映射引擎
 * 核心功能：协调RTU、TCP、S7之间的数据流转
 */
class MappingEngine {
public:
    MappingEngine(std::shared_ptr<ModbusRTUMaster> rtu_master,
                  std::shared_ptr<ModbusTCPServer> tcp_server,
                  std::shared_ptr<S7Client> s7_client);
    ~MappingEngine();

    // 禁止拷贝
    MappingEngine(const MappingEngine&) = delete;
    MappingEngine& operator=(const MappingEngine&) = delete;

    // 加载映射规则
    bool load_rules(const std::vector<MappingRule>& rules);

    // 添加/删除/更新单个规则
    bool add_rule(const MappingRule& rule);
    bool remove_rule(const std::string& rule_id);
    bool update_rule(const std::string& rule_id, const MappingRule& rule);

    // 启动/停止引擎
    bool start();
    void stop();
    bool is_running() const;

    // 手动触发同步
    bool sync_rule(const std::string& rule_id);

    // 获取规则状态
    std::vector<MappingRule> get_all_rules() const;
    bool get_rule_status(const std::string& rule_id, MappingStatus& status) const;

private:
    std::shared_ptr<ModbusRTUMaster> rtu_master_;
    std::shared_ptr<ModbusTCPServer> tcp_server_;
    std::shared_ptr<S7Client> s7_client_;

    std::map<std::string, MappingRule> rules_;
    std::map<std::string, std::thread> worker_threads_;
    std::atomic<bool> running_;
    mutable std::mutex mutex_;

    // 工作线程函数
    void rule_worker(const std::string& rule_id);

    // 执行单次数据同步
    bool execute_rule_once(MappingRule& rule);

    // RTU读取
    bool read_from_rtu(const ModbusRTUSource& source, double& value);

    // 写入目标
    bool write_to_destination(const DestinationConfig& dest, double value);

    // 写入Modbus TCP
    bool write_to_modbus_tcp(const ModbusTCPDestination& dest, double value);

    // 写入S7
    bool write_to_s7(const S7Destination& dest, double value);
};

} // namespace gateway

#endif // GATEWAY_BRIDGE_MAPPING_ENGINE_H
