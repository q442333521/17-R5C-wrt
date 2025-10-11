# S7 客户端可执行文件
# 需要 libsnap7 库

add_executable(s7d
    main.cpp
)

# 链接库
target_link_libraries(s7d
    gateway_common
    snap7          # Snap7 库 (西门子 S7 通信)
    pthread
    rt
)

# 安装到 /opt/gw/bin
install(TARGETS s7d DESTINATION /opt/gw/bin)
