#ifndef GATEWAY_STATUS_WRITER_H
#define GATEWAY_STATUS_WRITER_H

#include <json/json.h>
#include <string>

#include "ndm.h"

namespace StatusWriter {

/**
 * @brief 将组件运行状态写入共享状态文件，供 Web 前端展示
 *
 * @param component 组件标识，如 "modbus"、"s7"、"opcua"
 * @param data      可选的最新数据指针，nullptr 表示无新数据
 * @param active    当前组件是否处于激活/转发状态
 * @param extra     附加信息（可选）
 */
void write_component_status(const std::string& component,
                            const NormalizedData* data,
                            bool active,
                            const Json::Value& extra = Json::Value());

} // namespace StatusWriter

#endif // GATEWAY_STATUS_WRITER_H
