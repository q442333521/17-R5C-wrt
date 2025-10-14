#!/bin/bash
################################################################################
# 使用 Alpine Linux ARM64 容器编译（原生 musl）
# 包含：rs485d, modbusd, webcfg, s7d, opcuad
################################################################################

set -e

PROJECT_ROOT="/root/r5c"

echo "========================================="
echo "Alpine Linux ARM64 容器编译 (musl libc)"
echo "编译所有守护进程（5个）"
echo "========================================="
echo ""

# 运行 Alpine ARM64 容器进行编译
docker run --rm --platform linux/arm64 \
  -v "$PROJECT_ROOT:/workspace" \
  -w /workspace \
  alpine:latest /bin/sh -c '
    echo "==> 安装编译依赖..."
    apk add --no-cache \
        build-base \
        cmake \
        libmodbus-dev \
        jsoncpp-dev \
        jsoncpp-static \
        git \
        wget \
        tar

    echo ""
    echo "==> 编译并安装 snap7 (用于 s7d)..."
    cd /tmp
    wget -q https://sourceforge.net/projects/snap7/files/1.4.2/snap7-full-1.4.2.tar.gz/download -O snap7.tar.gz
    tar xzf snap7.tar.gz
    cd snap7-full-1.4.2/build/unix
    make -f arm_v7_linux.mk all || make -f arm_v6_linux.mk all || make -f aarch64_linux.mk all
    cp ../bin/libsnap7.so /usr/lib/
    cp ../../src/core/snap7.h /usr/include/
    echo "snap7 安装完成"
    
    echo ""
    echo "==> 编译并安装 open62541 (用于 opcuad)..."
    cd /tmp
    git clone --depth 1 --branch v1.3.6 https://github.com/open62541/open62541.git
    cd open62541
    mkdir build && cd build
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DUA_ENABLE_AMALGAMATION=ON
    make -j$(nproc)
    make install
    echo "open62541 安装完成"

    echo ""
    echo "==> 配置 CMake..."
    cd /workspace
    rm -rf build-alpine
    mkdir -p build-alpine
    cd build-alpine
    
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O2 -march=armv8-a+crc" \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
    
    echo ""
    echo "==> 开始编译所有守护进程..."
    make -j$(nproc) 2>&1 | grep -E "Building|Linking|Built target|Error|error" || true
    
    echo ""
    echo "==> 检查编译产物..."
    for binary in rs485d modbusd webcfg s7d opcuad; do
        if [ -f "src/$binary/$binary" ]; then
            size=$(du -h "src/$binary/$binary" | cut -f1)
            echo "✓ $binary ($size)"
            file "src/$binary/$binary" | cut -d: -f2
            echo "  依赖:"
            ldd "src/$binary/$binary" 2>&1 | grep -v "statically linked" | head -6 | sed "s/^/    /"
            echo ""
        else
            echo "✗ $binary - 编译失败"
            echo ""
        fi
    done
    
    echo "==> 复制到输出目录..."
    mkdir -p /workspace/package/gw-gateway_alpine
    mkdir -p /workspace/package/gw-gateway_alpine/opt/gw/bin
    mkdir -p /workspace/package/gw-gateway_alpine/opt/gw/conf
    mkdir -p /workspace/package/gw-gateway_alpine/opt/gw/logs
    
    # 复制所有二进制文件
    for binary in rs485d modbusd webcfg s7d opcuad; do
        if [ -f "src/$binary/$binary" ]; then
            cp "src/$binary/$binary" /workspace/package/gw-gateway_alpine/opt/gw/bin/
            echo "已复制: $binary"
        fi
    done
    
    cp /workspace/config/config.json /workspace/package/gw-gateway_alpine/opt/gw/conf/ || true
    chmod +x /workspace/package/gw-gateway_alpine/opt/gw/bin/* || true
    
    # 尝试静态链接 jsoncpp 重新编译 webcfg, s7d, opcuad
    echo ""
    echo "==> 尝试静态链接 jsoncpp..."
    
    # webcfg
    if [ -f "src/webcfg/CMakeFiles/webcfg.dir/main.cpp.o" ]; then
        cd /workspace/build-alpine/src/webcfg
        g++ -o webcfg_static \
            CMakeFiles/webcfg.dir/main.cpp.o \
            ../../src/common/libgateway_common.a \
            /usr/lib/libjsoncpp.a \
            -lpthread -static-libstdc++ -static-libgcc 2>/dev/null && \
        cp webcfg_static /workspace/package/gw-gateway_alpine/opt/gw/bin/webcfg && \
        echo "webcfg: 已静态链接 jsoncpp ✓"
    fi
    
    # s7d
    if [ -f "/workspace/build-alpine/src/s7d/CMakeFiles/s7d.dir/main.cpp.o" ]; then
        cd /workspace/build-alpine/src/s7d
        g++ -o s7d_static \
            CMakeFiles/s7d.dir/main.cpp.o \
            ../../src/common/libgateway_common.a \
            /usr/lib/libjsoncpp.a \
            /usr/lib/libsnap7.so \
            -lpthread -static-libstdc++ -static-libgcc 2>/dev/null && \
        cp s7d_static /workspace/package/gw-gateway_alpine/opt/gw/bin/s7d && \
        echo "s7d: 已静态链接 jsoncpp ✓"
    fi
    
    # opcuad
    if [ -f "/workspace/build-alpine/src/opcuad/CMakeFiles/opcuad.dir/main.cpp.o" ]; then
        cd /workspace/build-alpine/src/opcuad
        g++ -o opcuad_static \
            CMakeFiles/opcuad.dir/main.cpp.o \
            ../../src/common/libgateway_common.a \
            /usr/lib/libjsoncpp.a \
            /usr/local/lib/libopen62541.a \
            -lpthread -static-libstdc++ -static-libgcc 2>/dev/null && \
        cp opcuad_static /workspace/package/gw-gateway_alpine/opt/gw/bin/opcuad && \
        echo "opcuad: 已静态链接 jsoncpp ✓"
    fi
    
    echo ""
    echo "==> 最终二进制文件列表:"
    ls -lh /workspace/package/gw-gateway_alpine/opt/gw/bin/
    
    echo ""
    echo "==> 完成！"
'

echo ""
echo "========================================="
echo "编译完成！"
echo "========================================="
echo ""
echo "输出目录: $PROJECT_ROOT/package/gw-gateway_alpine"
echo ""
echo "打包并上传命令:"
echo "  cd $PROJECT_ROOT/package/gw-gateway_alpine"
echo "  tar czf ../gw-gateway-alpine.tar.gz opt/"
echo "  sshpass -p '!Wangzeyu166!@#' scp ../gw-gateway-alpine.tar.gz root@100.100.5.26:/tmp/"
echo ""

echo ""
echo "==> 复制管理脚本..."
cp /root/r5c/scripts/wrt/start.sh /root/r5c/package/gw-gateway_alpine/opt/gw/
cp /root/r5c/scripts/wrt/stop.sh /root/r5c/package/gw-gateway_alpine/opt/gw/
chmod +x /root/r5c/package/gw-gateway_alpine/opt/gw/*.sh
echo "管理脚本已复制"
echo ""
echo "==> 创建使用说明..."
cat > /root/r5c/package/gw-gateway_alpine/opt/gw/README.txt << 'README_EOF'
========================================
Gateway 服务管理
========================================

快速启动：
  cd /opt/gw
  ./start.sh

快速停止：
  cd /opt/gw
  ./stop.sh

停止并清理日志：
  cd /opt/gw
  ./stop.sh clean

查看日志：
  tail -f /opt/gw/logs/*.log

查看实时数据：
  curl http://localhost:8080/api/status

服务列表：
  - rs485d   : RS485 数据采集守护进程
  - modbusd  : Modbus TCP 服务器 (端口 1502)
  - webcfg   : Web 配置界面 (端口 8080)
  - s7d      : Siemens S7 PLC 通信守护进程
  - opcuad   : OPC-UA 客户端守护进程

配置文件：
  /opt/gw/conf/config.json

日志目录：
  /opt/gw/logs/

PID 文件：
  /tmp/gw-pids/

========================================
README_EOF

echo "使用说明已创建"

