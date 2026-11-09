cmake_minimum_required(VERSION 3.19)
project(pressure_mat_backend LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core LinguistTools SerialPort WebSockets Network)

qt_standard_project_setup()

qt_add_executable(pressure_mat_backend
    main.cpp
    snapshotbuffer.h snapshotservice.h snapshotservice.cpp
    pressureframeparser.h pressureframeparser.cpp
    serialreceiver.h serialreceiver.cpp
    pressurewebsocketserver.h pressurewebsocketserver.cpp
    StaticHttpServer.h StaticHttpServer.cpp
)

qt_add_translations(
    TARGETS pressure_mat_backend
    TS_FILES pressure_mat_backend_en_US.ts
)

target_link_libraries(pressure_mat_backend
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::SerialPort
        Qt6::WebSockets
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(snapshot_tests tests/snapshot_tests.cpp snapshotservice.cpp snapshotservice.h)
    set_target_properties(snapshot_tests PROPERTIES WIN32_EXECUTABLE FALSE)
    target_include_directories(snapshot_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(snapshot_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
    add_test(NAME snapshot_tests COMMAND snapshot_tests)
endif()

add_custom_command(
    TARGET pressure_mat_backend
    POST_BUILD

    COMMAND
        ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist"
        "$<TARGET_FILE_DIR:pressure_mat_backend>/frontend"

    COMMENT
        "Copying production frontend beside the executable"
)

install(TARGETS pressure_mat_backend
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
