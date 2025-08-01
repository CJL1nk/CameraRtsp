cmake_minimum_required(VERSION 3.22.1)

project("cameraserver")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")

add_library(${CMAKE_PROJECT_NAME} SHARED
        controller/main_controller.cpp
        packetizer/aac_latm_packetizer.cpp
        packetizer/h265_packetizer.cpp
        server/audio_stream.cpp
        server/video_stream.cpp
        server/rtsp_server.cpp
        source/audio_frame_queue.cpp
        source/audio_source.cpp
        source/video_source.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log)