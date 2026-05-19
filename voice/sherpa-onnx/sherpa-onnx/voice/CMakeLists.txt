# 相对 sherpa-onnx 根目录：../../zmq-comm-kit -> LLM_Voice_Flow/zmq-comm-kit
set(ZMQ_COMM_DIR ${CMAKE_SOURCE_DIR}/../../zmq-comm-kit)
set(ZMQ_COMM_SOURCES
    ${ZMQ_COMM_DIR}/src/ZmqInterface.cpp
    ${ZMQ_COMM_DIR}/src/ZmqServer.cpp
    ${ZMQ_COMM_DIR}/src/ZmqClient.cpp
    ${ZMQ_COMM_DIR}/src/ZmqPusher.cpp
    ${ZMQ_COMM_DIR}/src/ZmqPuller.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${ZMQ_COMM_DIR}/include
    ${ZMQ_INCLUDE_DIRS}
)

add_executable(sherpa-onnx-microphone-test1
    sherpa-onnx-microphone.cc
    microphone.cc
    ${ZMQ_COMM_SOURCES}
)

target_link_libraries(sherpa-onnx-microphone-test1
    portaudio_static
    sherpa-onnx-core
    ${ZMQ_LIBRARIES}
    pthread
)


target_link_libraries(sherpa-onnx-microphone-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../lib")
target_link_libraries(sherpa-onnx-microphone-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../../../sherpa_onnx/lib")
  
add_executable(sherpa-onnx-wav-test1
    sherpa-onnx-wav-test1.cc
    ${ZMQ_COMM_SOURCES}
)

target_link_libraries(sherpa-onnx-wav-test1
    sherpa-onnx-core
    ${ZMQ_LIBRARIES}
    pthread
)

target_link_libraries(sherpa-onnx-wav-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../lib")
target_link_libraries(sherpa-onnx-wav-test1 "-Wl,-rpath,${SHERPA_ONNX_RPATH_ORIGIN}/../../../sherpa_onnx/lib")

install(
    TARGETS sherpa-onnx-microphone-test1 sherpa-onnx-wav-test1
    DESTINATION
      bin

  )
