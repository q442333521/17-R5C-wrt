#include "modbus_tcp_server.h"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <unistd.h>

namespace gateway {

ModbusTCPServer::ModbusTCPServer(const std::string& listen_ip, int port)
    : ctx_(nullptr)
    , mapping_(nullptr)
    , listen_ip_(listen_ip)
    , port_(port)
    , socket_(-1)
    , running_(false)
    , connection_count_(0)
{
}

ModbusTCPServer::~ModbusTCPServer() {
    stop();
}

bool ModbusTCPServer::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return true;
    }

    // 创建TCP上下文
    ctx_ = modbus_new_tcp(listen_ip_.c_str(), port_);
    if (!ctx_) {
        last_error_ = "Failed to create TCP context: " + std::string(modbus_strerror(errno));
        return false;
    }

    // 创建寄存器映射 (保持寄存器, 输入寄存器, 线圈, 离散输入)
    // 参数: coils, discrete_inputs, holding_registers, input_registers
    mapping_ = modbus_mapping_new(1000, 1000, 1000, 1000);
    if (!mapping_) {
        last_error_ = "Failed to create mapping: " + std::string(modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    // 初始化寄存器为0
    memset(mapping_->tab_bits, 0, mapping_->nb_bits * sizeof(uint8_t));
    memset(mapping_->tab_input_bits, 0, mapping_->nb_input_bits * sizeof(uint8_t));
    memset(mapping_->tab_registers, 0, mapping_->nb_registers * sizeof(uint16_t));
    memset(mapping_->tab_input_registers, 0, mapping_->nb_input_registers * sizeof(uint16_t));

    // 监听
    socket_ = modbus_tcp_listen(ctx_, 32);  // 最大32个连接
    if (socket_ == -1) {
        last_error_ = "Failed to listen: " + std::string(modbus_strerror(errno));
        modbus_mapping_free(mapping_);
        modbus_free(ctx_);
        ctx_ = nullptr;
        mapping_ = nullptr;
        return false;
    }

    // 启动服务器线程
    running_ = true;
    server_thread_ = std::thread(&ModbusTCPServer::server_loop, this);

    return true;
}

void ModbusTCPServer::stop() {
    running_ = false;

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_ != -1) {
        close(socket_);
        socket_ = -1;
    }

    if (mapping_) {
        modbus_mapping_free(mapping_);
        mapping_ = nullptr;
    }

    if (ctx_) {
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
}

bool ModbusTCPServer::is_running() const {
    return running_;
}

void ModbusTCPServer::server_loop() {
    uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

    while (running_) {
        // 接受新连接
        int client_socket = modbus_tcp_accept(ctx_, &socket_);
        if (client_socket == -1) {
            if (running_) {
                std::cerr << "Accept failed: " << modbus_strerror(errno) << std::endl;
                usleep(100000);  // 100ms
            }
            continue;
        }

        connection_count_++;

        // 处理客户端请求
        while (running_) {
            int rc = modbus_receive(ctx_, query);
            if (rc == -1) {
                // 连接断开或错误
                break;
            }

            // 检查是否是写操作
            int function_code = query[7];
            bool is_write = (function_code == 0x05 || function_code == 0x06 ||
                            function_code == 0x0F || function_code == 0x10);

            // 处理请求
            rc = modbus_reply(ctx_, query, rc, mapping_);
            if (rc == -1) {
                std::cerr << "Reply failed: " << modbus_strerror(errno) << std::endl;
                break;
            }

            // 如果是写操作且有回调，触发回调
            if (is_write && write_callback_) {
                // 解析地址和数据
                int address = (query[8] << 8) | query[9];
                int count = (query[10] << 8) | query[11];

                std::vector<uint16_t> data;
                if (function_code == 0x06) {  // 写单个寄存器
                    data.push_back((query[10] << 8) | query[11]);
                    write_callback_(address, data);
                } else if (function_code == 0x10) {  // 写多个寄存器
                    for (int i = 0; i < count; ++i) {
                        uint16_t value = (query[13 + i * 2] << 8) | query[14 + i * 2];
                        data.push_back(value);
                    }
                    write_callback_(address, data);
                }
            }
        }

        close(client_socket);
        connection_count_--;
    }
}

bool ModbusTCPServer::set_holding_registers(int address, const std::vector<uint16_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + data.size() > mapping_->nb_registers) {
        last_error_ = "Address out of range";
        return false;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        mapping_->tab_registers[address + i] = data[i];
    }

    return true;
}

bool ModbusTCPServer::get_holding_registers(int address, int count, std::vector<uint16_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + count > mapping_->nb_registers) {
        last_error_ = "Address out of range";
        return false;
    }

    data.resize(count);
    for (int i = 0; i < count; ++i) {
        data[i] = mapping_->tab_registers[address + i];
    }

    return true;
}

bool ModbusTCPServer::set_input_registers(int address, const std::vector<uint16_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + data.size() > mapping_->nb_input_registers) {
        last_error_ = "Address out of range";
        return false;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        mapping_->tab_input_registers[address + i] = data[i];
    }

    return true;
}

bool ModbusTCPServer::get_input_registers(int address, int count, std::vector<uint16_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + count > mapping_->nb_input_registers) {
        last_error_ = "Address out of range";
        return false;
    }

    data.resize(count);
    for (int i = 0; i < count; ++i) {
        data[i] = mapping_->tab_input_registers[address + i];
    }

    return true;
}

bool ModbusTCPServer::set_coils(int address, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + data.size() > mapping_->nb_bits) {
        last_error_ = "Address out of range";
        return false;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        mapping_->tab_bits[address + i] = data[i];
    }

    return true;
}

bool ModbusTCPServer::get_coils(int address, int count, std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + count > mapping_->nb_bits) {
        last_error_ = "Address out of range";
        return false;
    }

    data.resize(count);
    for (int i = 0; i < count; ++i) {
        data[i] = mapping_->tab_bits[address + i];
    }

    return true;
}

bool ModbusTCPServer::set_discrete_inputs(int address, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + data.size() > mapping_->nb_input_bits) {
        last_error_ = "Address out of range";
        return false;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        mapping_->tab_input_bits[address + i] = data[i];
    }

    return true;
}

bool ModbusTCPServer::get_discrete_inputs(int address, int count, std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mapping_) {
        last_error_ = "Mapping not initialized";
        return false;
    }

    if (address < 0 || address + count > mapping_->nb_input_bits) {
        last_error_ = "Address out of range";
        return false;
    }

    data.resize(count);
    for (int i = 0; i < count; ++i) {
        data[i] = mapping_->tab_input_bits[address + i];
    }

    return true;
}

void ModbusTCPServer::set_write_callback(WriteCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    write_callback_ = callback;
}

int ModbusTCPServer::get_connection_count() const {
    return connection_count_;
}

std::string ModbusTCPServer::get_last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

} // namespace gateway
