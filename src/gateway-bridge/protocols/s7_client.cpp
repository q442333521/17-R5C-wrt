#include "s7_client.h"
#include <snap7.h>
#include <iostream>
#include <cstring>

namespace gateway {

S7Client::S7Client(const std::string& plc_ip, int rack, int slot)
    : client_(nullptr)
    , plc_ip_(plc_ip)
    , rack_(rack)
    , slot_(slot)
    , timeout_ms_(2000)
    , connected_(false)
    , last_error_code_(0)
{
    client_ = Cli_Create();
}

S7Client::~S7Client() {
    disconnect();
    if (client_) {
        Cli_Destroy(&client_);
    }
}

bool S7Client::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (connected_) {
        return true;
    }

    if (!client_) {
        last_error_ = "Client not initialized";
        return false;
    }

    // 设置超时
    Cli_SetAsConnectTimeout(client_, timeout_ms_);

    // 连接到PLC
    int result = Cli_ConnectTo(client_, plc_ip_.c_str(), rack_, slot_);
    if (!check_error(result)) {
        return false;
    }

    connected_ = true;
    return true;
}

void S7Client::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (client_ && connected_) {
        Cli_Disconnect(client_);
        connected_ = false;
    }
}

bool S7Client::is_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!client_ || !connected_) {
        return false;
    }

    // 检查连接状态
    int status;
    Cli_GetConnected(client_, &status);
    return status != 0;
}

bool S7Client::read_db(int db_number, int start_byte, int size, std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !client_) {
        last_error_ = "Not connected";
        return false;
    }

    data.resize(size);
    int result = Cli_DBRead(client_, db_number, start_byte, size, data.data());
    return check_error(result);
}

bool S7Client::write_db(int db_number, int start_byte, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || !client_) {
        last_error_ = "Not connected";
        return false;
    }

    int result = Cli_DBWrite(client_, db_number, start_byte, data.size(),
                             const_cast<uint8_t*>(data.data()));
    return check_error(result);
}

bool S7Client::read_db_real(int db_number, int byte_offset, float& value) {
    std::vector<uint8_t> buffer;
    if (!read_db(db_number, byte_offset, 4, buffer)) {
        return false;
    }

    // S7 Real是Big-Endian的IEEE 754浮点数
    uint32_t temp = (static_cast<uint32_t>(buffer[0]) << 24) |
                    (static_cast<uint32_t>(buffer[1]) << 16) |
                    (static_cast<uint32_t>(buffer[2]) << 8) |
                    static_cast<uint32_t>(buffer[3]);
    memcpy(&value, &temp, sizeof(float));
    return true;
}

bool S7Client::write_db_real(int db_number, int byte_offset, float value) {
    uint32_t temp;
    memcpy(&temp, &value, sizeof(float));

    std::vector<uint8_t> buffer(4);
    buffer[0] = (temp >> 24) & 0xFF;
    buffer[1] = (temp >> 16) & 0xFF;
    buffer[2] = (temp >> 8) & 0xFF;
    buffer[3] = temp & 0xFF;

    return write_db(db_number, byte_offset, buffer);
}

bool S7Client::read_db_dword(int db_number, int byte_offset, uint32_t& value) {
    std::vector<uint8_t> buffer;
    if (!read_db(db_number, byte_offset, 4, buffer)) {
        return false;
    }

    value = (static_cast<uint32_t>(buffer[0]) << 24) |
            (static_cast<uint32_t>(buffer[1]) << 16) |
            (static_cast<uint32_t>(buffer[2]) << 8) |
            static_cast<uint32_t>(buffer[3]);
    return true;
}

bool S7Client::write_db_dword(int db_number, int byte_offset, uint32_t value) {
    std::vector<uint8_t> buffer(4);
    buffer[0] = (value >> 24) & 0xFF;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;

    return write_db(db_number, byte_offset, buffer);
}

bool S7Client::read_db_int(int db_number, int byte_offset, int16_t& value) {
    std::vector<uint8_t> buffer;
    if (!read_db(db_number, byte_offset, 2, buffer)) {
        return false;
    }

    value = (static_cast<int16_t>(buffer[0]) << 8) |
            static_cast<int16_t>(buffer[1]);
    return true;
}

bool S7Client::write_db_int(int db_number, int byte_offset, int16_t value) {
    std::vector<uint8_t> buffer(2);
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;

    return write_db(db_number, byte_offset, buffer);
}

bool S7Client::read_db_word(int db_number, int byte_offset, uint16_t& value) {
    std::vector<uint8_t> buffer;
    if (!read_db(db_number, byte_offset, 2, buffer)) {
        return false;
    }

    value = (static_cast<uint16_t>(buffer[0]) << 8) |
            static_cast<uint16_t>(buffer[1]);
    return true;
}

bool S7Client::write_db_word(int db_number, int byte_offset, uint16_t value) {
    std::vector<uint8_t> buffer(2);
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;

    return write_db(db_number, byte_offset, buffer);
}

bool S7Client::read_db_bool(int db_number, int byte_offset, int bit_offset, bool& value) {
    std::vector<uint8_t> buffer;
    if (!read_db(db_number, byte_offset, 1, buffer)) {
        return false;
    }

    value = (buffer[0] & (1 << bit_offset)) != 0;
    return true;
}

bool S7Client::write_db_bool(int db_number, int byte_offset, int bit_offset, bool value) {
    std::vector<uint8_t> buffer;
    if (!read_db(db_number, byte_offset, 1, buffer)) {
        return false;
    }

    if (value) {
        buffer[0] |= (1 << bit_offset);
    } else {
        buffer[0] &= ~(1 << bit_offset);
    }

    return write_db(db_number, byte_offset, buffer);
}

void S7Client::set_timeout(int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    timeout_ms_ = timeout_ms;

    if (client_) {
        Cli_SetAsConnectTimeout(client_, timeout_ms_);
    }
}

std::string S7Client::get_last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

int S7Client::get_last_error_code() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_code_;
}

bool S7Client::check_error(int result) {
    if (result == 0) {
        last_error_.clear();
        last_error_code_ = 0;
        return true;
    }

    last_error_code_ = result;
    last_error_ = error_text(result);
    return false;
}

std::string S7Client::error_text(int error_code) const {
    char buffer[256];
    if (client_) {
        Cli_ErrorText(error_code, buffer, sizeof(buffer));
        return std::string(buffer);
    }
    return "Unknown error: " + std::to_string(error_code);
}

} // namespace gateway
