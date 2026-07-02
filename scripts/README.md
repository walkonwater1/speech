# 可用脚本

所有脚本在 repo 根目录下运行（保证 `cosyvoice/` 在 import 路径中）。

## 核心脚本

### `full_pipeline.py` — 一条龙语音交互 ✅

录音 → ASR → Ollama LLM → TTS → 播放，全离线。

```bash
python scripts/full_pipeline.py
```

| 输入 | 行为 |
|------|------|
| 直接打字 | 跳过录音，LLM 回复 + TTS 播报 |
| `r` + Enter | 录音 → ASR 识别 → LLM 回复 → TTS 播报 |
| `quit` | 退出 |

依赖：SenseVoice (本地模型) + Ollama (本地服务) + pyttsx3 (离线 TTS)

### `run_realtime.py` — 实时打断版 ✅

实时 VAD 检测 + 自动录音 + Ollama LLM + EdgeTTS 语音合成。

```bash
python scripts/run_realtime.py
```

## 测试脚本

### `test_ollama_pipeline.py` — ASR + Ollama LLM ✅

语音/文字输入 → SenseVoice ASR → Ollama LLM 回复（纯文本，不含 TTS）。

```bash
python scripts/test_ollama_pipeline.py
```

支持 `/qwen2` 和 `/qwen3` 切换 Ollama 模型。

### `test_pipeline.py` — ASR + Transformers LLM ✅

全离线版，直接用 transformers 加载 Qwen2.5-0.5B（不需要 Ollama 服务）。

```bash
python scripts/test_pipeline.py
```

> **注意**：需要设置 `HF_ENDPOINT=https://hf-mirror.com` 来下载模型。
> 此脚本不依赖 Ollama，适合离线环境。

## 架构对应

```
Python（实验）                    C++（集成）
─────────────────                ─────────────────
SenseVoice (funasr)         →    sherpa-onnx C API
Ollama / Transformers       →    llama.cpp
pyttsx3 / EdgeTTS           →    sherpa-onnx Kokoro
sounddevice / pyaudio       →    腾讯硬件 SDK
```

## C++ 开发者速查

- `full_pipeline.py` — 对应你最终机器人主循环的结构
- `run_realtime.py` — 对应实时打断场景（VAD + 线程模型）
- 每个函数的输入/输出就是 C++ 中对应 API 的接口定义
