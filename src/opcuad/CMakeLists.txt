add_executable(opcuad
    main.cpp
)

target_link_libraries(opcuad
    gateway_common
    pthread
    rt
)

install(TARGETS opcuad DESTINATION /opt/gw/bin)
