cmake_minimum_required(VERSION 3.16)
project(voice_pipeline VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── 第三方依赖 ────────────────────────────────────────
# 系统库
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(websocketpp QUIET)  # header-only, optional

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
    speech/streaming_asr.cpp
    speech/tts_engine.cpp
    speech/wake_word.cpp
    speech/speaker_verifier.cpp
    speech/voiceprint_library.cpp

    brain/skill_manager.cpp

    brain/skills/skill_weather.cpp
    brain/skills/skill_time.cpp
    brain/skills/skill_search.cpp
    brain/skills/skill_rag.cpp

    llm/llm_engine.cpp
    llm/prompt_builder.cpp
    llm/token_counter.cpp
    llm/embedding_engine.cpp
    llm/function_caller.cpp
    llm/react_engine.cpp
    llm/reflection.cpp
    llm/multi_agent.cpp

    memory/chat_memory.cpp
    memory/document_chunker.cpp

    audio/audio_io.cpp
    audio/vad.cpp
)

set(HEADERS
    pipeline/voice_pipeline.h
    pipeline/config.h

    speech/asr_engine.h
    speech/streaming_asr.h
    speech/tts_engine.h
    speech/wake_word.h
    speech/speaker_verifier.h
    speech/voiceprint_library.h
    speech/espeak_min.h

    brain/skill_base.h
    brain/skill_utils.h
    brain/skill_manager.h

    brain/skills/skill_weather.h
    brain/skills/skill_time.h
    brain/skills/skill_search.h
    brain/skills/skill_rag.h

    llm/llm_engine.h
    llm/prompt_builder.h
    llm/token_counter.h
    llm/embedding_engine.h
    llm/function_caller.h
    llm/react_engine.h
    llm/reflection.h
    llm/multi_agent.h

    memory/chat_memory.h
    memory/vector_store.h
    memory/document_chunker.h

    audio/audio_io.h
    audio/vad.h
    speech/prosody.h
    speech/voice_emotion.h
    audio/denoiser.h
)

# ── 可执行文件 ────────────────────────────────────────
add_executable(voice_pipeline main.cpp ${SOURCES} ${HEADERS})

# ── WebSocket 语音服务 (Layer 4.1) ─────────────────────
set(SERVER_SOURCES
    server_main.cpp
    server/ws_server.cpp
)
add_executable(ws_voice_server ${SERVER_SOURCES} ${SOURCES} ${HEADERS})
target_include_directories(ws_voice_server PRIVATE
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/speech
    ${CMAKE_SOURCE_DIR}/brain
    ${CMAKE_SOURCE_DIR}/brain/skills
    ${CMAKE_SOURCE_DIR}/llm
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/audio
    ${CMAKE_SOURCE_DIR}/server
    ${CMAKE_SOURCE_DIR}/utils
    ${CURL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)
target_link_libraries(ws_voice_server PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ${ESPEAK_LIB}
    pthread
    Boost::system
)
if(SHERPA_ONNX_LIB)
    target_include_directories(ws_voice_server PRIVATE ${SHERPA_ONNX_INCLUDE})
    target_link_libraries(ws_voice_server PRIVATE ${SHERPA_ONNX_LIB})
    target_compile_definitions(ws_voice_server PRIVATE SHERPA_ONNX_AVAILABLE)
endif()
target_compile_options(ws_voice_server PRIVATE -Wall -Wextra -O2)

# 语音情感分析独立测试
add_executable(test_voice_emotion test_voice_emotion.cpp)
target_include_directories(test_voice_emotion PRIVATE
    ${CMAKE_SOURCE_DIR}/speech)

# 流式 ASR 独立测试
add_executable(test_streaming test_streaming_asr.cpp
    speech/streaming_asr.cpp
    speech/asr_engine.cpp)
target_include_directories(test_streaming PRIVATE
    ${CMAKE_SOURCE_DIR}/speech
    ${CMAKE_SOURCE_DIR}/llm
    ${CURL_INCLUDE_DIRS})
target_link_libraries(test_streaming PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    pthread)
if(SHERPA_ONNX_LIB)
    target_include_directories(test_streaming PRIVATE ${SHERPA_ONNX_INCLUDE})
    target_link_libraries(test_streaming PRIVATE ${SHERPA_ONNX_LIB})
    target_compile_definitions(test_streaming PRIVATE SHERPA_ONNX_AVAILABLE)
endif()

target_include_directories(voice_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/pipeline
    ${CMAKE_SOURCE_DIR}/speech
    ${CMAKE_SOURCE_DIR}/brain
    ${CMAKE_SOURCE_DIR}/brain/skills
    ${CMAKE_SOURCE_DIR}/llm
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/audio
    ${CMAKE_SOURCE_DIR}/utils
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
