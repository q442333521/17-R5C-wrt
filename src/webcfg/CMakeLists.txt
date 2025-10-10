# Web Configuration Daemon
add_executable(webcfg
    main.cpp
)

target_link_libraries(webcfg
    gateway_common
    pthread
    rt
)

install(TARGETS webcfg DESTINATION /opt/gw/bin)
