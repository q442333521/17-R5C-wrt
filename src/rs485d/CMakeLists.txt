# RS485 Daemon
add_executable(rs485d
    main.cpp
)

target_link_libraries(rs485d
    gateway_common
    pthread
    rt
)

install(TARGETS rs485d DESTINATION /opt/gw/bin)
