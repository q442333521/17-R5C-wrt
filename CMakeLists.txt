cmake_minimum_required(VERSION 3.16)
project(gateway VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")

# 查找依赖库
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# libmodbus
pkg_check_modules(MODBUS REQUIRED libmodbus)

# jsoncpp
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${MODBUS_INCLUDE_DIRS}
    ${JSONCPP_INCLUDE_DIRS}
)

# 链接目录
link_directories(
    ${MODBUS_LIBRARY_DIRS}
    ${JSONCPP_LIBRARY_DIRS}
)

# 添加子目录
add_subdirectory(src/common)
add_subdirectory(src/rs485d)
add_subdirectory(src/modbusd)
add_subdirectory(src/webcfg)
add_subdirectory(src/s7d)
add_subdirectory(src/opcuad)

# 安装规则
install(DIRECTORY config/ DESTINATION /opt/gw/conf)
install(DIRECTORY systemd/ DESTINATION /etc/systemd/system)
install(PROGRAMS scripts/build.sh DESTINATION /opt/gw/scripts)

# 打印配置信息
message(STATUS "========================================")
message(STATUS "Gateway Project Configuration")
message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Modbus: ${MODBUS_LIBRARIES}")
message(STATUS "JsonCpp: ${JSONCPP_LIBRARIES}")
message(STATUS "========================================")
