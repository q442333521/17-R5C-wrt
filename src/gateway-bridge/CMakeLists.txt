cmake_minimum_required(VERSION 3.16)
project(gateway-bridge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖库
find_package(PkgConfig REQUIRED)
pkg_check_modules(MODBUS REQUIRED libmodbus)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MODBUS_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
    /usr/local/include  # Snap7
)

# 源文件
set(COMMON_SOURCES
    common/config.cpp
)

set(PROTOCOL_SOURCES
    protocols/modbus_rtu_master.cpp
    protocols/modbus_tcp_server.cpp
    protocols/s7_client.cpp
)

set(MAPPING_SOURCES
    mapping/data_converter.cpp
    mapping/mapping_engine.cpp
)

# 可执行文件
add_executable(gateway-bridge
    main.cpp
    ${COMMON_SOURCES}
    ${PROTOCOL_SOURCES}
    ${MAPPING_SOURCES}
)

# 链接库
target_link_libraries(gateway-bridge
    ${MODBUS_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    snap7
    pthread
)

# 安装
install(TARGETS gateway-bridge DESTINATION bin)
install(FILES ../../config/gateway_config.json DESTINATION /etc/gateway-bridge/)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(gateway-bridge PRIVATE -g -O0 -Wall -Wextra)
else()
    target_compile_options(gateway-bridge PRIVATE -O2 -Wall)
endif()

# 显示信息
message(STATUS "Gateway Bridge Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
