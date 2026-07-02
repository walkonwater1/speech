# ASR-LLM-TTS 本地语音交互（Linux 精简版）

基于 [ABexit/ASR-LLM-TTS](https://github.com/ABexit/ASR-LLM-TTS)，感谢原作者贡献。

离线语音交互管线：**ASR（语音识别）→ LLM（大模型）→ TTS（语音合成）**。

## 快速开始

```bash
conda activate chatAudio
python scripts/full_pipeline.py
```

打字聊天，或输入 `r` 录音对话。

## 目录结构

```
scripts/                         ✅ 核心脚本（你只需要这里）
  full_pipeline.py                一条龙：录音→ASR→LLM→TTS→播放
  run_realtime.py                 实时打断版（VAD）
  test_ollama_pipeline.py         ASR + Ollama LLM
  test_pipeline.py                离线 ASR + Transformers LLM

archive/                         🗄️ 原始参考脚本
  2_record_test.py                录音测试
  5_pyttsx3_test.py               pyttsx3 TTS 测试
  6_Inference_funasr.py           SenseVoice ASR 测试

docs/                            📖 文档
  LEARNING_ROADMAP.md              C++ 集成学习路线
  SESSION_RESUME.md                会话恢复指南
  FAQ.md                           常见问题
```

## 环境

| 组件 | 方案 | 说明 |
|------|------|------|
| Python | 3.10 (conda `chatAudio`) | |
| ASR | SenseVoice Small (~200MB) | `~/pretrained_models/SenseVoiceSmall/` |
| LLM | Ollama (qwen2.5:1.5b / qwen3:4b) | 后台服务，无需本地加载模型 |
| TTS | pyttsx3 | 完全离线，不依赖网络 |
| 硬件 | CPU only (i7-14700) | 无 GPU，ASR RTF 0.04，LLM ~0.3s |

## 数据流

```
麦克风 → PCM → SenseVoice(ASR) → 文本 → Ollama(LLM) → 回复 → pyttsx3(TTS) → 播放
```

## C++ 集成路线

Python 脚本用于验证模型效果。最终机器人集成替换为：

| 模块 | Python（实验） | C++（集成） |
|------|---------------|------------|
| ASR | funasr SenseVoice | sherpa-onnx C API |
| LLM | Ollama API | llama.cpp |
| TTS | pyttsx3 | sherpa-onnx Kokoro |

> Ollama 底层就是 llama.cpp，Python 和 C++ 共用同一个 GGUF 模型，推理效果一致。

详见 [docs/LEARNING_ROADMAP.md](docs/LEARNING_ROADMAP.md)。

## 修改记录

### 2026-07-02：仓库规范化 & 精简

**问题**：原始仓库面向 Windows + GPU + CosyVoice 生态，36 个脚本散落根目录，大量路径和依赖在 Linux 下不可用。

**修改思路**：

1. **删除视频/视觉相关内容**
   - 删除 `audio_visual/`、Qwen2-VL 脚本、`asset/` — 本项目只需语音交互

2. **删除 CosyVoice 生态**
   - 删除 `cosyvoice/`、`third_party/Matcha-TTS/`、`examples/`、`runtime/`、`docker/`、`tools/`、`webui/`
   - CosyVoice 模型未下载，依赖复杂，C++ 集成也不走这条路
   - TTS 改为 pyttsx3（完全离线，Linux 原生支持）

3. **删除 Windows 硬编码路径脚本（18个）**
   - 所有含 `E:\2_PYTHON\...` 路径的脚本直接删除，Linux 下无法运行

4. **LLM 改用 Ollama API**
   - 不需要 `transformers` 本地加载模型（省显存、省加载时间）
   - Ollama 后台运行，Python 通过 `ollama.chat()` 调用
   - 对应 C++ 集成直接用 llama.cpp

5. **目录规范化**
   - `scripts/` — 核心可用脚本
   - `archive/` — 原始参考脚本
   - `docs/` — 文档集中管理
   - 修复 `cosyvoice/flow/flow_matching.py` 内嵌 Windows 路径

**结果**：从 36 个 py 文件 + 10 个子目录 → 7 个 py 文件 + 3 个目录。只保留语音交互核心链路，可直接在 Linux CPU 环境运行。

## 许可证

Apache 2.0. 原始项目作者 [ABexit](https://github.com/ABexit/ASR-LLM-TTS)。
