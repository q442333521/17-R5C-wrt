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
#include <sstream>
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
    SimpleHTTPServer(int port) : port_(port), server_fd_(-1) {}
    
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
    std::string handle_status(RingBuffer* ring) {
        Json::Value root;
        
        // 系统状态
        root["running"] = (g_running != 0);
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - g_start_time).count();
        root["uptime_seconds"] = static_cast<Json::UInt64>(uptime);
        
        // 从共享内存读取最新数据
        NormalizedData data;
        if (ring && ring->pop_latest(data)) {
            root["current_data"]["thickness_mm"] = data.thickness_mm;
            root["current_data"]["sequence"] = data.sequence;
            root["current_data"]["status"] = data.status;
            root["current_data"]["timestamp_ns"] = static_cast<Json::UInt64>(data.timestamp_ns);
            uint64_t now_ns = get_timestamp_ns();
            if (now_ns > data.timestamp_ns) {
                root["current_data"]["age_ms"] = static_cast<Json::UInt64>((now_ns - data.timestamp_ns) / 1000000ULL);
            } else {
                root["current_data"]["age_ms"] = 0;
            }
        } else {
            root["current_data"] = Json::nullValue;
        }
        
        // 内存状态
        root["ring_buffer"]["size"] = ring ? ring->size() : 0;
        root["ring_buffer"]["capacity"] = RING_SIZE;
        
        Json::StreamWriterBuilder builder;
        std::string json_str = Json::writeString(builder, root);
        
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n";
        oss << "Content-Type: application/json\r\n";
        oss << "Content-Length: " << json_str.length() << "\r\n";
        oss << "Access-Control-Allow-Origin: *\r\n";
        oss << "\r\n";
        oss << json_str;
        
        return oss.str();
    }
    
    std::string handle_get_config() {
        ConfigManager& config = ConfigManager::instance();
        
        Json::StreamWriterBuilder builder;
        std::string json_str = Json::writeString(builder, config.get_config());
        
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n";
        oss << "Content-Type: application/json\r\n";
        oss << "Content-Length: " << json_str.length() << "\r\n";
        oss << "Access-Control-Allow-Origin: *\r\n";
        oss << "\r\n";
        oss << json_str;
        
        return oss.str();
    }
    
    std::string handle_post_config(const std::string& request) {
        // TODO: 完整实现 POST 配置更新
        Json::Value response;
        response["success"] = true;
        response["message"] = "Configuration updated (TODO: implement)";
        
        Json::StreamWriterBuilder builder;
        std::string json_str = Json::writeString(builder, response);
        
        std::ostringstream oss;
        oss << "HTTP/1.1 200 OK\r\n";
        oss << "Content-Type: application/json\r\n";
        oss << "Content-Length: " << json_str.length() << "\r\n";
        oss << "Access-Control-Allow-Origin: *\r\n";
        oss << "\r\n";
        oss << json_str;
        
        return oss.str();
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
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background: #f5f5f5;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            border-bottom: 2px solid #007bff;
            padding-bottom: 10px;
        }
        .section {
            margin: 20px 0;
            padding: 15px;
            background: #f9f9f9;
            border-radius: 4px;
        }
        .data-row {
            display: flex;
            justify-content: space-between;
            margin: 10px 0;
            padding: 10px;
            background: white;
            border-radius: 4px;
        }
        .label {
            font-weight: bold;
            color: #555;
        }
        .value {
            color: #007bff;
            font-family: monospace;
        }
        .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
        }
        .status-ok {
            background: #28a745;
        }
        .status-error {
            background: #dc3545;
        }
        button {
            background: #007bff;
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 4px;
            cursor: pointer;
            font-size: 14px;
        }
        button:hover {
            background: #0056b3;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔧 工业网关监控中心</h1>
        
        <div class="section">
            <h2>系统运行状态</h2>
            <div class="data-row">
                <span class="label">运行状态：</span>
                <span class="value">
                    <span id="status-indicator" class="status-indicator status-ok"></span>
                    <span id="running-status">加载中...</span>
                </span>
            </div>
            <div class="data-row">
                <span class="label">运行时长：</span>
                <span class="value" id="uptime">加载中...</span>
            </div>
            <div class="data-row">
                <span class="label">数据状态：</span>
                <span class="value" id="data-status">等待数据...</span>
            </div>
            <div class="data-row">
                <span class="label">数据延迟：</span>
                <span class="value" id="data-timestamp">等待数据...</span>
            </div>
            <div class="data-row">
                <span class="label">当前厚度：</span>
                <span class="value" id="thickness">等待数据...</span>
            </div>
            <div class="data-row">
                <span class="label">数据序列：</span>
                <span class="value" id="sequence">等待数据...</span>
            </div>
            <div class="data-row">
                <span class="label">环形缓冲区：</span>
                <span class="value" id="buffer-usage">计算中...</span>
            </div>
        </div>
        
        <div class="section">
            <h2>配置概览</h2>
            <button onclick="loadConfig()">刷新配置</button>
            <button onclick="reloadServices()">重载服务</button>
            <pre id="config-display" style="background: #2d2d2d; color: #f8f8f2; padding: 15px; border-radius: 4px; overflow-x: auto;">点击“刷新配置”以查看当前配置</pre>
        </div>
    </div>
    
    <script>
        const STATUS_FLAGS = {
            0x0001: "数据有效",
            0x0002: "RS-485 通信正常",
            0x0004: "CRC 校验通过",
            0x0008: "传感器正常"
        };

        function formatDuration(seconds) {
            if (!Number.isFinite(seconds)) {
                return "未知";
            }
            const total = Math.max(0, Math.floor(seconds));
            const hours = Math.floor(total / 3600);
            const minutes = Math.floor((total % 3600) / 60);
            const secs = total % 60;
            const parts = [];
            if (hours > 0) parts.push(hours + " 小时");
            if (minutes > 0) parts.push(minutes + " 分");
            if (hours === 0 && minutes === 0) parts.push(secs + " 秒");
            else if (secs > 0) parts.push(secs + " 秒");
            return parts.join("") || "0 秒";
        }

        function formatAge(ageMs) {
            if (!Number.isFinite(ageMs)) {
                return "未知";
            }
            if (ageMs < 1000) {
                return `${ageMs} 毫秒前`;
            }
            const seconds = ageMs / 1000;
            if (seconds < 60) {
                return `${seconds.toFixed(1)} 秒前`;
            }
            const minutes = Math.floor(seconds / 60);
            const remainder = Math.floor(seconds % 60);
            return `${minutes} 分 ${remainder} 秒前`;
        }

        function describeStatus(status) {
            if (typeof status !== "number") {
                return "等待数据...";
            }
            const ok = [];
            for (const [mask, label] of Object.entries(STATUS_FLAGS)) {
                if ((status & Number(mask)) !== 0) {
                    ok.push(label);
                }
            }
            let text = ok.length ? ok.join("，") : "无有效标志";
            const errorCode = status & 0xFF00;
            if (errorCode) {
                text += `；错误代码 0x${((errorCode >> 8) & 0xFF).toString(16).padStart(2, "0").toUpperCase()}`;
            }
            return text;
        }

        // 定时刷新状态
        setInterval(loadStatus, 2000);
        
        // 页面加载时立即获取状态
        loadStatus();
        
        function loadStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    const running = Boolean(data.running);
                    const indicator = document.getElementById('status-indicator');
                    indicator.classList.toggle('status-ok', running);
                    indicator.classList.toggle('status-error', !running);
                    document.getElementById('running-status').textContent = running ? '运行中' : '已停止';
                    document.getElementById('uptime').textContent = formatDuration(data.uptime_seconds);
                    
                    if (data.current_data) {
                        document.getElementById('data-status').textContent = describeStatus(data.current_data.status);
                        if (typeof data.current_data.age_ms === 'number') {
                            document.getElementById('data-timestamp').textContent = formatAge(data.current_data.age_ms);
                        } else {
                            document.getElementById('data-timestamp').textContent = '时间信息不可用';
                        }
                        document.getElementById('thickness').textContent = 
                            data.current_data.thickness_mm.toFixed(3) + ' 毫米';
                        document.getElementById('sequence').textContent = 
                            `#${data.current_data.sequence}`;
                    } else {
                        document.getElementById('data-status').textContent = '尚未收到测量数据';
                        document.getElementById('data-timestamp').textContent = '等待数据...';
                        document.getElementById('thickness').textContent = '暂无数据';
                        document.getElementById('sequence').textContent = '暂无数据';
                    }
                    
                    if (data.ring_buffer) {
                        const size = data.ring_buffer.size;
                        const capacity = data.ring_buffer.capacity;
                        const percent = capacity > 0 ? (size / capacity * 100).toFixed(1) : '0.0';
                        document.getElementById('buffer-usage').textContent = `${size} / ${capacity} （${percent}%）`;
                    } else {
                        document.getElementById('buffer-usage').textContent = '无法获取缓冲区信息';
                    }
                })
                .catch(err => {
                    console.error('获取状态失败:', err);
                    const indicator = document.getElementById('status-indicator');
                    indicator.classList.remove('status-ok');
                    indicator.classList.add('status-error');
                    document.getElementById('running-status').textContent = '通信中断';
                });
        }
        
        function loadConfig() {
            fetch('/api/config')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('config-display').textContent = 
                        JSON.stringify(data, null, 2);
                })
                .catch(err => {
                    console.error('获取配置失败:', err);
                    document.getElementById('config-display').textContent = `获取配置失败：${err.message}`;
                });
        }
        
        function reloadServices() {
            alert('服务重载功能即将上线');
        }
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
    SimpleHTTPServer server(8080);
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
