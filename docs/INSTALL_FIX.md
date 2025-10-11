# 🔧 依赖安装问题解决 (2025-10-11更新)

## 问题说明

原始的 `install_deps.sh` 脚本使用的下载链接已失效:
- Snap7 1.4.2 不再提供 `.tar.gz` 格式,改为 `.7z` 格式
- 需要使用专门的 ARM 版本

## ✅ 解决方案

已更新 `scripts/install_deps.sh` 脚本,新版本包含:

### 1. 多种安装方式
- **方案 A**: 优先使用 PPA 安装 (最简单,推荐)
- **方案 B**: PPA 失败时自动从源码编译

### 2. 使用正确的下载源
- **Snap7**: 使用 ARM 专用版本 `snap7-iot-arm-1.4.2.tar.gz`
- **open62541**: 使用 GitHub 发布的 1.3.14 版本

### 3. 更好的错误处理
- 自动检测架构
- 详细的安装日志
- 完整的验证流程

## 🚀 使用新脚本

### 方法一: 重新下载脚本 (推荐)

```bash
cd ~/r5c

# 备份旧脚本
mv scripts/install_deps.sh scripts/install_deps.sh.old

# 使用已更新的脚本 (在你的项目中)
# 脚本已经更新完成,直接运行即可

# 赋予执行权限
chmod +x scripts/install_deps.sh

# 运行安装
sudo ./scripts/install_deps.sh
```

### 方法二: 手动安装 (如果脚本仍有问题)

#### 步骤 1: 安装基础依赖

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    wget \
    p7zip-full \
    libmodbus-dev \
    libjsoncpp-dev \
    software-properties-common
```

#### 步骤 2: 安装 Snap7

**方案 A: 使用 PPA (推荐)**

```bash
sudo add-apt-repository -y ppa:gijzelaar/snap7
sudo apt update
sudo apt install -y libsnap7-1 libsnap7-dev
```

**方案 B: 从源码编译**

```bash
cd /tmp

# 下载 ARM 版本
wget -O snap7-iot-arm-1.4.2.tar.gz \
    "https://sourceforge.net/projects/snap7/files/iot/snap7-iot-arm-1.4.2.tar.gz/download"

# 解压
tar -xzf snap7-iot-arm-1.4.2.tar.gz
cd snap7-1.4.2/build/unix

# 编译 (ARM v7)
make -f arm_v7_linux.mk all

# 安装
sudo install -m 0755 ../../bin/arm_v7-linux/libsnap7.so /usr/local/lib/libsnap7.so
sudo install -m 0644 ../../src/core/snap7.h /usr/local/include/snap7.h

# 更新链接库
sudo ldconfig
```

#### 步骤 3: 安装 open62541

**方案 A: 使用 PPA (推荐)**

```bash
sudo add-apt-repository -y ppa:open62541-team/ppa
sudo apt update
sudo apt install -y libopen62541-1-dev
```

**方案 B: 从源码编译**

```bash
cd /tmp

# 下载
wget https://github.com/open62541/open62541/archive/refs/tags/v1.3.14.tar.gz
tar -xzf v1.3.14.tar.gz
cd open62541-1.3.14

# 编译
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DUA_ENABLE_AMALGAMATION=ON \
      -DUA_NAMESPACE_ZERO=FULL \
      ..
make -j$(nproc)

# 安装
sudo make install
sudo ldconfig
```

#### 步骤 4: 验证安装

```bash
# 检查库文件
ldconfig -p | grep libsnap7
ldconfig -p | grep libopen62541

# 检查头文件
ls /usr/include/snap7.h || ls /usr/local/include/snap7.h
ls /usr/include/open62541/ || ls /usr/local/include/open62541/
```

预期输出:
```
libsnap7.so (libc6,AArch64) => /usr/local/lib/libsnap7.so
libopen62541.so.1 (libc6,AArch64) => /usr/lib/libopen62541.so.1
```

## 🔍 故障排查

### 问题 1: wget 404 错误

**原因**: SourceForge 的直接下载链接可能需要重定向

**解决**: 使用 `-O` 参数指定输出文件名:
```bash
wget -O snap7-iot-arm-1.4.2.tar.gz \
    "https://sourceforge.net/projects/snap7/files/iot/snap7-iot-arm-1.4.2.tar.gz/download"
```

### 问题 2: PPA 添加失败

**原因**: 网络问题或 PPA 不可用

**解决**: 使用源码编译方案 (方案 B)

### 问题 3: 编译失败 "make: command not found"

**原因**: 缺少编译工具

**解决**:
```bash
sudo apt install build-essential
```

### 问题 4: 找不到库文件

**原因**: 库安装在 /usr/local/lib 而不是 /usr/lib

**解决**:
```bash
# 更新链接库缓存
sudo ldconfig

# 或手动添加到链接器路径
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/local.conf
sudo ldconfig
```

### 问题 5: CMake 找不到库

**原因**: CMake 搜索路径问题

**解决**: 在编译项目时指定库路径:
```bash
cd ~/r5c
rm -rf build
cmake -B build \
    -DCMAKE_PREFIX_PATH="/usr/local" \
    -DCMAKE_LIBRARY_PATH="/usr/local/lib" \
    -DCMAKE_INCLUDE_PATH="/usr/local/include"
make -C build
```

## ✅ 成功标志

安装成功后,你应该看到:

```bash
sudo ./scripts/install_deps.sh
```

输出类似:
```
=========================================
✅ 验证安装
=========================================

✅ Snap7: /usr/local/lib/libsnap7.so
✅ open62541: /usr/lib/libopen62541.so.1

头文件位置:
  Snap7: /usr/local/include/snap7.h
  open62541: /usr/include/open62541/client.h

=========================================
✅ 所有依赖库安装完成
=========================================
```

## 📝 下一步

安装完成后:

```bash
# 1. 编译项目
cd ~/r5c
rm -rf build
./scripts/build.sh

# 2. 启动服务
./scripts/start.sh

# 3. 查看日志
tail -f /tmp/gw-test/logs/s7d.log
tail -f /tmp/gw-test/logs/opcuad.log
```

## 📚 参考链接

- [Snap7 官方网站](https://snap7.sourceforge.net/)
- [Snap7 下载页面](https://sourceforge.net/projects/snap7/files/)
- [open62541 官方网站](https://www.open62541.org/)
- [open62541 GitHub](https://github.com/open62541/open62541)

---

**更新日期**: 2025-10-11  
**版本**: 2.1  
**测试平台**: Ubuntu 22.04 ARM64 (NanoPi R5S)
