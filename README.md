# ASR-LLM-TTS 本地语音交互（C++ 版）

**语音交互管线：ASR → 唤醒词 → 声纹 → LLM → TTS → 播放**，纯 C++ 实现。

## 快速开始

```bash
# 一键安装依赖 + 下载模型 + 编译 + 运行
./setup.sh --run

# 或者分步执行
./setup.sh              # 安装依赖 + 下载模型 + 编译
./setup.sh --models     # 仅下载模型
./setup.sh --build      # 仅编译
./setup.sh --clean      # 清理编译产物
```

首次运行前确保已安装 Ollama 并拉取模型：

```bash
curl -fsSL https://ollama.com/install.sh | sh
ollama pull qwen2.5:1.5b
```

**必须在项目根目录运行**（模型路径使用相对路径）。

### 交互命令

| 输入 | 功能 |
|---|---|
| 任意文字 | 直接和 LLM 对话 |
| `r` | 单次录音 → 唤醒词 → 声纹验证 → LLM → 语音回复 |
| **`listen`** | **🎤 交互模式（持续监听 + 语音打断）** |
| `enroll` | 录制声纹（说 3 秒以上） |
| `clear` | 清空对话记忆 |
| `quit` | 退出 |

### 语音打断（Barge-in）

输入 `listen` 进入交互模式后：

- 麦克风持续监听，**无需按键触发**
- 机器人说话时，**你随时开口即可打断**
- 打断后立刻开始听你的新指令
- 按 `Ctrl+C` 退出交互模式

```
┌─ 麦克风持续录音 ────────────────────────────────────┐
│                                                     │
│  你说 "今天天气怎么样"                                │
│     ↓ ASR → LLM → TTS → 播放                        │
│         "今天天气不错，适合..."  🔊                   │
│              ↓                                      │
│         你开口说 "等一下"                             │
│              ↓                                      │
│         🔴 立刻打断播放！                             │
│              ↓                                      │
│         识别 "等一下" → LLM → 播放新回复              │
│                                                     │
└─────────────────────────────────────────────────────┘
```

**VAD 灵敏度调节**：如果打断太灵敏（噪音误触发）或太迟钝（说话不打断），修改 `src/include/vad.h` 中的参数：

```cpp
struct VADConfig {
    int   sample_rate        = 16000;
    float energy_threshold   = 0.003f;    // RMS 阈值 — 改这个最有效
    int   min_speech_frames  = 8;         // 最小语音帧数 (~160ms)
    int   min_silence_frames = 30;        // 静音多少帧后判断结束 (~600ms)
    int   pre_speech_frames  = 15;        // 保留语音前的预录音 (~300ms)
    int   frame_size_samples = 320;       // 每帧采样数 (20ms @16kHz)
};
```

| 症状 | 调法 |
|---|---|
| 噪音/呼吸误触发打断 | 增大 `energy_threshold`（如 0.005→0.01） |
| 正常说话不触发打断 | 减小 `energy_threshold`（如 0.003→0.001） |
| 短促声音误触发 | 增大 `min_speech_frames`（如 8→15，~300ms） |
| 说话停顿被打断分段 | 增大 `min_silence_frames`（如 30→50，~1s） |

修改后重新编译即可生效：
```bash
./setup.sh --build
```

## 数据流

### 单次模式（`r` / 文字输入）

```
麦克风 → PCM → SenseVoice(ASR) → 拼音匹配(唤醒词) → CAM++(声纹) → Ollama(LLM) → espeak-ng(TTS) → 播放
                                                   ↑
                                             ChatMemory(对话历史)
```

### 交互模式（`listen` — 支持语音打断）

```
┌─ Capture 线程 ───────────────────────────────────────────┐
│  arecord --stdout → popen 管道 → 20ms 帧                 │
│    ↓                                                     │
│  EnergyVAD 持续检测                                       │
│    ├─ 检测到语音 + 正在播放 → kill(aplay) 🔴 打断        │
│    └─ 语音段结束 → 入队 → generation++                    │
└────────────────────────┬─────────────────────────────────┘
                         │ queue
┌─ Process 线程 ────────┼─────────────────────────────────┐
│  pop → WAV → ASR → KWS → LLM → TTS → play_async(aplay) │
│    └─ 每步检查 generation → 过期则丢弃                   │
└──────────────────────────────────────────────────────────┘
```

## 架构

```
src/
├── CMakeLists.txt              # 编译配置
├── main.cpp                    # 入口 + 交互菜单
├── include/                    # 头文件
│   ├── config.h                # PipelineConfig — 管线配置
│   ├── asr_engine.h            # ASREngine — 语音识别（sherpa-onnx SenseVoice）
│   ├── llm_engine.h            # LLMEngine — 大模型推理（Ollama HTTP）
│   ├── tts_engine.h            # TTSEngine — 语音合成（espeak-ng）
│   ├── wake_word.h             # WakeWordDetector — 唤醒词检测（拼音表）
│   ├── speaker_verifier.h      # SpeakerVerifier — 声纹验证（sherpa-onnx CAM++）
│   ├── chat_memory.h           # ChatMemory — 对话记忆
│   ├── audio_io.h              # AudioRecorder / AudioPlayer（ALSA）
│   ├── vad.h                   # EnergyVAD — 能量语音检测（打断检测）
│   └── voice_pipeline.h        # VoicePipeline — 管线编排（单次+交互）
├── src/                        # 实现文件（与头文件一一对应）
│   ├── asr_engine.cpp
│   ├── llm_engine.cpp
│   ├── tts_engine.cpp
│   ├── wake_word.cpp
│   ├── speaker_verifier.cpp
│   ├── chat_memory.cpp
│   ├── audio_io.cpp
│   ├── vad.cpp
│   └── voice_pipeline.cpp
└── third_party/                # sherpa-onnx 库 + 模型（需手动下载）
```

## 环境

| 组件 | 方案 | 说明 |
|---|---|---|
| ASR | SenseVoice Small int8 (~228MB) | sherpa-onnx C API |
| LLM | Ollama (qwen2.5:1.5b) | 本地 HTTP 服务，llama.cpp 底层 |
| TTS | espeak-ng | 系统库，完全离线 |
| VAD | 能量 RMS 阈值 | 纯 C++，无外部依赖 |
| KWS | 400+ 汉字拼音表 | 纯 C++，无外部依赖 |
| 声纹 | CAM++ (~27MB) | sherpa-onnx speaker embedding |
| 音频 | ALSA (arecord/aplay) | Linux 原生 |
| 硬件 | CPU only (i7-14700) | 无需 GPU |

## 许可证

Apache 2.0.
