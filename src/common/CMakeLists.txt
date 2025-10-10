# Common library
add_library(gateway_common STATIC
    ndm.h
    shm_ring.h
    shm_ring.cpp
    config.h
    config.cpp
    logger.h
    logger.cpp
)

target_link_libraries(gateway_common
    ${JSONCPP_LIBRARIES}
    pthread
    rt
)

target_include_directories(gateway_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
