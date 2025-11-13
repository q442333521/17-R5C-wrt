#ifndef GATEWAY_BRIDGE_MODBUS_RTU_MASTER_H
#define GATEWAY_BRIDGE_MODBUS_RTU_MASTER_H

#include <string>
#include <vector>
#include <mutex>
#include <modbus.h>

namespace gateway {

/**
 * Modbus RTU 主站
 * 负责从RTU从站读取数据
 */
class ModbusRTUMaster {
public:
    ModbusRTUMaster(const std::string& device, int baudrate,
                    char parity = 'N', int data_bits = 8, int stop_bits = 1);
    ~ModbusRTUMaster();

    // 禁止拷贝
    ModbusRTUMaster(const ModbusRTUMaster&) = delete;
    ModbusRTUMaster& operator=(const ModbusRTUMaster&) = delete;

    // 连接管理
    bool connect();
    void disconnect();
    bool is_connected() const;

    // 读取操作
    bool read_holding_registers(int slave_id, int address, int count,
                                 std::vector<uint16_t>& data);
    bool read_input_registers(int slave_id, int address, int count,
                               std::vector<uint16_t>& data);
    bool read_coils(int slave_id, int address, int count,
                    std::vector<uint8_t>& data);
    bool read_discrete_inputs(int slave_id, int address, int count,
                               std::vector<uint8_t>& data);

    // 写入操作
    bool write_single_register(int slave_id, int address, uint16_t value);
    bool write_multiple_registers(int slave_id, int address,
                                   const std::vector<uint16_t>& data);
    bool write_single_coil(int slave_id, int address, bool value);
    bool write_multiple_coils(int slave_id, int address,
                              const std::vector<uint8_t>& data);

    // 配置
    void set_timeout(int timeout_ms);
    void set_retry_count(int count);

    // 获取错误信息
    std::string get_last_error() const;

private:
    modbus_t* ctx_;
    std::string device_;
    int baudrate_;
    char parity_;
    int data_bits_;
    int stop_bits_;
    int timeout_ms_;
    int retry_count_;
    bool connected_;
    mutable std::mutex mutex_;
    std::string last_error_;

    // 辅助函数
    bool set_slave(int slave_id);
    bool retry_operation(std::function<bool()> operation);
};

} // namespace gateway

#endif // GATEWAY_BRIDGE_MODBUS_RTU_MASTER_H
