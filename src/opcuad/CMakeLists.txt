# OPC UA 客户端可执行文件
# 需要 libopen62541 库

add_executable(opcuad
    main.cpp
)

# 链接库
target_link_libraries(opcuad
    gateway_common
    open62541      # open62541 库 (OPC UA 通信)
    pthread
    rt
)

# 安装到 /opt/gw/bin
install(TARGETS opcuad DESTINATION /opt/gw/bin)
