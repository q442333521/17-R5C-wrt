#include "../common/logger.h"
#include "../common/config.h"
#include "../common/shm_ring.h"
#include <iostream>
#include <thread>
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
        root["running"] = true;
        root["uptime_seconds"] = 12345; // TODO: 实际计算运行时间
        
        // 从共享内存读取最新数据
        NormalizedData data;
        if (ring && ring->pop_latest(data)) {
            root["current_data"]["thickness_mm"] = data.thickness_mm;
            root["current_data"]["sequence"] = data.sequence;
            root["current_data"]["status"] = data.status;
            root["current_data"]["timestamp_ns"] = (Json::Value::UInt64)data.timestamp_ns;
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
    <title>Gateway Configuration</title>
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
        <h1>🔧 Industrial Gateway Configuration</h1>
        
        <div class="section">
            <h2>System Status</h2>
            <div class="data-row">
                <span class="label">Running Status:</span>
                <span class="value">
                    <span class="status-indicator status-ok"></span>
                    <span id="running-status">Running</span>
                </span>
            </div>
            <div class="data-row">
                <span class="label">Uptime:</span>
                <span class="value" id="uptime">Loading...</span>
            </div>
            <div class="data-row">
                <span class="label">Current Thickness:</span>
                <span class="value" id="thickness">Loading...</span>
            </div>
            <div class="data-row">
                <span class="label">Data Sequence:</span>
                <span class="value" id="sequence">Loading...</span>
            </div>
            <div class="data-row">
                <span class="label">Ring Buffer Usage:</span>
                <span class="value" id="buffer-usage">Loading...</span>
            </div>
        </div>
        
        <div class="section">
            <h2>Configuration</h2>
            <button onclick="loadConfig()">Load Configuration</button>
            <button onclick="reloadServices()">Reload Services</button>
            <pre id="config-display" style="background: #2d2d2d; color: #f8f8f2; padding: 15px; border-radius: 4px; overflow-x: auto;"></pre>
        </div>
    </div>
    
    <script>
        // 定时刷新状态
        setInterval(loadStatus, 2000);
        
        // 页面加载时立即获取状态
        loadStatus();
        
        function loadStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('uptime').textContent = data.uptime_seconds + ' seconds';
                    
                    if (data.current_data) {
                        document.getElementById('thickness').textContent = 
                            data.current_data.thickness_mm.toFixed(3) + ' mm';
                        document.getElementById('sequence').textContent = 
                            data.current_data.sequence;
                    } else {
                        document.getElementById('thickness').textContent = 'No data';
                        document.getElementById('sequence').textContent = 'N/A';
                    }
                    
                    if (data.ring_buffer) {
                        const usage = data.ring_buffer.size + ' / ' + data.ring_buffer.capacity;
                        const percent = (data.ring_buffer.size / data.ring_buffer.capacity * 100).toFixed(1);
                        document.getElementById('buffer-usage').textContent = usage + ' (' + percent + '%)';
                    }
                })
                .catch(err => console.error('Failed to load status:', err));
        }
        
        function loadConfig() {
            fetch('/api/config')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('config-display').textContent = 
                        JSON.stringify(data, null, 2);
                })
                .catch(err => console.error('Failed to load config:', err));
        }
        
        function reloadServices() {
            alert('Service reload functionality will be implemented');
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
