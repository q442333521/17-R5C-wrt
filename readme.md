# 工业机床数据采集网关 - README

基于 NanoPi R5S 的 Linux 数据采集网关项目

## 项目生成完成 ✅

所有文件已成功生成在: `D:\OneDrive\p\17-R5C-wrt`

## 快速导航

- 📖 [10分钟快速上手](QUICKSTART.md) - **从这里开始！**
- 📋 [项目完成总结](PROJECT_SUMMARY.md) - 功能清单和验证步骤
- 📝 [功能检查清单](FILE_CHECKLIST.md) - 已实现和待完成功能
- 📄 [原始技术方案](readme.md) - 完整的设计文档

## 立即开始测试

### Windows 用户 (WSL)
```cmd
cd D:\OneDrive\p\17-R5C-wrt
scripts\gateway.bat
```
选择: 1 → 2 → 3 (安装依赖 → 编译 → 启动)

### Linux 用户
```bash
cd D:\OneDrive\p\17-R5C-wrt

# 安装依赖
sudo apt install -y build-essential cmake libmodbus-dev libjsoncpp-dev

# 编译
./scripts/build.sh

# 启动
./scripts/start.sh

# 测试 Modbus
pip3 install pymodbus
python3 tests/test_modbus_client.py

# Web 界面
# http://localhost:8080
```

## 项目结构

```
17-R5C-wrt/
├── src/               源代码 (C++17)
│   ├── common/       公共库 (数据模型、共享内存、配置、日志)
│   ├── rs485d/       RS-485 采集守护进程
│   ├── modbusd/      Modbus TCP 服务器
│   └── webcfg/       Web 配置界面
├── config/           配置文件
├── scripts/          编译和启动脚本
├── systemd/          系统服务配置
└── tests/            测试脚本
```

## 核心功能

- ✅ RS-485 数据采集 (50Hz)
- ✅ Modbus TCP 服务器 (端口 502)
- ✅ Web 配置界面 (端口 8080)
- ✅ 无锁共享内存通信
- ✅ 配置管理和日志系统

## 技术栈

- **语言**: C++17
- **依赖**: libmodbus, jsoncpp
- **构建**: CMake 3.16+
- **平台**: Ubuntu 22.04 arm64 / x86_64

## 验证步骤

启动后验证：
1. 日志输出正常 (`tail -f /tmp/gw-test/logs/*.log`)
2. Web 界面可访问 (http://localhost:8080)
3. Modbus 客户端能读取数据
4. 数据实时更新 (50Hz)

## 重要说明

⚠️ **当前版本使用模拟数据**：rs485d 生成随机厚度值用于测试

📝 **实际部署前需要**：
- 修改 `src/rs485d/main.cpp` 中的协议解析逻辑
- 根据实际测厚仪的通信协议调整代码

## 文档索引

| 文档 | 用途 |
|------|------|
| [QUICKSTART.md](QUICKSTART.md) | 10分钟快速上手 |
| [PROJECT_SUMMARY.md](PROJECT_SUMMARY.md) | 项目完成总结 |
| [FILE_CHECKLIST.md](FILE_CHECKLIST.md) | 功能清单 |
| [readme.md](readme.md) | 原始技术方案 |

## 性能指标

| 指标 | 目标 |
|------|------|
| 采样频率 | 50 Hz |
| Modbus 延迟 | < 10 ms |
| 吞吐量 | > 1000 TPS |
| 内存占用 | < 100 MB |

## 下一步

1. ✅ 编译项目
2. ✅ 启动测试
3. ✅ 验证功能
4. 📝 连接实际设备
5. 🔧 协议适配
6. 🚀 部署到 R5S

---

**状态**: ✅ 可编译、可运行、可测试  
**版本**: v1.0 - 核心功能完整

开始使用: `./scripts/build.sh && ./scripts/start.sh`
