#include "../common/logger.h"
#include "../common/config.h"
#include "../common/shm_ring.h"
#include "../common/status_writer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <ctime>

using namespace std::chrono_literals;

volatile sig_atomic_t g_running = 1;

void signal_handler(int signum) {
    LOG_INFO("Received signal %d, shutting down...", signum);
    g_running = 0;
}

static std::filesystem::path ensure_log_dir() {
    std::filesystem::path dir("/tmp/gw-test/logs");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static void write_status(const NormalizedData* sample,
                         bool protocol_active,
                         const ConfigManager::S7Config& cfg,
                         const std::string& active_protocol) {
    Json::Value extra(Json::objectValue);
    Json::Value conf(Json::objectValue);
    conf["enabled"] = cfg.enabled;
    conf["plc_ip"] = cfg.plc_ip;
    conf["rack"] = cfg.rack;
    conf["slot"] = cfg.slot;
    conf["db_number"] = cfg.db_number;
    conf["update_interval_ms"] = cfg.update_interval_ms;
    extra["config"] = conf;
    extra["active_protocol"] = active_protocol;
    StatusWriter::write_component_status("s7", sample, protocol_active, extra);
}

int main(int argc, char* argv[]) {
    Logger::init("s7d", false);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    ConfigManager& config = ConfigManager::instance();
    std::string config_path = "/opt/gw/conf/config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    if (!config.load(config_path)) {
        LOG_WARN("Failed to load config, using defaults");
    }

    auto s7_cfg = config.get_s7_config();
    auto active_protocol = config.get_string("protocol.active", "modbus");
    auto make_signature = [](const ConfigManager::S7Config& cfg, const std::string& proto) {
        return proto + "|" + (cfg.enabled ? "1" : "0") + "|" + cfg.plc_ip + "|" +
               std::to_string(cfg.rack) + "|" + std::to_string(cfg.slot) + "|" +
               std::to_string(cfg.db_number) + "|" + std::to_string(cfg.update_interval_ms);
    };

    LOG_INFO("========================================");
    LOG_INFO("S7 模拟转发守护进程启动中...");
    LOG_INFO("========================================");
    LOG_INFO("目标 PLC IP: %s", s7_cfg.plc_ip.c_str());
    LOG_INFO("Rack: %d, Slot: %d, DB: %d", s7_cfg.rack, s7_cfg.slot, s7_cfg.db_number);
    LOG_INFO("刷新间隔: %d ms", s7_cfg.update_interval_ms);
    LOG_INFO("当前激活协议: %s", active_protocol.c_str());

    ensure_log_dir();
    std::ofstream sim_log("/tmp/gw-test/logs/s7d_sim.log", std::ios::app);
    if (sim_log.is_open()) {
        sim_log << "[" << time(nullptr) << "] S7 simulator started" << std::endl;
    }

    SharedMemoryManager shm;
    if (!shm.open()) {
        LOG_WARN("Failed to open shared memory, status data will be unavailable");
    }
    RingBuffer* ring = shm.is_connected() ? shm.get_ring() : nullptr;

    std::atomic<bool> protocol_active{active_protocol == "s7" && s7_cfg.enabled};
    std::string config_signature = make_signature(s7_cfg, active_protocol);
    NormalizedData last_data{};
    bool has_data = false;
    auto last_reload = std::chrono::steady_clock::now();
    std::filesystem::file_time_type last_mtime{};

    write_status(has_data ? &last_data : nullptr, protocol_active.load(), s7_cfg, active_protocol);

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        if (now - last_reload > 1s) {
            std::error_code ec;
            auto current_mtime = std::filesystem::last_write_time(config_path, ec);
            if (!ec && current_mtime != last_mtime) {
                last_mtime = current_mtime;
                if (config.load(config_path)) {
                    auto new_cfg = config.get_s7_config();
                    auto new_active_protocol = config.get_string("protocol.active", "modbus");
                    auto new_signature = make_signature(new_cfg, new_active_protocol);
                    bool new_active = (new_active_protocol == "s7" && new_cfg.enabled);
                    if (new_signature != config_signature) {
                        s7_cfg = new_cfg;
                        active_protocol = new_active_protocol;
                        protocol_active = new_active;
                        LOG_INFO("配置已更新: active=%s, enabled=%s", active_protocol.c_str(), s7_cfg.enabled ? "true" : "false");
                        write_status(has_data ? &last_data : nullptr, protocol_active.load(), s7_cfg, active_protocol);
                    } else {
                        s7_cfg = new_cfg;
                        active_protocol = new_active_protocol;
                        protocol_active = new_active;
                    }
                    config_signature = new_signature;
                }
            }
            last_reload = now;
        }

        if (ring) {
            NormalizedData data;
            if (ring->pop_latest(data)) {
                if (ndm_verify_crc(data)) {
                    last_data = data;
                    has_data = true;
                    if (protocol_active.load()) {
                        LOG_DEBUG("[S7 模拟] 发布厚度: %.3f mm (seq=%u)", data.thickness_mm, data.sequence);
                        if (sim_log.is_open()) {
                            sim_log << "[" << time(nullptr) << "] thickness="
                                    << data.thickness_mm << " seq=" << data.sequence << std::endl;
                        }
                    }
                    write_status(&last_data, protocol_active.load(), s7_cfg, active_protocol);
                }
            }
        }

        std::this_thread::sleep_for(10ms);
    }

    write_status(has_data ? &last_data : nullptr, protocol_active.load(), s7_cfg, active_protocol);
    LOG_INFO("S7 模拟守护进程退出");
    return 0;
}
