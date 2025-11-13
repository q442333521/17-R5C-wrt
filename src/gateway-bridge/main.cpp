#include "common/config.h"
#include "protocols/modbus_rtu_master.h"
#include "protocols/modbus_tcp_server.h"
#include "protocols/s7_client.h"
#include "mapping/mapping_engine.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace gateway;

// 全局运行标志
std::atomic<bool> g_running{true};

// 信号处理
void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -c, --config <file>    Configuration file (default: /etc/gateway-bridge/gateway_config.json)" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
    std::cout << "  -v, --version          Show version information" << std::endl;
}

void print_version() {
    std::cout << "Gateway Bridge v1.0.0" << std::endl;
    std::cout << "Industrial Protocol Gateway - RTU ↔ TCP/S7" << std::endl;
}

void print_banner() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════╗
║         工业协议网关 - Gateway Bridge v1.0.0           ║
║                                                       ║
║  Modbus RTU ↔ Modbus TCP                             ║
║  Modbus RTU ↔ S7 PLC                                 ║
║                                                       ║
║  超越商用网关 - 完全开源                               ║
╚═══════════════════════════════════════════════════════╝
)" << std::endl;
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_file = "/etc/gateway-bridge/gateway_config.json";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            print_version();
            return 0;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                std::cerr << "Error: --config requires an argument" << std::endl;
                return 1;
            }
        }
    }

    print_banner();

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 加载配置
    std::cout << "Loading configuration from: " << config_file << std::endl;
    ConfigManager config_mgr;
    if (!config_mgr.load_from_file(config_file)) {
        std::cerr << "Failed to load configuration file: " << config_file << std::endl;
        std::cerr << "Using default configuration..." << std::endl;
    }

    const auto& config = config_mgr.get_config();

    // 验证配置
    if (!config_mgr.validate()) {
        std::cerr << "Configuration validation failed!" << std::endl;
        return 1;
    }

    std::cout << "Configuration loaded successfully" << std::endl;
    std::cout << "  Gateway mode: " << config.gateway.mode << std::endl;
    std::cout << "  RTU device: " << config.modbus_rtu.device << " @ " << config.modbus_rtu.baudrate << std::endl;
    std::cout << "  Mapping rules: " << config.mapping_rules.size() << std::endl;

    // 创建Modbus RTU Master
    std::cout << "\nInitializing Modbus RTU Master..." << std::endl;
    auto rtu_master = std::make_shared<ModbusRTUMaster>(
        config.modbus_rtu.device,
        config.modbus_rtu.baudrate,
        config.modbus_rtu.parity,
        config.modbus_rtu.data_bits,
        config.modbus_rtu.stop_bits
    );

    rtu_master->set_timeout(config.modbus_rtu.timeout_ms);
    rtu_master->set_retry_count(config.modbus_rtu.retry_count);

    if (!rtu_master->connect()) {
        std::cerr << "Failed to connect RTU Master: " << rtu_master->get_last_error() << std::endl;
        std::cerr << "Warning: RTU master not connected, continuing..." << std::endl;
    } else {
        std::cout << "RTU Master connected successfully" << std::endl;
    }

    // 创建Modbus TCP Server或S7 Client
    std::shared_ptr<ModbusTCPServer> tcp_server;
    std::shared_ptr<S7Client> s7_client;

    if (config.gateway.mode == "modbus_tcp" && config.modbus_tcp.enabled) {
        std::cout << "\nInitializing Modbus TCP Server..." << std::endl;
        tcp_server = std::make_shared<ModbusTCPServer>(
            config.modbus_tcp.listen_ip,
            config.modbus_tcp.port
        );

        if (!tcp_server->start()) {
            std::cerr << "Failed to start TCP Server: " << tcp_server->get_last_error() << std::endl;
            return 1;
        }
        std::cout << "Modbus TCP Server started on " << config.modbus_tcp.listen_ip
                  << ":" << config.modbus_tcp.port << std::endl;
    } else if (config.gateway.mode == "s7" && config.s7.enabled) {
        std::cout << "\nInitializing S7 Client..." << std::endl;
        s7_client = std::make_shared<S7Client>(
            config.s7.plc_ip,
            config.s7.rack,
            config.s7.slot
        );

        s7_client->set_timeout(config.s7.connection_timeout_ms);

        if (!s7_client->connect()) {
            std::cerr << "Failed to connect S7 Client: " << s7_client->get_last_error() << std::endl;
            std::cerr << "Warning: S7 client not connected, continuing..." << std::endl;
        } else {
            std::cout << "S7 Client connected to " << config.s7.plc_ip
                      << " (Rack: " << config.s7.rack << ", Slot: " << config.s7.slot << ")" << std::endl;
        }
    }

    // 创建映射引擎
    std::cout << "\nInitializing Mapping Engine..." << std::endl;
    auto mapping_engine = std::make_shared<MappingEngine>(
        rtu_master,
        tcp_server,
        s7_client
    );

    // 加载映射规则
    mapping_engine->load_rules(config.mapping_rules);

    // 启动映射引擎
    if (!mapping_engine->start()) {
        std::cerr << "Failed to start Mapping Engine" << std::endl;
        return 1;
    }

    std::cout << "\n╔═══════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Gateway Bridge is running...            ║" << std::endl;
    std::cout << "║  Press Ctrl+C to stop                    ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════╝" << std::endl;

    // 主循环
    int heartbeat_counter = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 每10秒打印一次心跳
        if (++heartbeat_counter >= 10) {
            heartbeat_counter = 0;

            // 打印统计信息
            auto rules = mapping_engine->get_all_rules();
            int active_rules = 0;
            int healthy_rules = 0;
            uint64_t total_reads = 0;
            uint64_t total_errors = 0;

            for (const auto& rule : rules) {
                if (rule.enabled) {
                    active_rules++;
                    if (rule.status.is_healthy) {
                        healthy_rules++;
                    }
                    total_reads += rule.status.read_count;
                    total_errors += rule.status.error_count;
                }
            }

            std::cout << "\n[HEARTBEAT] Active: " << active_rules
                      << " | Healthy: " << healthy_rules
                      << " | Reads: " << total_reads
                      << " | Errors: " << total_errors;

            if (tcp_server) {
                std::cout << " | TCP Clients: " << tcp_server->get_connection_count();
            }

            std::cout << std::endl;

            // 打印每个规则的状态
            for (const auto& rule : rules) {
                if (rule.enabled) {
                    std::cout << "  [" << rule.rule_id << "] "
                              << (rule.status.is_healthy ? "✓" : "✗") << " "
                              << "Last value: " << rule.status.last_value << " "
                              << "Reads: " << rule.status.read_count.load() << " "
                              << "Errors: " << rule.status.error_count.load()
                              << std::endl;
                }
            }
        }
    }

    // 清理
    std::cout << "\nShutting down..." << std::endl;
    mapping_engine->stop();

    if (tcp_server) {
        tcp_server->stop();
    }

    if (s7_client) {
        s7_client->disconnect();
    }

    rtu_master->disconnect();

    std::cout << "Gateway Bridge stopped successfully" << std::endl;
    return 0;
}
