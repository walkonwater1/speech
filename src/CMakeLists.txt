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
# 下载并解压到 cpp/third_party/sherpa-onnx/ 后取消注释:
set(SHERPA_ONNX_DIR "${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx")
find_library(SHERPA_ONNX_LIB sherpa-onnx-c-api PATHS "${SHERPA_ONNX_DIR}/lib")
find_path(SHERPA_ONNX_INCLUDE sherpa-onnx/c-api/c-api.h PATHS "${SHERPA_ONNX_DIR}/include")
if(SHERPA_ONNX_LIB AND SHERPA_ONNX_INCLUDE)
    add_compile_definitions(SHERPA_ONNX_AVAILABLE)
endif()

# ── 源文件 ────────────────────────────────────────────
set(SOURCES
    src/chat_memory.cpp
    src/tts_engine.cpp
    src/llm_engine.cpp
    src/wake_word.cpp
    src/audio_io.cpp
    src/asr_engine.cpp
    src/speaker_verifier.cpp
    src/voice_pipeline.cpp
)

set(HEADERS
    include/config.h
    include/asr_engine.h
    include/llm_engine.h
    include/tts_engine.h
    include/wake_word.h
    include/speaker_verifier.h
    include/chat_memory.h
    include/audio_io.h
    include/voice_pipeline.h
)

# ── 可执行文件 ────────────────────────────────────────
add_executable(voice_pipeline main.cpp ${SOURCES} ${HEADERS})

target_include_directories(voice_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CURL_INCLUDE_DIRS}
)

target_link_libraries(voice_pipeline PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ${ESPEAK_LIB}
)

# 如果有 sherpa-onnx
if(SHERPA_ONNX_LIB)
    target_include_directories(voice_pipeline PRIVATE ${SHERPA_ONNX_INCLUDE})
    target_link_libraries(voice_pipeline PRIVATE ${SHERPA_ONNX_LIB})
endif()

# ── 编译选项 ──────────────────────────────────────────
target_compile_options(voice_pipeline PRIVATE -Wall -Wextra -O2)
