# 脚本入口 & 模块说明

所有脚本在 repo 根目录下运行。

> 💡 功能逻辑已提取到 `src/` 目录，脚本只是薄入口。C++ 开发者直接看 `src/*.py`，
> 每个文件头部有 C++ 伪代码对照。

## 模块架构

```
src/
├── config.py      PipelineConfig      — 所有配置一处改
├── asr.py         ASREngine           — 语音识别 (→ sherpa-onnx)
├── llm.py         LLMEngine           — 大模型推理 (→ llama.cpp)
├── tts.py         TTSEngine           — 语音合成 (→ sherpa-onnx Kokoro)
├── kws.py         WakeWordDetector    — 唤醒词检测 (→ 拼音表)
├── speaker.py     SpeakerVerifier     — 声纹验证 (→ sherpa-onnx SV)
├── memory.py      ChatMemory          — 对话记忆 (→ std::vector)
├── audio_io.py    AudioRecorder/Player— 录音/播放 (→ 硬件SDK)
└── pipeline.py    VoicePipeline       — 管线编排 (→ 机器人主控类)

scripts/  ← 只负责 print 菜单 + 调 VoicePipeline
```

## 核心脚本

### `full_pipeline.py` — 一条龙语音交互

录音 → ASR → Ollama LLM → TTS → 播放。

```bash
python scripts/full_pipeline.py
```

| 输入 | 行为 |
|------|------|
| 直接打字 | 跳过录音，LLM 回复 + TTS 播报 |
| `r` + Enter | 录音 → ASR → LLM → TTS 播报 |
| `quit` | 退出 |

### `full_pipeline_kws_sv.py` — 一条龙 + 唤醒词 + 声纹 + 对话记忆

在 `full_pipeline.py` 基础上增加三个特性：

```bash
python scripts/full_pipeline_kws_sv.py
```

| 输入 | 行为 |
|------|------|
| 直接打字 | 跳过 ASR/唤醒词/声纹，直接 LLM + TTS |
| `r` + Enter | 录音 → ASR → 唤醒词检查 → 声纹验证 → LLM(带记忆) → TTS |
| `enroll` | 声纹注册（说 3 秒以上） |
| `clear` | 清空对话记忆 |
| `quit` | 退出 |

**特性开关**：修改脚本顶部配置变量

```python
WAKE_WORD = "zhan qi lai"   # 唤醒词（拼音），设为 "" 关闭
SV_ENROLL_DIR = "~/speaker_voice/"  # 声纹注册目录
```

**C++ 集成对应**：

| 功能 | Python | C++ |
|------|--------|-----|
| 唤醒词 | ASR→pypinyin→字符串匹配 | ASR→拼音表查找 |
| 声纹 | CAM++ (modelscope) | sherpa-onnx speaker verification C API |
| 记忆 | `ChatMemory` 类 | `std::vector<pair<string,string>>` |

### `run_realtime.py` — 实时打断版

实时 VAD 检测 + 自动录音 + Ollama LLM + TTS。

```bash
python scripts/run_realtime.py
```

## 测试脚本

### `test_ollama_pipeline.py` — ASR + Ollama LLM

语音/文字输入 → ASR → Ollama LLM（纯文本，不含 TTS）。
支持 `/qwen2` `/qwen3` 切换模型。

### `test_pipeline.py` — 离线 ASR + Transformers LLM

不需要 Ollama 服务，直接加载 Qwen2.5-0.5B。（需要 `HF_ENDPOINT=https://hf-mirror.com`）

## 架构对应

```
Python（实验）                    C++（集成）
─────────────────                ─────────────────
SenseVoice (funasr)         →    sherpa-onnx C API
Ollama API                  →    llama.cpp
pyttsx3                     →    sherpa-onnx Kokoro
sounddevice                 →    腾讯硬件 SDK
CAM++ (modelscope)          →    sherpa-onnx speaker verification
pypinyin (唤醒词匹配)        →    C++ 拼音表字符串匹配
ChatMemory (对话记忆)        →    std::vector<pair<string,string>>
```
