#include "../common/logger.h"
#include "../common/config.h"
#include "../common/shm_ring.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <memory>
#include <filesystem>
#include <fstream>
#include <json/json.h>

// 全局运行标志
volatile sig_atomic_t g_running = 1;
static const auto g_start_time = std::chrono::steady_clock::now();

void signal_handler(int signum) {
    LOG_INFO("Received signal %d, shutting down...", signum);
    g_running = 0;
}

/**
 * Simple HTTP Server
 * 简单的 HTTP 服务器
 */
class SimpleHTTPServer {
public:
    SimpleHTTPServer(int port, std::string config_path)
        : port_(port), config_path_(std::move(config_path)), server_fd_(-1) {}
    
    ~SimpleHTTPServer() {
        stop();
    }
    
    bool start() {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            LOG_ERROR("Failed to create socket");
            return false;
        }
        
        // 设置 SO_REUSEADDR
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);
        
        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            LOG_ERROR("Failed to bind to port %d", port_);
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }
        
        if (listen(server_fd_, 10) < 0) {
            LOG_ERROR("Failed to listen");
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }
        
        LOG_INFO("HTTP server listening on port %d", port_);
        return true;
    }
    
    void stop() {
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }
    }
    
    void handle_requests(RingBuffer* ring) {
        while (g_running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            
            int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (g_running) {
                    LOG_ERROR("Failed to accept connection");
                }
                continue;
            }
            
            // 处理请求（简化版，仅支持 GET）
            char buffer[4096];
            ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            
            if (n > 0) {
                buffer[n] = '\0';
                
                // 解析请求行
                std::string request(buffer);
                std::istringstream iss(request);
                std::string method, path, version;
                iss >> method >> path >> version;
                
                LOG_INFO("Request: %s %s", method.c_str(), path.c_str());
                
                std::string response;
                
                if (path == "/api/status") {
                    response = handle_status(ring);
                } else if (path == "/api/config") {
                    if (method == "GET") {
                        response = handle_get_config();
                    } else if (method == "POST") {
                        // TODO: 解析 POST 数据
                        response = handle_post_config(request);
                    }
                } else if (path == "/" || path == "/index.html") {
                    response = handle_index();
                } else {
                    response = handle_404();
                }
                
                send(client_fd, response.c_str(), response.length(), 0);
            }
            
            close(client_fd);
        }
    }
    
private:
    std::string build_json_response(const std::string& status_line, const Json::Value& payload) {
        Json::StreamWriterBuilder builder;
        std::string json_str = Json::writeString(builder, payload);
        
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_line << "\r\n";
        oss << "Content-Type: application/json\r\n";
        oss << "Content-Length: " << json_str.length() << "\r\n";
        oss << "Access-Control-Allow-Origin: *\r\n";
        oss << "\r\n";
        oss << json_str;
        return oss.str();
    }

    std::string handle_status(RingBuffer* ring) {
        Json::Value root;
        ConfigManager& config = ConfigManager::instance();
        auto rs485_cfg = config.get_rs485_config();
        auto modbus_cfg = config.get_modbus_config();
        auto s7_cfg = config.get_s7_config();
        auto opcua_cfg = config.get_opcua_config();
        auto active_protocol = config.get_string("protocol.active", "modbus");
        
        static bool has_prev = false;
        static uint32_t last_sequence_raw = 0;
        static uint64_t sequence_wraps = 0;
        static uint64_t total_dropped = 0;
        static double last_interval_ms = 0.0;
        static double frequency_hz = 0.0;
        static uint64_t last_timestamp_ns = 0;
        static uint32_t last_gap = 0;
        static NormalizedData last_sample{};
        static uint64_t last_samples_window = 0;
        
        const double expected_frequency_hz =
            rs485_cfg.poll_rate_ms > 0 ? 1000.0 / static_cast<double>(rs485_cfg.poll_rate_ms) : 0.0;
        uint64_t samples_in_window = last_samples_window;
        
        // 系统状态
        root["running"] = (g_running != 0);
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - g_start_time).count();
        root["uptime_seconds"] = static_cast<Json::UInt64>(uptime);
        
        // 从共享内存读取最新数据
        NormalizedData data;
        bool has_sample = false;
        bool is_new_sample = false;
        if (ring) {
            NormalizedData latest;
            if (ring->peek_latest(latest) && ndm_verify_crc(latest)) {
                has_sample = true;
                data = latest;
                if (!has_prev || latest.sequence != last_sequence_raw) {
                    is_new_sample = true;
                }
            }
        }
        if (!has_sample && has_prev) {
            data = last_sample;
            has_sample = true;
        }

        if (has_sample) {
            root["current_data"]["thickness_mm"] = data.thickness_mm;
            root["current_data"]["sequence"] = data.sequence;
            root["current_data"]["status"] = data.status;
            root["current_data"]["timestamp_ns"] = static_cast<Json::UInt64>(data.timestamp_ns);
            uint64_t now_ns = get_timestamp_ns();
            root["current_data"]["age_ms"] = (now_ns > data.timestamp_ns)
                ? static_cast<Json::UInt64>((now_ns - data.timestamp_ns) / 1000000ULL)
                : static_cast<Json::UInt64>(0);
        } else {
            root["current_data"] = Json::nullValue;
        }

        if (has_sample && is_new_sample) {
            if (has_prev) {
                if (data.sequence < last_sequence_raw) {
                    sequence_wraps++;
                }
                uint64_t current_abs_seq = (sequence_wraps << 32) + data.sequence;
                uint64_t previous_abs_seq = (sequence_wraps << 32) + last_sequence_raw;
                if (data.sequence < last_sequence_raw) {
                    previous_abs_seq -= (1ULL << 32);
                }
                uint64_t seq_diff = current_abs_seq - previous_abs_seq;
                samples_in_window = seq_diff;
                
                uint64_t time_diff_ns = data.timestamp_ns > last_timestamp_ns
                    ? (data.timestamp_ns - last_timestamp_ns)
                    : 0;
                
                if (seq_diff > 0 && time_diff_ns > 0) {
                    const double total_interval_ms = static_cast<double>(time_diff_ns) / 1e6;
                    const double avg_interval_ms = total_interval_ms / static_cast<double>(seq_diff);
                    last_interval_ms = avg_interval_ms;
                    frequency_hz = avg_interval_ms > 0.0 ? 1000.0 / avg_interval_ms : frequency_hz;
                    
                    double expected_samples = 0.0;
                    if (rs485_cfg.poll_rate_ms > 0) {
                        expected_samples = static_cast<double>(time_diff_ns) /
                            (static_cast<double>(rs485_cfg.poll_rate_ms) * 1e6);
                    }
                    uint64_t expected_round = static_cast<uint64_t>(std::llround(expected_samples));
                    if (expected_round < 1) {
                        expected_round = 1;
                    }
                    uint64_t missing = 0;
                    if (expected_round > seq_diff) {
                        uint64_t tolerance = expected_round > 5 ? expected_round / 5 : 1;
                        if (expected_round > seq_diff + tolerance) {
                            missing = expected_round - seq_diff;
                        }
                    }
                    
                    if (missing > 0) {
                        total_dropped += missing;
                        last_gap = static_cast<uint32_t>(missing);
                    } else {
                        last_gap = 0;
                    }
                }
            } else {
                last_interval_ms = 0.0;
                frequency_hz = expected_frequency_hz;
                last_gap = 0;
            }
            last_samples_window = samples_in_window;
            last_sequence_raw = data.sequence;
            last_timestamp_ns = data.timestamp_ns;
            last_sample = data;
            has_prev = true;
        } else if (!has_sample) {
            last_gap = 0;
            samples_in_window = last_samples_window;
        }
        
        root["stats"]["sequence_gap"] = static_cast<Json::UInt>(last_gap);
        root["stats"]["dropped_total"] = static_cast<Json::UInt64>(total_dropped);
        root["stats"]["interval_ms"] = last_interval_ms;
        root["stats"]["frequency_hz"] = frequency_hz;
        root["stats"]["samples_window"] = static_cast<Json::UInt64>(samples_in_window);
        root["stats"]["samples_per_second"] = frequency_hz;
        root["stats"]["expected_frequency_hz"] = expected_frequency_hz;
        
        // 内存状态
        root["ring_buffer"]["size"] = ring ? ring->size() : 0;
        root["ring_buffer"]["capacity"] = RING_SIZE;
        
        // 配置信息
        root["config"]["rs485"]["poll_rate_ms"] = rs485_cfg.poll_rate_ms;
        root["config"]["rs485"]["baudrate"] = rs485_cfg.baudrate;
        root["config"]["rs485"]["simulate"] = rs485_cfg.simulate;
        root["config"]["rs485"]["target_frequency_hz"] = expected_frequency_hz;
        
        Json::Value modbus_conf(Json::objectValue);
        modbus_conf["listen_ip"] = modbus_cfg.listen_ip;
        modbus_conf["port"] = modbus_cfg.port;
        modbus_conf["slave_id"] = modbus_cfg.slave_id;
        modbus_conf["enabled"] = modbus_cfg.enabled;
        root["config"]["modbus"] = modbus_conf;
        
        Json::Value s7_conf(Json::objectValue);
        s7_conf["enabled"] = s7_cfg.enabled;
        s7_conf["plc_ip"] = s7_cfg.plc_ip;
        s7_conf["rack"] = s7_cfg.rack;
        s7_conf["slot"] = s7_cfg.slot;
        s7_conf["db_number"] = s7_cfg.db_number;
        s7_conf["update_interval_ms"] = s7_cfg.update_interval_ms;
        
        Json::Value opcua_conf(Json::objectValue);
        opcua_conf["enabled"] = opcua_cfg.enabled;
        opcua_conf["server_url"] = opcua_cfg.server_url;
        opcua_conf["security_mode"] = opcua_cfg.security_mode;
        opcua_conf["username"] = opcua_cfg.username;
        opcua_conf["password"] = opcua_cfg.password;
        
        root["config"]["protocol"]["active"] = active_protocol;
        root["config"]["protocol"]["modbus"] = modbus_conf;
        root["config"]["protocol"]["s7"] = s7_conf;
        root["config"]["protocol"]["opcua"] = opcua_conf;
        
        Json::Value protocol_stats(Json::objectValue);
        protocol_stats["modbus"] = read_component_status("modbus");
        protocol_stats["s7"] = read_component_status("s7");
        protocol_stats["opcua"] = read_component_status("opcua");
        root["protocol_stats"] = protocol_stats;
        
        return build_json_response("200 OK", root);
    }
    
    std::string handle_get_config() {
        ConfigManager& config = ConfigManager::instance();
        
        return build_json_response("200 OK", config.get_config());
    }
    
    std::string handle_post_config(const std::string& request) {
        auto body_pos = request.find("\r\n\r\n");
        std::string body = (body_pos == std::string::npos) ? "" : request.substr(body_pos + 4);
        
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value payload;
        std::string errs;
        
        if (!reader->parse(body.data(), body.data() + body.size(), &payload, &errs)) {
            Json::Value resp;
            resp["success"] = false;
            resp["message"] = "JSON 解析失败: " + errs;
            return build_json_response("400 Bad Request", resp);
        }
        
        ConfigManager& config = ConfigManager::instance();
        bool changed = false;
        
        if (payload.isMember("rs485") && payload["rs485"].isObject()) {
            const Json::Value& rs = payload["rs485"];
            if (rs.isMember("poll_rate_ms") && rs["poll_rate_ms"].isInt()) {
                config.set("rs485.poll_rate_ms", Json::Value(rs["poll_rate_ms"].asInt()));
                changed = true;
            }
            if (rs.isMember("baudrate") && rs["baudrate"].isInt()) {
                config.set("rs485.baudrate", Json::Value(rs["baudrate"].asInt()));
                changed = true;
            }
            if (rs.isMember("simulate") && rs["simulate"].isBool()) {
                config.set("rs485.simulate", Json::Value(rs["simulate"].asBool()));
                changed = true;
            }
        }
        
        if (payload.isMember("protocol") && payload["protocol"].isObject()) {
            const Json::Value& protocol = payload["protocol"];
            
            if (protocol.isMember("active") && protocol["active"].isString()) {
                config.set("protocol.active", Json::Value(protocol["active"].asString()));
                changed = true;
            }
            
            if (protocol.isMember("modbus") && protocol["modbus"].isObject()) {
                const Json::Value& mb = protocol["modbus"];
                if (mb.isMember("listen_ip") && mb["listen_ip"].isString()) {
                    config.set("protocol.modbus.listen_ip", Json::Value(mb["listen_ip"].asString()));
                    changed = true;
                }
                if (mb.isMember("port") && mb["port"].isInt()) {
                    config.set("protocol.modbus.port", Json::Value(mb["port"].asInt()));
                    changed = true;
                }
                if (mb.isMember("slave_id") && mb["slave_id"].isInt()) {
                    config.set("protocol.modbus.slave_id", Json::Value(mb["slave_id"].asInt()));
                    changed = true;
                }
                if (mb.isMember("enabled") && mb["enabled"].isBool()) {
                    config.set("protocol.modbus.enabled", Json::Value(mb["enabled"].asBool()));
                    changed = true;
                }
            }
            
            if (protocol.isMember("s7") && protocol["s7"].isObject()) {
                const Json::Value& s7 = protocol["s7"];
                if (s7.isMember("enabled") && s7["enabled"].isBool()) {
                    config.set("protocol.s7.enabled", Json::Value(s7["enabled"].asBool()));
                    changed = true;
                }
                if (s7.isMember("plc_ip") && s7["plc_ip"].isString()) {
                    config.set("protocol.s7.plc_ip", Json::Value(s7["plc_ip"].asString()));
                    changed = true;
                }
                if (s7.isMember("rack") && s7["rack"].isInt()) {
                    config.set("protocol.s7.rack", Json::Value(s7["rack"].asInt()));
                    changed = true;
                }
                if (s7.isMember("slot") && s7["slot"].isInt()) {
                    config.set("protocol.s7.slot", Json::Value(s7["slot"].asInt()));
                    changed = true;
                }
                if (s7.isMember("db_number") && s7["db_number"].isInt()) {
                    config.set("protocol.s7.db_number", Json::Value(s7["db_number"].asInt()));
                    changed = true;
                }
                if (s7.isMember("update_interval_ms") && s7["update_interval_ms"].isInt()) {
                    config.set("protocol.s7.update_interval_ms", Json::Value(s7["update_interval_ms"].asInt()));
                    changed = true;
                }
            }
            
            if (protocol.isMember("opcua") && protocol["opcua"].isObject()) {
                const Json::Value& opc = protocol["opcua"];
                if (opc.isMember("enabled") && opc["enabled"].isBool()) {
                    config.set("protocol.opcua.enabled", Json::Value(opc["enabled"].asBool()));
                    changed = true;
                }
                if (opc.isMember("server_url") && opc["server_url"].isString()) {
                    config.set("protocol.opcua.server_url", Json::Value(opc["server_url"].asString()));
                    changed = true;
                }
                if (opc.isMember("security_mode") && opc["security_mode"].isString()) {
                    config.set("protocol.opcua.security_mode", Json::Value(opc["security_mode"].asString()));
                    changed = true;
                }
                if (opc.isMember("username") && opc["username"].isString()) {
                    config.set("protocol.opcua.username", Json::Value(opc["username"].asString()));
                    changed = true;
                }
                if (opc.isMember("password") && opc["password"].isString()) {
                    config.set("protocol.opcua.password", Json::Value(opc["password"].asString()));
                    changed = true;
                }
            }
        }
        
        Json::Value resp;
        resp["success"] = true;
        resp["message"] = changed ? "配置已更新" : "未检测到可更新的字段";
        
        if (changed) {
            if (!config.save(config_path_)) {
                resp["success"] = false;
                resp["message"] = "配置保存失败";
                return build_json_response("500 Internal Server Error", resp);
            }
        }
        
        resp["config"] = config.get_config();
        return build_json_response("200 OK", resp);
    }
    
    Json::Value read_component_status(const std::string& component) const {
        namespace fs = std::filesystem;
        fs::path path = fs::path("/tmp/gw-test") / ("status_" + component + ".json");
        if (!fs::exists(path)) {
            return Json::Value(Json::nullValue);
        }
        
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return Json::Value(Json::nullValue);
        }
        
        Json::CharReaderBuilder builder;
        Json::Value value;
        std::string errs;
        if (!Json::parseFromStream(builder, ifs, &value, &errs)) {
            return Json::Value(Json::nullValue);
        }
        return value;
    }
    
    std::string handle_index() {
        extern const char* index_html; // 在后面定义
        
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n";
        oss << "Content-Type: text/html\r\n";
        oss << "Content-Length: " << strlen(index_html) << "\r\n";
        oss << "\r\n";
        oss << index_html;
        
        return oss.str();
    }
    
    std::string handle_404() {
        const char* html = "<html><body><h1>404 Not Found</h1></body></html>";
        
        std::ostringstream oss;
        oss << "HTTP/1.1 404 Not Found\r\n";
        oss << "Content-Type: text/html\r\n";
        oss << "Content-Length: " << strlen(html) << "\r\n";
        oss << "\r\n";
        oss << html;
        
        return oss.str();
    }
    
    int port_;
    std::string config_path_;
    int server_fd_;
};

// 嵌入式 HTML 页面（简化版）
const char* index_html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>工业网关监控中心</title>
    <style>
        :root {
            color-scheme: light dark;
        }
        body {
            font-family: "Helvetica Neue", Arial, sans-serif;
            margin: 0;
            padding: 0;
            background: #f4f6f8;
            color: #1f2328;
        }
        .container {
            max-width: 1280px;
            margin: 0 auto;
            padding: 24px 20px 40px;
        }
        h1 {
            font-size: 28px;
            margin: 0 0 12px;
            color: #1f2933;
        }
        h2 {
            margin: 0 0 16px;
            font-size: 20px;
            color: #1f2933;
        }
        h3 {
            margin: 0;
            font-size: 18px;
        }
        .section {
            background: #ffffff;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 12px rgba(15, 23, 42, 0.08);
        }
        .grid-two {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 12px 16px;
        }
        .data-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px 14px;
            border: 1px solid #e5e8ec;
            border-radius: 8px;
            background: #fafbfc;
            gap: 16px;
        }
        .data-row .label {
            font-weight: 600;
            color: #46505c;
        }
        .data-row .value {
            font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
            color: #1471f9;
            text-align: right;
            flex: 1;
        }
        .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
            background: #d1d5db;
        }
        .status-indicator.is-on {
            background: #16a34a;
        }
        .status-indicator.is-off {
            background: #f97316;
        }
        .protocol-grid {
            display: grid;
            gap: 16px;
            grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
        }
        .protocol-card {
            border: 1px solid #e5e8ec;
            border-radius: 12px;
            padding: 18px;
            background: #fff;
            display: flex;
            flex-direction: column;
            gap: 12px;
            transition: border-color 0.2s ease, box-shadow 0.2s ease;
        }
        .protocol-card.is-selected {
            border-color: #2563eb;
            box-shadow: 0 6px 18px rgba(37, 99, 235, 0.12);
        }
        .protocol-card__header {
            display: flex;
            justify-content: space-between;
            align-items: flex-start;
            gap: 12px;
        }
        .protocol-card__subtitle {
            margin: 6px 0 0;
            font-size: 13px;
            color: #6b7686;
        }
        .protocol-indicator {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            font-size: 13px;
            padding: 4px 10px;
            border-radius: 999px;
            background: #e5e7eb;
            color: #374151;
        }
        .protocol-indicator.is-active {
            background: rgba(22, 163, 74, 0.12);
            color: #15803d;
        }
        .protocol-indicator.is-warning {
            background: rgba(234, 179, 8, 0.18);
            color: #b45309;
        }
        .protocol-card__metrics {
            display: grid;
            gap: 6px;
            font-size: 13px;
            color: #4b5563;
        }
        .protocol-card__metrics strong {
            font-weight: 600;
            color: #111827;
        }
        .protocol-card__actions {
            display: flex;
            gap: 10px;
            margin-top: auto;
        }
        .protocol-card__actions button {
            flex: 1;
        }
        .chart-wrapper {
            background: #ffffff;
            border: 1px solid #e5e8ec;
            border-radius: 12px;
            padding: 16px;
        }
        canvas {
            width: 100%;
            height: 240px;
        }
        .chart-legend {
            margin-top: 10px;
            font-size: 12px;
            color: #657080;
        }
        .config-form {
            display: grid;
            gap: 20px;
        }
        fieldset {
            border: 1px solid #d6dae1;
            border-radius: 10px;
            padding: 16px;
        }
        fieldset legend {
            padding: 0 8px;
            font-weight: 600;
        }
        .form-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 14px 16px;
        }
        label {
            font-size: 13px;
            color: #374151;
            display: flex;
            flex-direction: column;
            gap: 6px;
        }
        input, select {
            font-size: 14px;
            padding: 8px 10px;
            border: 1px solid #d0d6dd;
            border-radius: 6px;
            background: #ffffff;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #2563eb;
            box-shadow: 0 0 0 2px rgba(37, 99, 235, 0.15);
        }
        .action-row {
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            gap: 12px;
        }
        .btn-primary, .btn-outline {
            border: none;
            border-radius: 6px;
            padding: 10px 16px;
            font-size: 14px;
            cursor: pointer;
            transition: background 0.2s ease, color 0.2s ease;
        }
        .btn-primary {
            background: #2563eb;
            color: #ffffff;
        }
        .btn-primary:disabled {
            background: #94a3b8;
            cursor: not-allowed;
        }
        .btn-outline {
            background: transparent;
            color: #1f2933;
            border: 1px solid #cbd5f0;
        }
        .btn-outline:hover {
            background: #e2e8f6;
        }
        .status-text {
            font-size: 13px;
            color: #475569;
        }
        .status-text.error {
            color: #dc2626;
        }
        @media (max-width: 768px) {
            .container {
                padding: 18px 14px 32px;
            }
            .data-row {
                flex-direction: column;
                align-items: flex-start;
            }
            .data-row .value {
                text-align: left;
            }
            .protocol-card__actions {
                flex-direction: column;
            }
        }
        /* 输入框、下拉框、文本域：强制黑色文字、白色背景、黑色光标 */
        input, select, textarea {
            color: #111827 !important;          /* 文字颜色（几乎黑） */
            background-color: #ffffff !important;/* 背景白色，避免暗色模式下反色 */
            caret-color: #111827;                /* 输入光标颜色 */
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔧 RS485工业网关监控设置中心</h1>
        
        <div class="section">
            <h2>系统运行状态</h2>
            <div class="grid-two">
                <div class="data-row">
                    <span class="label">运行状态</span>
                    <span class="value"><span id="status-indicator" class="status-indicator"></span><span id="running-status">加载中...</span></span>
                </div>
                <div class="data-row">
                    <span class="label">运行时长</span>
                    <span class="value" id="uptime">加载中...</span>
                </div>
                <div class="data-row">
                    <span class="label">当前厚度</span>
                    <span class="value" id="thickness">等待数据...</span>
                </div>
                <div class="data-row">
                    <span class="label">数据序列</span>
                    <span class="value" id="sequence">等待数据...</span>
                </div>
                <div class="data-row">
                    <span class="label">数据状态</span>
                    <span class="value" id="data-status">等待数据...</span>
                </div>
                <div class="data-row">
                    <span class="label">数据延迟</span>
                    <span class="value" id="data-timestamp">等待数据...</span>
                </div>
            </div>
        </div>
        
        <div class="section">
            <h2>协议转发概览</h2>
            <div class="protocol-grid">
                <div class="protocol-card" data-protocol-card="modbus">
                    <div class="protocol-card__header">
                        <div>
                            <h3>Modbus TCP</h3>
                            <p data-role="status-text" class="protocol-card__subtitle">状态待更新</p>
                        </div>
                        <span class="protocol-indicator" data-role="indicator">--</span>
                    </div>
                    <div class="protocol-card__metrics">
                        <div data-role="summary">等待数据...</div>
                        <div data-role="meta">服务器: --</div>
                        <div data-role="last-update">更新时间: --</div>
                    </div>
                    <div class="protocol-card__actions">
                        <button class="btn-primary" data-set-active="modbus">切换至 Modbus</button>
                    </div>
                </div>
                <div class="protocol-card" data-protocol-card="s7">
                    <div class="protocol-card__header">
                        <div>
                            <h3>S7 模拟</h3>
                            <p data-role="status-text" class="protocol-card__subtitle">状态待更新</p>
                        </div>
                        <span class="protocol-indicator" data-role="indicator">--</span>
                    </div>
                    <div class="protocol-card__metrics">
                        <div data-role="summary">等待数据...</div>
                        <div data-role="meta">PLC: --</div>
                        <div data-role="last-update">更新时间: --</div>
                    </div>
                        <div class="protocol-card__actions">
                        <button class="btn-primary" data-set-active="s7">切换至 S7</button>
                    </div>
                </div>
                <div class="protocol-card" data-protocol-card="opcua">
                    <div class="protocol-card__header">
                        <div>
                            <h3>OPC UA 模拟</h3>
                            <p data-role="status-text" class="protocol-card__subtitle">状态待更新</p>
                        </div>
                        <span class="protocol-indicator" data-role="indicator">--</span>
                    </div>
                    <div class="protocol-card__metrics">
                        <div data-role="summary">等待数据...</div>
                        <div data-role="meta">服务器: --</div>
                        <div data-role="last-update">更新时间: --</div>
                    </div>
                    <div class="protocol-card__actions">
                        <button class="btn-primary" data-set-active="opcua">切换至 OPC UA</button>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="section">
            <h2>通信统计</h2>
            <div class="grid-two">
                <div class="data-row">
                    <span class="label">采样周期</span>
                    <span class="value" id="poll-rate">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">目标频率</span>
                    <span class="value" id="target-frequency">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">实时频率</span>
                    <span class="value" id="actual-frequency">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">每秒数据包</span>
                    <span class="value" id="packets-rate">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">平均采样间隔</span>
                    <span class="value" id="interval-ms">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">串口波特率</span>
                    <span class="value" id="baudrate">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">环形缓冲区</span>
                    <span class="value" id="buffer-usage">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">窗口样本数</span>
                    <span class="value" id="samples-window">0</span>
                </div>
                <div class="data-row">
                    <span class="label">窗口缺包</span>
                    <span class="value" id="sequence-gap">0</span>
                </div>
                <div class="data-row">
                    <span class="label">累计缺包</span>
                    <span class="value" id="drop-total">0</span>
                </div>
                <div class="data-row">
                    <span class="label">Modbus TCP</span>
                    <span class="value" id="modbus-config">计算中...</span>
                </div>
                <div class="data-row">
                    <span class="label">模拟模式</span>
                    <span class="value" id="simulate-flag">计算中...</span>
                </div>
            </div>
        </div>
        
        <div class="section">
            <h2>实时监控</h2>
            <div class="chart-wrapper">
                <canvas id="latency-chart"></canvas>
                <div class="chart-legend">蓝线：数据延迟 (毫秒) · 橙线：缺包数 (×20) · 紫线：每秒样本数</div>
            </div>
            <div class="chart-wrapper" style="margin-top:16px;">
                <canvas id="thickness-chart"></canvas>
                <div class="chart-legend">绿色曲线：厚度值 (毫米)</div>
            </div>
        </div>
        
        <div class="section">
            <h2>配置概览</h2>
            <button class="btn-outline" onclick="loadConfig()">刷新配置</button>
            <button class="btn-outline" onclick="reloadServices()">重载服务</button>
            <pre id="config-display" style="background:#111827; color:#f8fafc; padding:16px; border-radius:10px; overflow:auto; margin-top:12px;">点击“刷新配置”以查看当前配置</pre>
        </div>
        
        <div class="section">
            <h2>参数调整</h2>
            <form id="config-form" class="config-form" onsubmit="submitConfig(event)">
                <fieldset>
                    <legend>总览</legend>
                    <div class="form-grid">
                        <label>传输协议
                            <select id="form-active-protocol" required></select>
                        </label>
                    </div>
                </fieldset>
                <fieldset>
                    <legend>RS485 采集</legend>
                    <div class="form-grid">
                        <label>采样周期 (ms)
                            <input type="number" id="form-poll-rate" min="5" max="1000" step="1" required>
                        </label>
                        <label>串口波特率
                            <select id="form-baudrate"></select>
                        </label>
                        <label>模拟模式
                            <select id="form-simulate">
                                <option value="false">关闭</option>
                                <option value="true">开启</option>
                            </select>
                        </label>
                    </div>
                </fieldset>
                <fieldset>
                    <legend>Modbus TCP</legend>
                    <div class="form-grid">
                        <label>启用
                            <select id="form-modbus-enabled">
                                <option value="true">启用</option>
                                <option value="false">禁用</option>
                            </select>
                        </label>
                        <label>监听地址
                            <input type="text" id="form-modbus-ip" required>
                        </label>
                        <label>端口
                            <input type="number" id="form-modbus-port" min="1" max="65535" required>
                        </label>
                        <label>站号
                            <input type="number" id="form-modbus-slave" min="1" max="247" required>
                        </label>
                    </div>
                </fieldset>
                <fieldset>
                    <legend>S7 模拟</legend>
                    <div class="form-grid">
                        <label>启用
                            <select id="form-s7-enabled">
                                <option value="true">启用</option>
                                <option value="false">禁用</option>
                            </select>
                        </label>
                        <label>PLC IP
                            <input type="text" id="form-s7-ip" required>
                        </label>
                        <label>Rack
                            <input type="number" id="form-s7-rack" min="0" max="7" required>
                        </label>
                        <label>Slot
                            <input type="number" id="form-s7-slot" min="0" max="7" required>
                        </label>
                        <label>DB 编号
                            <input type="number" id="form-s7-db" min="1" required>
                        </label>
                        <label>刷新间隔 (ms)
                            <input type="number" id="form-s7-interval" min="10" required>
                        </label>
                    </div>
                </fieldset>
                <fieldset>
                    <legend>OPC UA 模拟</legend>
                    <div class="form-grid">
                        <label>启用
                            <select id="form-opcua-enabled">
                                <option value="true">启用</option>
                                <option value="false">禁用</option>
                            </select>
                        </label>
                        <label>服务器 URL
                            <input type="text" id="form-opcua-url" required>
                        </label>
                        <label>安全模式
                            <select id="form-opcua-security"></select>
                        </label>
                        <label>用户名
                            <input type="text" id="form-opcua-username">
                        </label>
                        <label>密码
                            <input type="password" id="form-opcua-password">
                        </label>
                    </div>
                </fieldset>
                <div class="action-row">
                    <button type="submit" class="btn-primary">保存配置</button>
                    <button type="button" class="btn-outline" onclick="resetConfigForm()">恢复已加载配置</button>
                    <span id="config-save-status" class="status-text"></span>
                </div>
            </form>
        </div>
    </div>
    
    <script>
        const STATUS_FLAGS = {
            0x0001: "数据有效",
            0x0002: "RS-485 通信正常",
            0x0004: "CRC 校验通过",
            0x0008: "传感器正常"
        };
        const PROTOCOLS = ["modbus", "s7", "opcua"];
        const PROTOCOL_LABELS = {
            modbus: "Modbus TCP",
            s7: "S7 模拟",
            opcua: "OPC UA 模拟"
        };
        const BAUD_RATES = [9600, 19200, 38400, 57600, 115200];
        const SECURITY_MODES = ["None", "Sign", "SignAndEncrypt"];
        const CHART_POINTS = 300;
        const DROP_SCALE = 20;
        const chartData = {
            timestamps: [],
            latency: [],
            drops: [],
            thickness: [],
            sampleRate: []
        };
        let lastConfig = null;
        const lastStatsCache = {
            frequency: null,
            interval: null,
            rate: null,
            samplesWindow: 0
        };
        
        function formatDuration(seconds) {
            if (!Number.isFinite(seconds)) return "未知";
            const total = Math.max(0, Math.floor(seconds));
            const hours = Math.floor(total / 3600);
            const minutes = Math.floor((total % 3600) / 60);
            const secs = total % 60;
            const parts = [];
            if (hours > 0) parts.push(`${hours} 小时`);
            if (minutes > 0) parts.push(`${minutes} 分`);
            if (hours === 0 && minutes === 0) {
                parts.push(`${secs} 秒`);
            } else if (secs > 0) {
                parts.push(`${secs} 秒`);
            }
            return parts.join('') || '0 秒';
        }
        
        function formatAge(ageMs) {
            if (!Number.isFinite(ageMs)) return "未知";
            if (ageMs < 1000) return `${Math.round(ageMs)} 毫秒前`;
            const seconds = ageMs / 1000;
            if (seconds < 60) return `${seconds.toFixed(1)} 秒前`;
            const minutes = Math.floor(seconds / 60);
            const remainder = Math.floor(seconds % 60);
            return `${minutes} 分 ${remainder} 秒前`;
        }
        
        function describeStatus(status) {
            if (typeof status !== "number") return "等待数据...";
            const ok = [];
            for (const [mask, label] of Object.entries(STATUS_FLAGS)) {
                if ((status & Number(mask)) !== 0) ok.push(label);
            }
            const errorCode = status & 0xFF00;
            let extra = ok.length ? ok.join("，") : "无有效标志";
            if (errorCode) {
                extra += `；错误代码 0x${((errorCode >> 8) & 0xFF).toString(16).padStart(2, "0").toUpperCase()}`;
            }
            return extra;
        }
        
        function formatInterval(value) {
            return Number.isFinite(value) && value > 0 ? `${value.toFixed(1)} 毫秒` : '计算中...';
        }
        
        function formatFrequency(value) {
            return Number.isFinite(value) && value > 0 ? `${value.toFixed(1)} Hz` : '计算中...';
        }
        
        function formatRate(value) {
            return Number.isFinite(value) && value > 0 ? `${value.toFixed(1)} 包/秒` : '计算中...';
        }
        
        setInterval(loadStatus, 1000);
        loadStatus();
        initializeFormControls();
        setupProtocolButtons();
        setConfigStatus('');
        loadConfig();
        
        function loadStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    const running = Boolean(data.running);
                    const indicator = document.getElementById('status-indicator');
                    indicator.classList.toggle('is-on', running);
                    indicator.classList.toggle('is-off', !running);
                    document.getElementById('running-status').textContent = running ? '运行中' : '已停止';
                    document.getElementById('uptime').textContent = formatDuration(data.uptime_seconds);
                    const stats = data.stats || {};
                    
                    if (data.current_data) {
                        document.getElementById('data-status').textContent = describeStatus(data.current_data.status);
                        if (typeof data.current_data.age_ms === 'number') {
                            document.getElementById('data-timestamp').textContent = formatAge(data.current_data.age_ms);
                        } else {
                            document.getElementById('data-timestamp').textContent = '时间信息不可用';
                        }
                        document.getElementById('thickness').textContent = data.current_data.thickness_mm.toFixed(3) + ' 毫米';
                        document.getElementById('sequence').textContent = `#${data.current_data.sequence}`;
                        const rateForChart = (Number.isFinite(stats.samples_per_second) && stats.samples_per_second > 0)
                            ? stats.samples_per_second
                            : (lastStatsCache.rate ?? 0);
                        updateCharts(
                            typeof data.current_data.age_ms === 'number' ? data.current_data.age_ms : 0,
                            stats.sequence_gap ?? 0,
                            data.current_data.thickness_mm,
                            rateForChart
                        );
                    } else {
                        document.getElementById('data-status').textContent = '尚未收到测量数据';
                        document.getElementById('data-timestamp').textContent = '等待数据...';
                        document.getElementById('thickness').textContent = '暂无数据';
                        document.getElementById('sequence').textContent = '暂无数据';
                        updateCharts(0, 0, null, lastStatsCache.rate ?? 0);
                    }
                    
                    if (data.ring_buffer) {
                        const size = data.ring_buffer.size;
                        const capacity = data.ring_buffer.capacity;
                        const percent = capacity > 0 ? ((size / capacity) * 100).toFixed(1) : '0.0';
                        document.getElementById('buffer-usage').textContent = `${size} / ${capacity} （${percent}%）`;
                    } else {
                        document.getElementById('buffer-usage').textContent = '无法获取缓冲区信息';
                    }
                    
                    document.getElementById('samples-window').textContent = stats.samples_window ?? lastStatsCache.samplesWindow ?? 0;
                    document.getElementById('sequence-gap').textContent = stats.sequence_gap ?? 0;
                    document.getElementById('drop-total').textContent = stats.dropped_total ?? 0;
                    const intervalVal = (Number.isFinite(stats.interval_ms) && stats.interval_ms > 0)
                        ? stats.interval_ms
                        : lastStatsCache.interval;
                    document.getElementById('interval-ms').textContent = formatInterval(intervalVal);
                    if (Number.isFinite(stats.interval_ms) && stats.interval_ms > 0) {
                        lastStatsCache.interval = stats.interval_ms;
                    }
                    const frequencyVal = Number.isFinite(stats.frequency_hz) && stats.frequency_hz > 0
                        ? stats.frequency_hz
                        : lastStatsCache.frequency;
                    document.getElementById('actual-frequency').textContent = formatFrequency(frequencyVal);
                    if (Number.isFinite(stats.frequency_hz) && stats.frequency_hz > 0) {
                        lastStatsCache.frequency = stats.frequency_hz;
                    }
                    const rateVal = Number.isFinite(stats.samples_per_second) && stats.samples_per_second > 0
                        ? stats.samples_per_second
                        : lastStatsCache.rate;
                    document.getElementById('packets-rate').textContent = formatRate(rateVal);
                    if (Number.isFinite(stats.samples_per_second) && stats.samples_per_second > 0) {
                        lastStatsCache.rate = stats.samples_per_second;
                    }
                    lastStatsCache.samplesWindow = stats.samples_window ?? lastStatsCache.samplesWindow ?? 0;
                    
                    let targetFreq = 0;
                    if (Number.isFinite(stats.expected_frequency_hz)) {
                        targetFreq = stats.expected_frequency_hz;
                    } else if (data.config && data.config.protocol && data.config.protocol.modbus) {
                        targetFreq = data.config.rs485 ? data.config.rs485.target_frequency_hz : 0;
                    }
                    document.getElementById('target-frequency').textContent = formatFrequency(targetFreq);
                    
                    if (data.config && data.config.rs485) {
                        document.getElementById('poll-rate').textContent = `${data.config.rs485.poll_rate_ms} 毫秒`;
                        document.getElementById('baudrate').textContent = `${data.config.rs485.baudrate} bps`;
                        document.getElementById('simulate-flag').textContent = data.config.rs485.simulate ? '开启' : '关闭';
                    }
                    
                    if (data.config && data.config.modbus) {
                        const cfg = data.config.modbus;
                        document.getElementById('modbus-config').textContent = `${cfg.enabled ? '启用' : '禁用'} - ${cfg.listen_ip}:${cfg.port} (站号 ${cfg.slave_id})`;
                    }
                    
                    updateProtocolCards(data);
                })
                .catch(err => {
                    console.error('获取状态失败:', err);
                    const indicator = document.getElementById('status-indicator');
                    indicator.classList.remove('is-on');
                    indicator.classList.add('is-off');
                    document.getElementById('running-status').textContent = '通信中断';
                });
        }
        
        function loadConfig() {
            fetch('/api/config')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('config-display').textContent = JSON.stringify(data, null, 2);
                    lastConfig = data;
                    populateConfigForm(data);
                    setConfigStatus('配置已刷新');
                })
                .catch(err => {
                    console.error('获取配置失败:', err);
                    document.getElementById('config-display').textContent = `获取配置失败：${err.message}`;
                    setConfigStatus('获取配置失败：' + err.message, true);
                });
        }
        
        function reloadServices() {
            alert('服务重载功能即将上线');
        }
        
        function initializeFormControls() {
            const baudSelect = document.getElementById('form-baudrate');
            baudSelect.innerHTML = '';
            BAUD_RATES.forEach(rate => {
                const option = document.createElement('option');
                option.value = rate;
                option.textContent = `${rate} bps`;
                baudSelect.appendChild(option);
            });
            
            const protoSelect = document.getElementById('form-active-protocol');
            protoSelect.innerHTML = '';
            PROTOCOLS.forEach(proto => {
                const option = document.createElement('option');
                option.value = proto;
                option.textContent = PROTOCOL_LABELS[proto];
                protoSelect.appendChild(option);
            });
            
            const securitySelect = document.getElementById('form-opcua-security');
            securitySelect.innerHTML = '';
            SECURITY_MODES.forEach(mode => {
                const option = document.createElement('option');
                option.value = mode;
                option.textContent = mode;
                securitySelect.appendChild(option);
            });
        }
        
        function setupProtocolButtons() {
            document.querySelectorAll('[data-set-active]').forEach(btn => {
                btn.addEventListener('click', () => {
                    const proto = btn.getAttribute('data-set-active');
                    setActiveProtocol(proto);
                });
            });
        }
        
        function setActiveProtocol(protocol) {
            setConfigStatus(`正在切换至 ${PROTOCOL_LABELS[protocol] || protocol}...`);
            fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ protocol: { active: protocol } })
            }).then(async response => {
                const data = await response.json();
                if (!response.ok || data.success === false) {
                    throw new Error(data.message || '未知错误');
                }
                setConfigStatus('协议切换成功');
                loadConfig();
                loadStatus();
            }).catch(err => {
                console.error('切换协议失败:', err);
                setConfigStatus('切换协议失败：' + err.message, true);
            });
        }
        
        function populateConfigForm(config) {
            if (!config) return;
            const protoConfig = config.protocol || {};
            document.getElementById('form-active-protocol').value = protoConfig.active || 'modbus';
            
            const rs485 = config.rs485 || {};
            document.getElementById('form-poll-rate').value = rs485.poll_rate_ms ?? 10;
            document.getElementById('form-baudrate').value = rs485.baudrate ?? 19200;
            document.getElementById('form-simulate').value = rs485.simulate ? 'true' : 'false';
            
            const modbus = (protoConfig.modbus || config.modbus) || {};
            document.getElementById('form-modbus-enabled').value = modbus.enabled ? 'true' : 'false';
            document.getElementById('form-modbus-ip').value = modbus.listen_ip ?? '0.0.0.0';
            document.getElementById('form-modbus-port').value = modbus.port ?? 502;
            document.getElementById('form-modbus-slave').value = modbus.slave_id ?? 1;
            
            const s7 = protoConfig.s7 || {};
            document.getElementById('form-s7-enabled').value = s7.enabled ? 'true' : 'false';
            document.getElementById('form-s7-ip').value = s7.plc_ip ?? '192.168.1.10';
            document.getElementById('form-s7-rack').value = s7.rack ?? 0;
            document.getElementById('form-s7-slot').value = s7.slot ?? 1;
            document.getElementById('form-s7-db').value = s7.db_number ?? 10;
            document.getElementById('form-s7-interval').value = s7.update_interval_ms ?? 50;
            
            const opcua = protoConfig.opcua || {};
            document.getElementById('form-opcua-enabled').value = opcua.enabled ? 'true' : 'false';
            document.getElementById('form-opcua-url').value = opcua.server_url ?? 'opc.tcp://localhost:4840';
            document.getElementById('form-opcua-security').value = opcua.security_mode ?? 'None';
            document.getElementById('form-opcua-username').value = opcua.username ?? '';
            document.getElementById('form-opcua-password').value = opcua.password ?? '';
        }
        
        function resetConfigForm() {
            populateConfigForm(lastConfig);
            setConfigStatus('已恢复为已加载配置');
        }
        
        function submitConfig(event) {
            event.preventDefault();
            const payload = {
                protocol: {
                    active: document.getElementById('form-active-protocol').value,
                    modbus: {
                        listen_ip: document.getElementById('form-modbus-ip').value.trim(),
                        port: Number(document.getElementById('form-modbus-port').value),
                        slave_id: Number(document.getElementById('form-modbus-slave').value),
                        enabled: document.getElementById('form-modbus-enabled').value === 'true'
                    },
                    s7: {
                        enabled: document.getElementById('form-s7-enabled').value === 'true',
                        plc_ip: document.getElementById('form-s7-ip').value.trim(),
                        rack: Number(document.getElementById('form-s7-rack').value),
                        slot: Number(document.getElementById('form-s7-slot').value),
                        db_number: Number(document.getElementById('form-s7-db').value),
                        update_interval_ms: Number(document.getElementById('form-s7-interval').value)
                    },
                    opcua: {
                        enabled: document.getElementById('form-opcua-enabled').value === 'true',
                        server_url: document.getElementById('form-opcua-url').value.trim(),
                        security_mode: document.getElementById('form-opcua-security').value,
                        username: document.getElementById('form-opcua-username').value.trim(),
                        password: document.getElementById('form-opcua-password').value
                    }
                },
                rs485: {
                    poll_rate_ms: Number(document.getElementById('form-poll-rate').value),
                    baudrate: Number(document.getElementById('form-baudrate').value),
                    simulate: document.getElementById('form-simulate').value === 'true'
                }
            };
            
            fetch('/api/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            }).then(async response => {
                const data = await response.json();
                if (!response.ok || data.success === false) {
                    throw new Error(data.message || '未知错误');
                }
                lastConfig = data.config;
                populateConfigForm(lastConfig);
                setConfigStatus(data.message || '配置已更新');
                loadStatus();
                return data;
            }).catch(err => {
                console.error('保存配置失败:', err);
                setConfigStatus('保存失败：' + err.message, true);
            });
        }
        
        function setConfigStatus(message, isError = false) {
            const el = document.getElementById('config-save-status');
            if (!el) return;
            el.textContent = message || '';
            el.classList.toggle('error', Boolean(isError));
        }
        
        function updateProtocolCards(data) {
            const stats = data.protocol_stats || {};
            const protoConfig = data.config && data.config.protocol ? data.config.protocol : {};
            const active = protoConfig.active || 'modbus';
            PROTOCOLS.forEach(proto => {
                const card = document.querySelector(`[data-protocol-card="${proto}"]`);
                if (!card) return;
                const info = stats[proto] || null;
                const configDetail = protoConfig[proto] || {};
                const button = card.querySelector('[data-set-active]');
                const indicator = card.querySelector('[data-role="indicator"]');
                const summary = card.querySelector('[data-role="summary"]');
                const meta = card.querySelector('[data-role="meta"]');
                const lastUpdate = card.querySelector('[data-role="last-update"]');
                const statusText = card.querySelector('[data-role="status-text"]');
                const isActive = active === proto;
                const workerActive = info && info.active === true;
                
                card.classList.toggle('is-selected', isActive);
                if (button) {
                    button.disabled = isActive;
                    button.textContent = isActive ? '当前协议' : `切换至 ${PROTOCOL_LABELS[proto]}`;
                }
                
                if (indicator) {
                    indicator.classList.toggle('is-active', workerActive);
                    indicator.classList.toggle('is-warning', isActive && !workerActive);
                    indicator.textContent = workerActive ? '转发中' : (isActive ? '待激活' : '待机');
                }
                
                if (info && info.data && Number.isFinite(info.data.thickness_mm)) {
                    summary.textContent = `厚度 ${info.data.thickness_mm.toFixed(3)} mm (序列 ${info.data.sequence || 0})`;
                } else {
                    summary.textContent = '厚度信息暂不可用';
                }
                
                let metaText = '';
                if (proto === 'modbus') {
                    metaText = `${configDetail.listen_ip || '--'}:${configDetail.port || '--'} · 站号 ${configDetail.slave_id ?? '--'}`;
                } else if (proto === 's7') {
                    metaText = `${configDetail.plc_ip || '--'} · Rack ${configDetail.rack ?? '--'} / Slot ${configDetail.slot ?? '--'}`;
                } else if (proto === 'opcua') {
                    metaText = `${configDetail.server_url || '--'} · 安全 ${configDetail.security_mode || 'None'}`;
                }
                meta.textContent = metaText;
                
                if (info && typeof info.updated_ms === 'number') {
                    const ageMs = Date.now() - info.updated_ms;
                    lastUpdate.textContent = `更新时间: ${formatAge(ageMs)}`;
                } else {
                    lastUpdate.textContent = '更新时间: 未知';
                }
                
                if (statusText) {
                    if (!info) {
                        statusText.textContent = '组件状态未知';
                    } else if (workerActive) {
                        statusText.textContent = '数据转发中';
                    } else if (isActive) {
                        statusText.textContent = '等待激活或组件不可用';
                    } else {
                        statusText.textContent = '待机';
                    }
                }
            });
        }
        
        function updateCharts(latencyMs, dropCount, thicknessValue, sampleRate) {
            if (!Number.isFinite(latencyMs)) latencyMs = 0;
            if (!Number.isFinite(dropCount)) dropCount = 0;
            if (!Number.isFinite(sampleRate)) sampleRate = 0;
            chartData.latency.push(latencyMs);
            chartData.drops.push(dropCount);
            chartData.thickness.push(Number.isFinite(thicknessValue) ? thicknessValue : null);
            chartData.sampleRate.push(sampleRate);
            chartData.timestamps.push(Date.now());
            while (chartData.latency.length > CHART_POINTS) {
                chartData.latency.shift();
                chartData.drops.shift();
                chartData.thickness.shift();
                chartData.sampleRate.shift();
                chartData.timestamps.shift();
            }
            drawLatencyChart();
            drawThicknessChart();
        }
        
        function drawLatencyChart() {
            const canvas = document.getElementById('latency-chart');
            if (!canvas) return;
            const ctx = canvas.getContext('2d');
            const dpr = window.devicePixelRatio || 1;
            const width = canvas.clientWidth || 800;
            const height = canvas.clientHeight || 240;
            canvas.width = width * dpr;
            canvas.height = height * dpr;
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.scale(dpr, dpr);
            ctx.clearRect(0, 0, width, height);
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(0, 0, width, height);
            
            const padding = { left: 50, right: 20, top: 20, bottom: 30 };
            const plotWidth = width - padding.left - padding.right;
            const plotHeight = height - padding.top - padding.bottom;
            const scaledDrops = chartData.drops.map(v => v * DROP_SCALE);
            const maxLatency = chartData.latency.length ? Math.max(...chartData.latency) : 0;
            const maxDrop = scaledDrops.length ? Math.max(...scaledDrops) : 0;
            const maxRate = chartData.sampleRate.length ? Math.max(...chartData.sampleRate) : 0;
            const maxY = Math.max(10, maxLatency, maxDrop, maxRate);
            const gridLines = 5;
            ctx.strokeStyle = '#e5e7eb';
            ctx.lineWidth = 1;
            for (let i = 0; i <= gridLines; i++) {
                const y = padding.top + (plotHeight / gridLines) * i;
                ctx.beginPath();
                ctx.moveTo(padding.left, y);
                ctx.lineTo(width - padding.right, y);
                ctx.stroke();
                const value = (maxY - (maxY / gridLines) * i).toFixed(0);
                ctx.fillStyle = '#6b7280';
                ctx.font = '12px sans-serif';
                ctx.fillText(value, 10, y + 4);
            }
            ctx.strokeStyle = '#d1d5db';
            ctx.lineWidth = 1.2;
            ctx.beginPath();
            ctx.moveTo(padding.left, padding.top);
            ctx.lineTo(padding.left, height - padding.bottom);
            ctx.lineTo(width - padding.right, height - padding.bottom);
            ctx.stroke();
            const count = chartData.latency.length;
            if (count < 2 || plotWidth <= 0) return;
            const toX = index => padding.left + (count > 1 ? index / (count - 1) : 0) * plotWidth;
            const toY = value => padding.top + (1 - value / maxY) * plotHeight;
            ctx.strokeStyle = '#2563eb';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(toX(0), toY(chartData.latency[0]));
            for (let i = 1; i < count; i++) {
                ctx.lineTo(toX(i), toY(chartData.latency[i]));
            }
            ctx.stroke();
            ctx.strokeStyle = '#f97316';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(toX(0), toY(scaledDrops[0]));
            for (let i = 1; i < count; i++) {
                ctx.lineTo(toX(i), toY(scaledDrops[i]));
            }
            ctx.stroke();

            ctx.strokeStyle = '#9333ea';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(toX(0), toY(chartData.sampleRate[0]));
            for (let i = 1; i < count; i++) {
                ctx.lineTo(toX(i), toY(chartData.sampleRate[i]));
            }
            ctx.stroke();
        }
        
        function drawThicknessChart() {
            const canvas = document.getElementById('thickness-chart');
            if (!canvas) return;
            const ctx = canvas.getContext('2d');
            const dpr = window.devicePixelRatio || 1;
            const width = canvas.clientWidth || 800;
            const height = canvas.clientHeight || 240;
            canvas.width = width * dpr;
            canvas.height = height * dpr;
            ctx.setTransform(1, 0, 0, 1, 0, 0);
            ctx.scale(dpr, dpr);
            ctx.clearRect(0, 0, width, height);
            ctx.fillStyle = '#ffffff';
            ctx.fillRect(0, 0, width, height);
            
            const padding = { left: 60, right: 20, top: 20, bottom: 30 };
            const plotWidth = width - padding.left - padding.right;
            const plotHeight = height - padding.top - padding.bottom;
            const values = chartData.thickness.filter(v => Number.isFinite(v));
            if (!values.length) {
                ctx.fillStyle = '#94a3b8';
                ctx.font = '14px sans-serif';
                ctx.fillText('暂未获取到厚度数据', padding.left + 20, padding.top + plotHeight / 2);
                return;
            }
            const minVal = Math.min(...values);
            const maxVal = Math.max(...values);
            const range = Math.max(maxVal - minVal, 0.001);
            const minY = minVal - range * 0.1;
            const maxY = maxVal + range * 0.1;
            const gridLines = 5;
            ctx.strokeStyle = '#e5e7eb';
            ctx.lineWidth = 1;
            for (let i = 0; i <= gridLines; i++) {
                const y = padding.top + (plotHeight / gridLines) * i;
                ctx.beginPath();
                ctx.moveTo(padding.left, y);
                ctx.lineTo(width - padding.right, y);
                ctx.stroke();
                const value = maxY - (maxY - minY) / gridLines * i;
                ctx.fillStyle = '#6b7280';
                ctx.font = '12px sans-serif';
                ctx.fillText(value.toFixed(3), 18, y + 4);
            }
            ctx.strokeStyle = '#d1d5db';
            ctx.lineWidth = 1.2;
            ctx.beginPath();
            ctx.moveTo(padding.left, padding.top);
            ctx.lineTo(padding.left, height - padding.bottom);
            ctx.lineTo(width - padding.right, height - padding.bottom);
            ctx.stroke();
            const count = chartData.thickness.length;
            if (count < 2 || plotWidth <= 0) return;
            const toX = index => padding.left + (count > 1 ? index / (count - 1) : 0) * plotWidth;
            const toY = value => padding.top + (1 - (value - minY) / (maxY - minY)) * plotHeight;
            ctx.strokeStyle = '#22c55e';
            ctx.lineWidth = 2;
            ctx.beginPath();
            let started = false;
            for (let i = 0; i < count; i++) {
                const value = chartData.thickness[i];
                if (!Number.isFinite(value)) {
                    started = false;
                    continue;
                }
                const x = toX(i);
                const y = toY(value);
                if (!started) {
                    ctx.moveTo(x, y);
                    started = true;
                } else {
                    ctx.lineTo(x, y);
                }
            }
            ctx.stroke();
        }
        
        window.addEventListener('resize', () => {
            drawLatencyChart();
            drawThicknessChart();
        });
    </script>
</body>
</html>
)HTML";


/**
 * Web Config Daemon Main Function
 */
int main(int argc, char* argv[]) {
    // 初始化日志
    Logger::init("webcfg", false);
    
    LOG_INFO("Web Config Daemon starting...");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 加载配置
    ConfigManager& config = ConfigManager::instance();
    
    std::string config_path = "/opt/gw/conf/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }
    
    if (!config.load(config_path)) {
        LOG_WARN("Failed to load config, using defaults");
    }
    
    // 打开共享内存
    SharedMemoryManager shm;
    if (!shm.open()) {
        LOG_WARN("Failed to open shared memory, status data will be unavailable");
    }
    
    RingBuffer* ring = shm.is_connected() ? shm.get_ring() : nullptr;
    
    // 创建 HTTP 服务器
    SimpleHTTPServer server(8080, config_path);
    if (!server.start()) {
        LOG_FATAL("Failed to start HTTP server");
        return 1;
    }
    
    LOG_INFO("Web Config Daemon started successfully");
    LOG_INFO("Open http://localhost:8080 in your browser");
    
    // 主循环：处理 HTTP 请求
    server.handle_requests(ring);
    
    LOG_INFO("Web Config Daemon shutting down...");
    
    // 清理资源
    server.stop();
    if (shm.is_connected()) {
        shm.close();
    }
    
    LOG_INFO("Web Config Daemon stopped");
    return 0;
}
