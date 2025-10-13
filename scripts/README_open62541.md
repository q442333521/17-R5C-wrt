# open62541 安装和使用指南

## 📋 快速开始

### 1. 标准安装（推荐 - 不含加密）

```bash
cd ~/r5c
sudo ./scripts/install_deps.sh
```

这将安装：
- ✅ Snap7 (西门子 S7)
- ✅ open62541 (OPC UA) - 标准配置
- ✅ libmodbus (Modbus)
- ✅ libjsoncpp (JSON)

**open62541 标准配置功能：**
- ✅ 完整 OPC UA 客户端/服务器
- ✅ 订阅功能
- ✅ 方法调用
- ✅ 节点管理
- ❌ 加密支持（不需要 MbedTLS，避免依赖问题）

---

## 🔧 自定义安装

### 选项 1: 最小化配置（仅基础功能）

```bash
cd ~/r5c
sudo BUILD_CONFIG=minimal ./scripts/wrt/build_open62541.sh
```

**功能：**
- ✅ 基础 OPC UA 客户端/服务器
- ❌ 订阅、方法调用、加密等高级功能

**优点：**
- 编译最快（约 2-3 分钟）
- 占用空间最小
- 适合只需要基础通信的场景

---

### 选项 2: 标准配置（常用功能，默认）

```bash
cd ~/r5c
sudo BUILD_CONFIG=standard ./scripts/wrt/build_open62541.sh
```

**功能：**
- ✅ 完整 OPC UA 客户端/服务器
- ✅ 订阅功能
- ✅ 方法调用
- ✅ 节点管理
- ❌ 加密支持

**优点：**
- 包含大部分常用功能
- 不需要 MbedTLS 依赖
- 编译时间适中（约 5-8 分钟）

---

### 选项 3: 完整配置（含加密）

```bash
cd ~/r5c
sudo BUILD_CONFIG=full ./scripts/wrt/build_open62541.sh
```

**功能：**
- ✅ 所有标准功能
- ✅ MbedTLS 加密支持
- ✅ PubSub 支持

**注意：**
- 会自动安装 MbedTLS
- 编译时间最长（约 10-15 分钟）
- 只在需要加密通信时使用

---

## 🔍 验证安装

### 检查库文件

```bash
# 检查 open62541
ldconfig -p | grep libopen62541

# 检查 Snap7
ldconfig -p | grep libsnap7

# 检查版本
pkg-config --modversion open62541
```

### 检查头文件

```bash
# open62541 头文件
ls -l /usr/local/include/open62541/

# Snap7 头文件
ls -l /usr/local/include/snap7.h
```

---

## 💻 在项目中使用

### CMakeLists.txt 示例

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyOPCUAProject)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)

# 查找依赖库
find_package(open62541 REQUIRED)

# 创建可执行文件
add_executable(my_opcua_app
    src/main.cpp
    src/opcua_client.cpp
)

# 链接库（方法 1 - 推荐）
target_link_libraries(my_opcua_app
    open62541::open62541
)

# 或者直接链接（方法 2）
# target_include_directories(my_opcua_app PRIVATE /usr/local/include)
# target_link_libraries(my_opcua_app open62541 snap7 modbus jsoncpp)
```

### 编译项目

```bash
cd /path/to/your/project
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## 🐛 故障排查

### 问题 1: CMake 找不到 open62541

**错误信息：**
```
Could NOT find open62541 (missing: open62541_DIR)
```

**解决方法：**
```bash
# 检查 pkg-config
pkg-config --list-all | grep open62541

# 设置 PKG_CONFIG_PATH
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# 或者在 CMakeLists.txt 中指定路径
set(open62541_DIR /usr/local/lib/cmake/open62541)
find_package(open62541 REQUIRED)
```

---

### 问题 2: 编译时缺少 MbedTLS

**错误信息：**
```
Could NOT find MbedTLS
```

**原因：**
使用了 `BUILD_CONFIG=full` 但系统没有 MbedTLS

**解决方法 1：** 使用标准配置（推荐）
```bash
sudo BUILD_CONFIG=standard ./scripts/wrt/build_open62541.sh
```

**解决方法 2：** 安装 MbedTLS
```bash
sudo apt install libmbedtls-dev
sudo BUILD_CONFIG=full ./scripts/wrt/build_open62541.sh
```

---

### 问题 3: 链接时找不到库

**错误信息：**
```
undefined reference to `UA_Client_new'
```

**解决方法：**
```bash
# 更新动态链接库缓存
sudo ldconfig

# 在 CMakeLists.txt 中确保正确链接
target_link_libraries(your_target open62541::open62541)
```

---

## 📊 配置对比

| 功能 | minimal | standard | full |
|------|---------|----------|------|
| 基础通信 | ✅ | ✅ | ✅ |
| 订阅 | ❌ | ✅ | ✅ |
| 方法调用 | ❌ | ✅ | ✅ |
| 节点管理 | ❌ | ✅ | ✅ |
| 加密支持 | ❌ | ❌ | ✅ |
| PubSub | ❌ | ❌ | ✅ |
| MbedTLS 依赖 | ❌ | ❌ | ✅ |
| 编译时间 | ~3分钟 | ~8分钟 | ~15分钟 |
| 库文件大小 | ~2MB | ~5MB | ~8MB |

---

## 📝 常用命令

```bash
# 查看已安装版本
pkg-config --modversion open62541

# 查看编译参数
pkg-config --cflags open62541

# 查看链接参数
pkg-config --libs open62541

# 卸载 open62541
sudo rm -rf /usr/local/lib/libopen62541*
sudo rm -rf /usr/local/include/open62541*
sudo rm -rf /usr/local/lib/cmake/open62541
sudo ldconfig

# 重新安装
sudo ./scripts/wrt/build_open62541.sh
```

---

## 🔄 重新编译

如果需要切换配置，直接运行相应的命令：

```bash
# 从 standard 切换到 full
sudo BUILD_CONFIG=full ./scripts/wrt/build_open62541.sh

# 从 full 切换到 minimal
sudo BUILD_CONFIG=minimal ./scripts/wrt/build_open62541.sh
```

脚本会自动覆盖之前的安装。

---

## ⚠️ 注意事项

1. **Ubuntu 版本**
   - Ubuntu 22.04 必须从源码编译
   - Ubuntu 20.04 可以使用 PPA（但仍推荐源码编译）

2. **系统资源**
   - 至少 2GB 可用内存
   - 至少 500MB 磁盘空间
   - 推荐多核 CPU 以加快编译

3. **权限**
   - 所有安装脚本需要 root 权限 (sudo)
   - 编译过程会使用 /tmp 目录

4. **清理**
   - 脚本会自动清理临时文件
   - 如需保留用于调试，设置 `CLEANUP_TEMP=0`

---

## 📚 更多资源

- [open62541 官方文档](https://www.open62541.org/doc/)
- [OPC UA 规范](https://opcfoundation.org/)
- [Snap7 文档](http://snap7.sourceforge.net/)

---

## 🎯 总结

**推荐配置选择：**

- 🏠 **家庭项目/学习** → `standard` (默认)
- 🏭 **工业应用 (无加密需求)** → `standard`
- 🔒 **需要加密的生产环境** → `full`
- 🚀 **资源受限设备** → `minimal`

**默认安装命令：**
```bash
cd ~/r5c
sudo ./scripts/install_deps.sh
```

这将给你一个功能完善且编译快速的 open62541 安装！
