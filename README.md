# ASR-LLM-TTS 本地语音交互（C++ 版）


**语音交互管线：ASR → 唤醒词 → 声纹 → LLM → TTS → 播放**，纯 C++ 实现。

## 快速开始

### 1. 安装依赖

```bash
# 系统库
sudo apt install -y espeak-ng libcurl4 nlohmann-json3-dev cmake g++

# Ollama（LLM 后台服务）
curl -fsSL https://ollama.com/install.sh | sh
ollama pull qwen2.5:1.5b
```

### 2. 下载 sherpa-onnx + 模型

```bash
# 下载 sherpa-onnx 运行时库
# 从 https://github.com/k2-fsa/sherpa-onnx/releases 下载 Linux x64 版本
# 解压到 src/third_party/sherpa-onnx/

# 下载 ASR 模型（SenseVoice Small int8 ~228MB）
# 下载到 src/third_party/sherpa-onnx/sense-voice-model/
#   - model.int8.onnx
#   - tokens.txt

# 下载声纹模型（CAM++ ~27MB）
# 下载到 src/third_party/sherpa-onnx/speaker-verification-model/
#   - 3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx
```

### 3. 编译运行

```bash
cd src && mkdir -p build && cd build
cmake .. && make -j$(nproc)
cd ../..  # 回到项目根目录
./src/build/voice_pipeline
```

**必须在项目根目录运行**（模型路径使用相对路径）。

### 4. 交互命令

| 输入 | 功能 |
|---|---|
| 任意文字 | 直接和 LLM 对话 |
| `r` | 录音 → 唤醒词 → 声纹验证 → LLM → 语音回复 |
| `enroll` | 录制声纹（说 3 秒以上） |
| `clear` | 清空对话记忆 |
| `quit` | 退出 |

## 数据流

```
麦克风 → PCM → SenseVoice(ASR) → 拼音匹配(唤醒词) → CAM++(声纹) → Ollama(LLM) → espeak-ng(TTS) → 播放
                                                   ↑
                                             ChatMemory(对话历史)
```

## 架构

```
src/
├── CMakeLists.txt              # 编译配置
├── main.cpp                    # 入口 + 交互菜单
├── include/                    # 头文件
│   ├── config.h                # PipelineConfig — 管线配置
│   ├── asr_engine.h            # ASREngine — 语音识别（sherpa-onnx）
│   ├── llm_engine.h            # LLMEngine — 大模型推理（Ollama HTTP）
│   ├── tts_engine.h            # TTSEngine — 语音合成（espeak-ng）
│   ├── wake_word.h             # WakeWordDetector — 唤醒词检测（拼音表）
│   ├── speaker_verifier.h      # SpeakerVerifier — 声纹验证（sherpa-onnx）
│   ├── chat_memory.h           # ChatMemory — 对话记忆
│   ├── audio_io.h              # AudioRecorder / AudioPlayer（ALSA）
│   └── voice_pipeline.h        # VoicePipeline — 管线编排
├── src/                        # 实现文件（与头文件一一对应）
│   ├── asr_engine.cpp
│   ├── llm_engine.cpp
│   ├── tts_engine.cpp
│   ├── wake_word.cpp
│   ├── speaker_verifier.cpp
│   ├── chat_memory.cpp
│   ├── audio_io.cpp
│   └── voice_pipeline.cpp
└── third_party/                # sherpa-onnx 库 + 模型（需手动下载）
```

## 环境

| 组件 | 方案 | 说明 |
|---|---|---|
| ASR | SenseVoice Small int8 (~228MB) | sherpa-onnx C API |
| LLM | Ollama (qwen2.5:1.5b) | 本地 HTTP 服务，llama.cpp 底层 |
| TTS | espeak-ng | 系统库，完全离线 |
| KWS | 400+ 汉字拼音表 | 纯 C++，无外部依赖 |
| 声纹 | CAM++ (~27MB) | sherpa-onnx speaker embedding |
| 音频 | ALSA (arecord/aplay) | Linux 原生 |
| 硬件 | CPU only (i7-14700) | 无需 GPU |

## 许可证

Apache 2.0.
