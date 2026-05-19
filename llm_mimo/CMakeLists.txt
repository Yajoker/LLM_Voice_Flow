cmake_minimum_required(VERSION 3.12)
project(llm_mimo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ZMQ_COMM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zmq-comm-kit)
set(ZMQ_COMM_SOURCES
    ${ZMQ_COMM_DIR}/src/ZmqInterface.cpp
    ${ZMQ_COMM_DIR}/src/ZmqServer.cpp
    ${ZMQ_COMM_DIR}/src/ZmqClient.cpp
    ${ZMQ_COMM_DIR}/src/ZmqPusher.cpp
    ${ZMQ_COMM_DIR}/src/ZmqPuller.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(ZMQ REQUIRED libzmq)

find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp
    PATHS /usr/include /usr/local/include)

if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 nlohmann/json.hpp，请安装 nlohmann-json3-dev")
endif()

add_executable(llm_mimo_server
    src/main.cpp
    src/config.cpp
    src/sse_parser.cpp
    src/mimo_client.cpp
    src/sentence_splitter.cpp
    src/zmq_gateway.cpp
    ${ZMQ_COMM_SOURCES}
)

install(FILES config/mimo.ini.example DESTINATION share/llm_mimo)

target_include_directories(llm_mimo_server PRIVATE
    ${NLOHMANN_JSON_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZMQ_COMM_DIR}/include
)

target_include_directories(llm_mimo_server SYSTEM PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${ZMQ_INCLUDE_DIRS}
)

target_link_libraries(llm_mimo_server PRIVATE
    ${CURL_LIBRARIES}
    ${ZMQ_LIBRARIES}
)

target_compile_options(llm_mimo_server PRIVATE
    -Wall -Wextra -Wpedantic
)

install(TARGETS llm_mimo_server RUNTIME DESTINATION bin)
