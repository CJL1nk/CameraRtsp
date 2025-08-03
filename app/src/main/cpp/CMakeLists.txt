cmake_minimum_required(VERSION 3.22.1)

project("cameraserver")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")

add_library(${CMAKE_PROJECT_NAME} SHARED
        src/main_controller.cpp
        src/server/rtsp_server.cpp
        src/server/stream/audio_stream.cpp
        src/server/stream/video_stream.cpp
        src/media/audio_source.cpp
        src/media/video_source.cpp
        src/media/helper/audio_frame_queue.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/includes
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        aaudio
        mediandk)