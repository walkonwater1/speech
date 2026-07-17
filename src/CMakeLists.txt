cmake_minimum_required(VERSION 3.16)
project(voice_pipeline VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 第三方依赖 ────────────────────────────────────────
# 系统库
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

# espeak-ng (libespeak-ng.so.1 已安装，手动指定路径)
set(ESPEAK_LIB "/usr/lib/x86_64-linux-gnu/libespeak-ng.so.1")

# sherpa-onnx (可选 — 不装也能编译，ASR/SV 降级运行)
set(SHERPA_ONNX_DIR "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx")
find_library(SHERPA_ONNX_LIB sherpa-onnx-c-api PATHS "${SHERPA_ONNX_DIR}/lib")
find_path(SHERPA_ONNX_INCLUDE sherpa-onnx/c-api/c-api.h PATHS "${SHERPA_ONNX_DIR}/include")
if(SHERPA_ONNX_LIB AND SHERPA_ONNX_INCLUDE)
    add_compile_definitions(SHERPA_ONNX_AVAILABLE)
endif()

# ── 源文件（按功能模块组织）────────────────────────────
set(SOURCES
    pipeline/voice_pipeline.cpp
    pipeline/config.cpp

    speech/asr_engine.cpp
    speech/tts_engine.cpp
    speech/wake_word.cpp
    speech/speaker_verifier.cpp

    brain/llm_engine.cpp
    brain/chat_memory.cpp
    brain/skill_manager.cpp

    audio/audio_io.cpp
    audio/vad.cpp
)

set(HEADERS
    pipeline/voice_pipeline.h
    pipeline/config.h

    speech/asr_engine.h
    speech/tts_engine.h
    speech/wake_word.h
    speech/speaker_verifier.h
    speech/espeak_min.h

    brain/llm_engine.h
    brain/chat_memory.h
    brain/skill_manager.h

    audio/audio_io.h
    audio/vad.h
)

# ── 可执行文件 ────────────────────────────────────────
add_executable(voice_pipeline main.cpp ${SOURCES} ${HEADERS})

target_include_directories(voice_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/speech
    ${CMAKE_SOURCE_DIR}/brain
    ${CMAKE_SOURCE_DIR}/audio
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(voice_pipeline PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ${ESPEAK_LIB}
    pthread
)

# 如果有 sherpa-onnx
if(SHERPA_ONNX_LIB)
    target_include_directories(voice_pipeline PRIVATE ${SHERPA_ONNX_INCLUDE})
    target_link_libraries(voice_pipeline PRIVATE ${SHERPA_ONNX_LIB})
endif()

# ── 编译选项 ──────────────────────────────────────────
target_compile_options(voice_pipeline PRIVATE -Wall -Wextra -O2)
