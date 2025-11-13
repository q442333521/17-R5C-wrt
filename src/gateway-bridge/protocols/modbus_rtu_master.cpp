#include "modbus_rtu_master.h"
#include <iostream>
#include <cstring>
#include <errno.h>

namespace gateway {

ModbusRTUMaster::ModbusRTUMaster(const std::string& device, int baudrate,
                                 char parity, int data_bits, int stop_bits)
    : ctx_(nullptr)
    , device_(device)
    , baudrate_(baudrate)
    , parity_(parity)
    , data_bits_(data_bits)
    , stop_bits_(stop_bits)
    , timeout_ms_(1000)
    , retry_count_(3)
    , connected_(false)
{
}

ModbusRTUMaster::~ModbusRTUMaster() {
    disconnect();
}

bool ModbusRTUMaster::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (connected_) {
        return true;
    }

    // 创建RTU上下文
    ctx_ = modbus_new_rtu(device_.c_str(), baudrate_, parity_, data_bits_, stop_bits_);
    if (!ctx_) {
        last_error_ = "Failed to create RTU context: " + std::string(modbus_strerror(errno));
        return false;
    }

    // 设置超时
    struct timeval response_timeout;
    response_timeout.tv_sec = timeout_ms_ / 1000;
    response_timeout.tv_usec = (timeout_ms_ % 1000) * 1000;
    modbus_set_response_timeout(ctx_, response_timeout.tv_sec, response_timeout.tv_usec);

    // 设置字节超时
    struct timeval byte_timeout;
    byte_timeout.tv_sec = 0;
    byte_timeout.tv_usec = 500000;  // 500ms
    modbus_set_byte_timeout(ctx_, byte_timeout.tv_sec, byte_timeout.tv_usec);

    // 连接
    if (modbus_connect(ctx_) == -1) {
        last_error_ = "Connection failed: " + std::string(modbus_strerror(errno));
        modbus_free(ctx_);
        ctx_ = nullptr;
        return false;
    }

    connected_ = true;
    return true;
}

void ModbusRTUMaster::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }
    connected_ = false;
}

bool ModbusRTUMaster::is_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

bool ModbusRTUMaster::set_slave(int slave_id) {
    if (modbus_set_slave(ctx_, slave_id) == -1) {
        last_error_ = "Failed to set slave: " + std::string(modbus_strerror(errno));
        return false;
    }
    return true;
}

bool ModbusRTUMaster::retry_operation(std::function<bool()> operation) {
    for (int i = 0; i < retry_count_; ++i) {
        if (operation()) {
            return true;
        }
        // 短暂延迟后重试
        if (i < retry_count_ - 1) {
            usleep(50000);  // 50ms
        }
    }
    return false;
}

bool ModbusRTUMaster::read_holding_registers(int slave_id, int address, int count,
                                              std::vector<uint16_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    data.resize(count);

    auto operation = [&]() -> bool {
        int rc = modbus_read_registers(ctx_, address, count, data.data());
        if (rc == -1) {
            last_error_ = "Read holding registers failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

bool ModbusRTUMaster::read_input_registers(int slave_id, int address, int count,
                                            std::vector<uint16_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    data.resize(count);

    auto operation = [&]() -> bool {
        int rc = modbus_read_input_registers(ctx_, address, count, data.data());
        if (rc == -1) {
            last_error_ = "Read input registers failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

bool ModbusRTUMaster::read_coils(int slave_id, int address, int count,
                                  std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    data.resize(count);

    auto operation = [&]() -> bool {
        int rc = modbus_read_bits(ctx_, address, count, data.data());
        if (rc == -1) {
            last_error_ = "Read coils failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

bool ModbusRTUMaster::read_discrete_inputs(int slave_id, int address, int count,
                                            std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    data.resize(count);

    auto operation = [&]() -> bool {
        int rc = modbus_read_input_bits(ctx_, address, count, data.data());
        if (rc == -1) {
            last_error_ = "Read discrete inputs failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

bool ModbusRTUMaster::write_single_register(int slave_id, int address, uint16_t value) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    auto operation = [&]() -> bool {
        int rc = modbus_write_register(ctx_, address, value);
        if (rc == -1) {
            last_error_ = "Write single register failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

bool ModbusRTUMaster::write_multiple_registers(int slave_id, int address,
                                                const std::vector<uint16_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    auto operation = [&]() -> bool {
        int rc = modbus_write_registers(ctx_, address, data.size(), data.data());
        if (rc == -1) {
            last_error_ = "Write multiple registers failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

bool ModbusRTUMaster::write_single_coil(int slave_id, int address, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    auto operation = [&]() -> bool {
        int rc = modbus_write_bit(ctx_, address, value ? 1 : 0);
        if (rc == -1) {
            last_error_ = "Write single coil failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

bool ModbusRTUMaster::write_multiple_coils(int slave_id, int address,
                                           const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !ctx_) {
        last_error_ = "Not connected";
        return false;
    }

    if (!set_slave(slave_id)) {
        return false;
    }

    auto operation = [&]() -> bool {
        int rc = modbus_write_bits(ctx_, address, data.size(), data.data());
        if (rc == -1) {
            last_error_ = "Write multiple coils failed: " + std::string(modbus_strerror(errno));
            return false;
        }
        return true;
    };

    return retry_operation(operation);
}

void ModbusRTUMaster::set_timeout(int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    timeout_ms_ = timeout_ms;

    if (ctx_) {
        struct timeval response_timeout;
        response_timeout.tv_sec = timeout_ms_ / 1000;
        response_timeout.tv_usec = (timeout_ms_ % 1000) * 1000;
        modbus_set_response_timeout(ctx_, response_timeout.tv_sec, response_timeout.tv_usec);
    }
}

void ModbusRTUMaster::set_retry_count(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    retry_count_ = count;
}

std::string ModbusRTUMaster::get_last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

} // namespace gateway
