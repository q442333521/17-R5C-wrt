#include "status_writer.h"

#include <filesystem>
#include <fstream>

#include "logger.h"

namespace StatusWriter {
namespace {
std::filesystem::path status_directory() {
    return std::filesystem::path("/tmp/gw-test");
}

std::filesystem::path status_path_for(const std::string& component) {
    return status_directory() / ("status_" + component + ".json");
}
}

void write_component_status(const std::string& component,
                            const NormalizedData* data,
                            bool active,
                            const Json::Value& extra) {
    try {
        std::filesystem::create_directories(status_directory());
    } catch (const std::exception& ex) {
        LOG_WARN("Failed to create status directory: %s", ex.what());
        return;
    }

    const uint64_t now_ns = get_timestamp_ns();

    Json::Value root;
    root["component"] = component;
    root["active"] = active;
    root["updated_ns"] = static_cast<Json::UInt64>(now_ns);
    root["updated_ms"] = static_cast<Json::UInt64>(now_ns / 1000000ULL);

    if (data) {
        Json::Value payload(Json::objectValue);
        payload["sequence"] = static_cast<Json::UInt64>(data->sequence);
        payload["thickness_mm"] = data->thickness_mm;
        payload["status_flags"] = data->status;
        payload["timestamp_ns"] = static_cast<Json::UInt64>(data->timestamp_ns);
        root["data"] = payload;
    } else {
        root["data"] = Json::nullValue;
    }

    if (!extra.isNull()) {
        root["extra"] = extra;
    }

    std::ofstream ofs(status_path_for(component));
    if (!ofs.is_open()) {
        LOG_WARN("Failed to open status file for component %s", component.c_str());
        return;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &ofs);
}

} // namespace StatusWriter
