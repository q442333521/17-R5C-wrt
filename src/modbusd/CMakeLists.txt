# Modbus TCP Daemon
add_executable(modbusd
    main.cpp
)

target_link_libraries(modbusd
    gateway_common
    ${MODBUS_LIBRARIES}
    pthread
    rt
)

install(TARGETS modbusd DESTINATION /opt/gw/bin)
