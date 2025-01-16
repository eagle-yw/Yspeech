

# add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

add_executable(sample 
    main.cpp
    # ${CMAKE_SOURCE_DIR}/src/common/log.cppm
)

# target_compile_definitions(sample PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

target_link_libraries(sample PRIVATE 
    # spdlog::spdlog
)


