add_executable(s7d
    main.cpp
)

target_link_libraries(s7d
    gateway_common
    pthread
    rt
)

install(TARGETS s7d DESTINATION /opt/gw/bin)
