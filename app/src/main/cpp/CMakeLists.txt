cmake_minimum_required(VERSION 3.22.1)

project("cameraserver")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")

add_library(${CMAKE_PROJECT_NAME} SHARED
        src/main.cpp
        src/encoder/E_AAC.cpp
        src/encoder/E_AACFrameQueue.cpp
        src/encoder/E_H265.cpp
        src/mediasource/M_AudioSource.cpp
        src/mediasource/M_VideoSource.cpp
        src/server/S_RtspClient.cpp
        src/server/S_RtspServer.cpp
        src/server/S_RtpSession.cpp
        src/server/S_VideoStream.cpp
        src/server/S_AudioStream.cpp
        src/utils/Utils.cpp
        src/utils/Packetizer.cpp
        src/utils/StreamStats.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/includes
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        aaudio
        mediandk
        camera2ndk
        EGL
        GLESv3)