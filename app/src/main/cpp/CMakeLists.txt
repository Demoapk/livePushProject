cmake_minimum_required(VERSION 3.22.1)

project(native_streamer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PUSHER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(native_streamer SHARED
    src/core/StreamEngine.cpp
    src/codec/AudioEncoder.cpp
    src/codec/VideoEncoder.cpp
    src/gpu/OpenClBeauty.cpp
    src/opengl/EglCore.cpp
    src/opengl/GlProgram.cpp
    src/opengl/GlRenderer.cpp
    src/output/FlvMuxer.cpp
    src/output/RtmpClient.cpp
    src/pipeline/Pipeline.cpp
    src/jni/JniBridge.cpp
)

target_include_directories(native_streamer PRIVATE ${PUSHER_INCLUDE_DIR})

target_link_libraries(native_streamer
    android
    log
    EGL
    GLESv2
    mediandk
    dl
)

target_compile_options(native_streamer PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)
