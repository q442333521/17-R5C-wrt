#ifndef GATEWAY_BRIDGE_MODBUS_TCP_SERVER_H
#define GATEWAY_BRIDGE_MODBUS_TCP_SERVER_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <modbus.h>

namespace gateway {

/**
 * Modbus TCP 服务器
 * 作为从站接受客户端连接并响应查询
 */
class ModbusTCPServer {
public:
    ModbusTCPServer(const std::string& listen_ip, int port);
    ~ModbusTCPServer();

    // 禁止拷贝
    ModbusTCPServer(const ModbusTCPServer&) = delete;
    ModbusTCPServer& operator=(const ModbusTCPServer&) = delete;

    // 启动/停止服务器
    bool start();
    void stop();
    bool is_running() const;

    // 寄存器映射设置
    bool set_holding_registers(int address, const std::vector<uint16_t>& data);
    bool get_holding_registers(int address, int count, std::vector<uint16_t>& data);

    bool set_input_registers(int address, const std::vector<uint16_t>& data);
    bool get_input_registers(int address, int count, std::vector<uint16_t>& data);

    bool set_coils(int address, const std::vector<uint8_t>& data);
    bool get_coils(int address, int count, std::vector<uint8_t>& data);

    bool set_discrete_inputs(int address, const std::vector<uint8_t>& data);
    bool get_discrete_inputs(int address, int count, std::vector<uint8_t>& data);

    // 回调函数：当客户端写入时触发
    using WriteCallback = std::function<void(int address, const std::vector<uint16_t>&)>;
    void set_write_callback(WriteCallback callback);

    // 获取连接数
    int get_connection_count() const;

    // 获取错误信息
    std::string get_last_error() const;

private:
    modbus_t* ctx_;
    modbus_mapping_t* mapping_;
    std::string listen_ip_;
    int port_;
    int socket_;
    std::thread server_thread_;
    std::atomic<bool> running_;
    WriteCallback write_callback_;
    std::atomic<int> connection_count_;
    mutable std::mutex mutex_;
    std::string last_error_;

    // 服务器主循环
    void server_loop();

    // 处理单个连接
    void handle_connection(int client_socket);
};

} // namespace gateway

#endif // GATEWAY_BRIDGE_MODBUS_TCP_SERVER_H
