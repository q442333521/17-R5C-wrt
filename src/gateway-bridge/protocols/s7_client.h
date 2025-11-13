#ifndef GATEWAY_BRIDGE_S7_CLIENT_H
#define GATEWAY_BRIDGE_S7_CLIENT_H

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

// Snap7前向声明
typedef void* S7Object;

namespace gateway {

/**
 * S7 客户端
 * 使用Snap7库连接西门子S7 PLC
 */
class S7Client {
public:
    S7Client(const std::string& plc_ip, int rack, int slot);
    ~S7Client();

    // 禁止拷贝
    S7Client(const S7Client&) = delete;
    S7Client& operator=(const S7Client&) = delete;

    // 连接管理
    bool connect();
    void disconnect();
    bool is_connected() const;

    // DB块读写
    bool read_db(int db_number, int start_byte, int size, std::vector<uint8_t>& data);
    bool write_db(int db_number, int start_byte, const std::vector<uint8_t>& data);

    // 数据类型读写辅助函数
    bool read_db_real(int db_number, int byte_offset, float& value);
    bool write_db_real(int db_number, int byte_offset, float value);

    bool read_db_dword(int db_number, int byte_offset, uint32_t& value);
    bool write_db_dword(int db_number, int byte_offset, uint32_t value);

    bool read_db_int(int db_number, int byte_offset, int16_t& value);
    bool write_db_int(int db_number, int byte_offset, int16_t value);

    bool read_db_word(int db_number, int byte_offset, uint16_t& value);
    bool write_db_word(int db_number, int byte_offset, uint16_t value);

    bool read_db_bool(int db_number, int byte_offset, int bit_offset, bool& value);
    bool write_db_bool(int db_number, int byte_offset, int bit_offset, bool value);

    // 超时设置
    void set_timeout(int timeout_ms);

    // 获取错误信息
    std::string get_last_error() const;
    int get_last_error_code() const;

private:
    S7Object client_;
    std::string plc_ip_;
    int rack_;
    int slot_;
    int timeout_ms_;
    bool connected_;
    mutable std::mutex mutex_;
    std::string last_error_;
    int last_error_code_;

    // 辅助函数
    bool check_error(int result);
    std::string error_text(int error_code) const;
};

} // namespace gateway

#endif // GATEWAY_BRIDGE_S7_CLIENT_H
