# ASR-LLM-TTS 本地语音交互（Linux 精简版）

基于 [ABexit/ASR-LLM-TTS](https://github.com/ABexit/ASR-LLM-TTS)，感谢原作者贡献。

**语音交互管线：ASR → 唤醒词 → 声纹 → LLM → TTS → 播放**。

## 快速开始

```bash
conda activate chatAudio

# 基础版：语音/文字 → LLM → TTS
python scripts/full_pipeline.py

# 完整版：语音/文字 → 唤醒词 → 声纹 → LLM(带记忆) → TTS
python scripts/full_pipeline_kws_sv.py
```

打字聊天，或输入 `r` 录音对话。

## 目录结构

```
src/                                  🔧 Python 功能模块（每个对应一个 C++ 实现）
  config.py                             PipelineConfig — 管线配置结构体
  asr.py                                ASREngine — 语音识别（→ sherpa-onnx）
  llm.py                                LLMEngine — 大模型推理（→ llama.cpp）
  tts.py                                TTSEngine — 语音合成（→ sherpa-onnx Kokoro）
  kws.py                                WakeWordDetector — 唤醒词检测（→ 拼音表）
  speaker.py                            SpeakerVerifier — 声纹验证（→ sherpa-onnx SV）
  memory.py                             ChatMemory — 对话记忆（→ std::vector）
  audio_io.py                           AudioRecorder / AudioPlayer（→ 硬件SDK）
  pipeline.py                           VoicePipeline — 管线编排（→ 机器人主控类）

cpp/                                  🔨 C++ 实现（可直接编译运行）
  include/                              头文件（每个 .h 对应一个 Python 模块）
  src/                                  实现文件
  main.cpp                              Demo 主函数
  CMakeLists.txt                         编译配置
  build/voice_pipeline                   编译产物

scripts/                              📜 Python 脚本入口（薄层，只调 src/ 模块）
  full_pipeline.py                     基础版：ASR → LLM → TTS
  full_pipeline_kws_sv.py              完整版：+唤醒词+声纹+对话记忆
  run_realtime.py                      实时打断版（VAD）
  test_ollama_pipeline.py              ASR + Ollama LLM 测试
  test_pipeline.py                     离线 ASR + Transformers LLM 测试

archive/                              🗄️ 原始参考脚本
  2_record_test.py                     录音测试
  5_pyttsx3_test.py                    pyttsx3 TTS 测试
  6_Inference_funasr.py                SenseVoice ASR 测试

docs/                                 📖 文档

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
麦克风 → PCM → SenseVoice(ASR) → 拼音匹配(唤醒词) → CAM++(声纹) → Ollama(LLM) → pyttsx3(TTS) → 播放
                                                    ↑
                                              ChatMemory(对话历史)
```

## C++ 集成路线

`src/` 下每个 Python 类直接对应一个 C++ 类，接口风格刻意参考 C++:

| src/ 模块 | Python 类 | C++ 实现 |
|-----------|----------|---------|
| `asr.py` | `ASREngine` | sherpa-onnx SenseVoice C API |
| `llm.py` | `LLMEngine` | llama.cpp |
| `tts.py` | `TTSEngine` | sherpa-onnx Kokoro / espeak |
| `kws.py` | `WakeWordDetector` | 拼音查找表 + 字符串匹配 |
| `speaker.py` | `SpeakerVerifier` | sherpa-onnx speaker verification |
| `memory.py` | `ChatMemory` | `std::vector<pair<string,string>>` |
| `audio_io.py` | `AudioRecorder` / `AudioPlayer` | 机器人硬件 SDK |
| `pipeline.py` | `VoicePipeline` | 机器人主控类 |
| `config.py` | `PipelineConfig` | 配置结构体 |

每个 `.py` 文件头部有 C++ 伪代码，可直接对照阅读。

> Ollama 底层就是 llama.cpp，Python 和 C++ 共用同一个 GGUF 模型，推理效果一致。

详见 [docs/LEARNING_ROADMAP.md](docs/LEARNING_ROADMAP.md)。

## 修改记录

### 2026-07-02（续3）：C++ 实现

**结果**：`cpp/` 目录，9 个头文件 + 8 个实现文件，零 warning 编译通过。

| 模块 | 状态 | 依赖 |
|------|------|------|
| LLM (llm_engine) | ✅ 可用 | libcurl + nlohmann/json → Ollama HTTP |
| TTS (tts_engine) | ✅ 可用 | libespeak-ng.so (系统自带) |
| KWS (wake_word) | ✅ 可用 | 纯 C++，400+ 汉字拼音表 |
| 记忆 (chat_memory) | ✅ 可用 | std::deque |
| 音频 (audio_io) | ✅ 可用 | ALSA arecord/aplay |
| ASR (asr_engine) | ⏳ 等待 sherpa-onnx | 取消 `#define SHERPA_ONNX_AVAILABLE` 注释即可 |
| 声纹 (speaker_verifier) | ⏳ 等待 sherpa-onnx | 同上 |

编译运行:
```bash
cd cpp && mkdir build && cd build
cmake .. && make -j
./voice_pipeline
```

### 2026-07-02（续2）：模块化重构 — C++ 可读

**问题**：所有逻辑散落在各脚本中，C++ 开发者难以对照理解。

**修改**：
1. 新增 `src/` 目录，8 个独立模块，每个对应一个 C++ 实现方向
2. 每个 `.py` 文件头部写 C++ 伪代码，可直接对照阅读
3. 所有模块用类封装，接口刻意按 C++ 风格设计（构造函数 → initialize → 方法调用）
4. `VoicePipeline` 类统一编排，等价于机器人的主控类
5. 脚本缩减为薄入口（~50 行），只负责打印菜单 + 调管线

**结果**：`src/` 模块化 → C++ 开发者可逐文件对照实现。

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

**结果**：从 36 个 py 文件 + 10 个子目录 → 8 个 py 文件 + 3 个目录。

### 2026-07-02（续）：恢复唤醒词 + 声纹功能

从 git 历史恢复 `15.1_SenceVoice_kws_CAM++.py`，适配 Linux：

1. **修复路径**：Windows `E:\...` → Linux `~/pretrained_models/`
2. **LLM 换 Ollama**：删除 transformers 本地加载，改用 `ollama.chat()`
3. **TTS 换 pyttsx3**：删除 edge-tts（网络依赖），改用本地 pyttsx3
4. **删除视频依赖**：去除 cv2、pyaudio，改用 sounddevice
5. **新增 `full_pipeline_kws_sv.py`**：整合唤醒词+声纹+记忆+ASR+LLM+TTS

## 许可证

Apache 2.0. 原始项目作者 [ABexit](https://github.com/ABexit/ASR-LLM-TTS)。
