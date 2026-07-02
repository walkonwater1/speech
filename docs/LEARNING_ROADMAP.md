# 学习路线图：从 Python 仓库到 C++ 机器人本地语音交互

> **目标**：理解 ASR-LLM-TTS 本地推理全链路，最终将本地大模型集成到腾讯硬件 + C++ 机器人系统中，替代云端 API。

---

## 一、现状梳理

你有两套系统：

| 维度 | 机器人产线（工作系统） | 目标方案（本地推理） |
|------|----------------------|---------------------|
| 编程语言 | C++ | C++ |
| 音频前端 | 腾讯硬件 + 前端声学算法 | **不变** |
| VAD | 腾讯云端 / 硬件自带 | 本地 WebRTC VAD（C 库） |
| ASR | 腾讯云端 API → 文本 | 本地 SenseVoice（sherpa-onnx） |
| LLM | 云端大模型 API → 文本 | 本地 Qwen2.5（llama.cpp） |
| TTS | 云端 API → 音频 PCM | 本地 Kokoro/Matcha-TTS（sherpa-onnx） |
| 声纹识别 | 无/云端 | 本地 CAM++（sherpa-onnx） |

**关键原则**：腾讯硬件和前端声学**完全不动**。你只替换云端 API 调用链路。

```
当前架构：
  腾讯硬件 → 前端声学(腾讯) → 云端API(ASR/LLM/TTS) → 播报
                                  ↑ 你要替换这一块

目标架构：
  腾讯硬件 → 前端声学(腾讯) → 本地推理(sherpa-onnx+llama.cpp) → 播报
```

---

## 二、你需要先配本地大模型吗？

**不需要一步到位。** 按下面的阶段来。

---

## 三、三阶段路线图

```
Phase 1            Phase 2              Phase 3
Python学习         模型评估验证          C++机器人集成
第1-2周            第3-4周               第5-8周

┌──────────┐     ┌──────────┐         ┌──────────────┐
│ 跑通仓库  │ ──▶ │ 验证效果  │ ──▶    │ 嵌入机器人代码 │
│ 理解概念  │     │ 选定模型  │        │ 替换云端API    │
└──────────┘     └──────────┘         └──────────────┘
  学习阶段           实验阶段              工程阶段
```

---

## Phase 1：Python 学习 & 仓库跑通（第 1-2 周）

**目标**：能在这个仓库上做实验、改参数、理解每个模块的输入输出。

### 1.1 Python 速成（2-3 天）

C++ 开发者只需要学这些：

| 主题 | 时间 | 学到什么程度 |
|------|------|------------|
| 基本语法、函数、类 | 半天 | 能读写 Python 代码 |
| `pip` / `conda` / 虚拟环境 | 半天 | 能装包、切换环境 |
| `import` 机制 | 0.5 天 | 理解模块导入 |
| `numpy` 张量操作 | 0.5 天 | 等价于 C++ `std::vector` |
| `torch.Tensor` 基本操作 | 半天 | 知道 tensor 是什么 |
| 读 transformers 调用代码 | 0.5 天 | 理解 `from_pretrained` → `generate` 模式 |

> **不用学**：装饰器、元类、async/await 高级用法、Python C 扩展。

参考：[廖雪峰 Python 教程](https://www.liaoxuefeng.com/wiki/1016959663602400)（只看"Python 基础"+"函数"+"面向对象"三章即可）

### 1.2 环境搭建（1-2 天）

```bash
# 1. 创建虚拟环境
conda create -n chatAudio python=3.10
conda activate chatAudio

# 2. 安装 PyTorch（根据你的 GPU 选版本）
#    CUDA 11.8:
pip install torch==2.3.1 torchvision==0.18.1 torchaudio==2.3.1 --index-url https://download.pytorch.org/whl/cu118
#    CUDA 12.1:
#    pip install torch==2.3.1 torchvision==0.18.1 torchaudio==2.3.1 --index-url https://download.pytorch.org/whl/cu121

# 3. 简易版依赖（不含 CosyVoice，先跑通基本管线）
pip install edge-tts==6.1.17 funasr==1.1.12 opencv-python==4.10.0.84 \
    transformers==4.45.2 webrtcvad==2.0.10 pygame==2.6.1 \
    langid==1.1.6 langdetect==1.0.9 accelerate==0.33.0 PyAudio==0.2.14

# 4. 验证：跑最简单的 TTS 脚本
python 3_Inference_edgeTTS.py
```

### 1.3 按顺序跑通脚本（3-5 天）

**从简单到复杂，每跑通一个理解一个模块**：

| 顺序 | 脚本 | 理解什么 | 依赖 |
|------|------|---------|------|
| 1 | `3_Inference_edgeTTS.py` | TTS 怎么把文本变成语音 | edge-tts（网络） |
| 2 | `0_Inference_QWen2.5.py` | LLM 怎么对话 | transformers, torch |
| 3 | `6_Inference_funasr.py` | ASR 怎么把语音变成文本 | funasr |
| 4 | `2_record_test.py` | 音频采集怎么做 | pyaudio, sounddevice |
| 5 | `12_SenceVoice_QWen2.5_edgeTTS.py` | ASR→LLM→TTS 完整离线管线 | 上述全部 |
| 6 | `13_SenceVoice_QWen2.5_edgeTTS_realTime.py` | 加上 VAD 实时打断 | webrtcvad |
| 7 | `15.1_SenceVoice_kws_CAM++.py` | 加上唤醒词+声纹+对话记忆 | pypinyin, modelscope |

**每跑通一个，做以下笔记**：
- 输入是什么格式？（音频文件？文本？采样率？）
- 输出是什么格式？（字符串？wav 文件？PCM？）
- 延迟大概多少秒？
- 这个模块在你的机器人场景下能不能直接用？

### 1.4 可选：跑通 CosyVoice（额外 1-2 天）

CosyVoice 是本地 TTS，质量高但依赖复杂：

```bash
# 安装 pynini（通过 conda，pip 经常失败）
conda install -c conda-forge pynini=2.1.6
pip install WeTextProcessing --no-deps

# 其它依赖
pip install HyperPyYAML==1.2.2 modelscope==1.15.0 onnxruntime==1.19.2 \
    openai-whisper==20231117 sounddevice==0.5.1 matcha-tts==0.0.7.0

# 验证
python 10_SenceVoice_QWen2.5_cosyVoice.py
```

**Phase 1 完成标志**：
- [ ] 能独立跑通 `13_SenceVoice_QWen2.5_edgeTTS_realTime.py`
- [ ] 能修改 system prompt、TTS 音色、VAD 灵敏度等参数并观察效果
- [ ] 理解了数据流：`麦克风 → PCM buffer → VAD切段 → wav文件 → ASR文本 → LLM prompt → LLM回复 → TTS合成 → 音频播放`
- [ ] 知道每个模块在你的机器人场景中对应什么

---

## Phase 2：模型评估 & 方案验证（第 3-4 周）

**目标**：确定哪些本地模型能替代云端 API，达不到要求的保留云端。

### 2.1 需要评估的维度

| 维度 | 评估方法 | 合格标准（参考） |
|------|---------|----------------|
| **ASR 准确率** | 在真实机器人环境中录音测试 | 中文 > 95% |
| **ASR 延迟** | 计时 `model_senceVoice.generate()` | < 500ms（实时场景） |
| **LLM 回复质量** | 用机器人典型对话场景测试 | 回复合理、不胡编 |
| **LLM 首 token 延迟** | 计时 `model.generate()` | < 2s（1.5B 模型） |
| **TTS 合成质量** | 主观听感 + 自然度 | 可接受即可 |
| **TTS 合成延迟** | 计时合成第一步到输出音频 | < 1s |
| **端到端延迟** | 说完话 → 听到回复 | < 3s |
| **GPU 显存占用** | `nvidia-smi` 监控 | 1.5B 模型 < 4GB |

### 2.2 模型方案对比

#### ASR 方案

| 方案 | Python（学习用） | C++（集成用） |
|------|-----------------|-------------|
| SenseVoice | `funasr.AutoModel` | sherpa-onnx SenseVoice C API |
| 优势 | 开箱即用 | 纯 C++，支持流式，多平台 |
| 劣势 | 依赖 PyTorch | 需要先导出 ONNX |

→ **结论**：直接用 sherpa-onnx 的 SenseVoice，Python 和 C++ 用同一个 ONNX 模型，效果一致。

#### LLM 方案

| 方案 | Python（学习用） | C++（集成用） |
|------|-----------------|-------------|
| Qwen2.5-0.5B | `transformers` | llama.cpp GGUF |
| Qwen2.5-1.5B | `transformers` | llama.cpp GGUF |
| Qwen2.5-7B | `transformers` + 量化 | llama.cpp GGUF（需大显存） |

→ **结论**：从 1.5B 开始验证，如果不够聪明再上 7B 量化版。

#### TTS 方案

| 方案 | 类型 | C++ 集成 | 质量 | 延迟 |
|------|------|---------|------|------|
| Edge-TTS | 网络 API | ❌ 需网络 | ★★★ | 高（网络往返） |
| CosyVoice | 本地 PyTorch | ❌ 无 C++ 支持 | ★★★★★ | 低 |
| Kokoro (sherpa-onnx) | 本地 ONNX | ✅ C API | ★★★★ | 低 |
| Matcha-TTS (sherpa-onnx) | 本地 ONNX | ✅ C API | ★★★★ | 低 |

→ **结论**：C++ 集成用 sherpa-onnx 的 Kokoro（53 说话人，中英双语）或 Matcha-TTS（质量更高但需搭配 vocoder）。CosyVoice 的 C++ 路径不成熟，放弃。

> **注意**：CosyVoice 内部用的就是 Matcha-TTS 作为声学模型。sherpa-onnx 直接支持 Matcha-TTS，所以效果接近。

### 2.3 GPU 要求估算

| 模型组合 | 显存需求 | 推荐 GPU |
|---------|---------|---------|
| SenseVoice + Qwen2.5-0.5B + Kokoro | ~2.5 GB | GTX 1660 / RTX 2060 |
| SenseVoice + Qwen2.5-1.5B + Kokoro | ~4 GB | RTX 3060 / RTX 4060 |
| SenseVoice + Qwen2.5-7B(q4) + Kokoro | ~8 GB | RTX 3080 / RTX 4080 |

### 2.4 模型下载清单

| 模型 | 下载地址 | 大小 | 用途 |
|------|---------|------|------|
| SenseVoice Small | [ModelScope](https://www.modelscope.cn/models/iic/SenseVoiceSmall/files) | ~200MB | ASR |
| sherpa-onnx SenseVoice | [GitHub Releases](https://github.com/k2-fsa/sherpa-onnx/releases) | ~200MB | C++ ASR |
| Qwen2.5-1.5B-Instruct | [ModelScope](https://www.modelscope.cn/models/qwen/Qwen2.5-1.5B-Instruct/files) | ~3GB | LLM (PyTorch) |
| Qwen2.5-1.5B-Instruct-GGUF | [HuggingFace](https://huggingface.co/models?search=Qwen2.5-1.5B-Instruct-GGUF) | ~1.5GB | LLM (llama.cpp) |
| CAM++ | [ModelScope](https://www.modelscope.cn/models/damo/speech_campplus_sv_zh-cn_16k-common/files) | ~10MB | 声纹 |
| Kokoro 中文 | [GitHub Releases](https://github.com/k2-fsa/sherpa-onnx/releases) | ~330MB | C++ TTS |

**Phase 2 完成标志**：
- [ ] 在 Python 仓库中验证了所有模型的输出质量
- [ ] 确定了最终方案的模型组合（选哪个 LLM 大小、选哪个 TTS）
- [ ] 明确了哪些用本地推理、哪些保留云端（如有）
- [ ] 记录了各模块的延迟和显存占用数据

---

## Phase 3：C++ 机器人集成（第 5-8 周）

**目标**：用 sherpa-onnx + llama.cpp 实现本地推理链路，嵌入现有机器人 C++ 代码。

### 3.1 技术栈确认

```
┌───────────────────────────────────────────────┐
│              现有 C++ 机器人代码                │
├───────────────────────────────────────────────┤
│ 腾讯硬件 SDK  │  音频采集  │  音频播放  │  其它  │
└───────────────────────────────────────────────┘
         ↓ 新增三个推理库
┌──────────────────────────────────────────────┐
│ sherpa-onnx  │  ASR + VAD + 声纹 + TTS        │
│ llama.cpp    │  LLM 文本推理                   │
└──────────────────────────────────────────────┘
```

### 3.2 集成步骤

#### Step 1：编译 sherpa-onnx（1 天）

```bash
git clone https://github.com/k2-fsa/sherpa-onnx.git
cd sherpa-onnx
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
# 产出 libsherpa-onnx.a / libsherpa-onnx.so
```

#### Step 2：编译 llama.cpp（半天）

```bash
git clone https://github.com/ggerganov/llama.cpp.git
cd llama.cpp
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DGGML_CUDA=ON ..  # 如果有 NVIDIA GPU
make -j$(nproc)
# 产出 libllama.a
```

#### Step 3：写本地推理管理层（核心工作，2-3 周）

在你的机器人 C++ 项目中新增一个 `LocalInferenceEngine` 类：

```cpp
// local_inference_engine.h
#pragma once
#include <string>
#include <functional>
#include "sherpa-onnx/c-api/c-api.h"
#include "llama.h"

class LocalInferenceEngine {
public:
    // ---- 初始化 ----
    bool Initialize(const std::string& model_dir);

    // ---- ASR ----
    // 输入：PCM 音频数据 (16kHz, 16bit, mono)
    // 输出：识别文本
    std::string Recognize(const std::vector<int16_t>& pcm_data);

    // ---- VAD ----
    // 输入：PCM 音频片段
    // 输出：是否有语音活动
    bool IsSpeech(const std::vector<int16_t>& pcm_frame);

    // ---- 声纹验证 ----
    // 输入：两段音频
    // 输出：是否同一说话人
    bool VerifySpeaker(const std::vector<int16_t>& enrolled,
                       const std::vector<int16_t>& test);

    // ---- LLM ----
    // 输入：prompt 文本，回调函数接收流式输出
    // 输出：生成的回复
    std::string GenerateReply(const std::string& prompt,
                              std::function<void(const std::string&)> on_token = nullptr);

    // ---- TTS ----
    // 输入：文本
    // 输出：PCM 音频数据 (采样率取决于模型)
    std::vector<float> Synthesize(const std::string& text, int speaker_id = 0);

    // ---- 清理 ----
    void Shutdown();

private:
    // sherpa-onnx 对象
    const SherpaOnnxOfflineRecognizer* asr_ = nullptr;
    const SherpaOnnxVoiceActivityDetector* vad_ = nullptr;
    const SherpaOnnxSpeakerVerificationModel* sv_ = nullptr;
    const SherpaOnnxOfflineTts* tts_ = nullptr;

    // llama.cpp 对象
    llama_model* llm_model_ = nullptr;
    llama_context* llm_ctx_ = nullptr;
};
```

#### Step 4：对接机器人主循环（1-2 周）

把原来调用云端 API 的地方替换成 `LocalInferenceEngine` 调用：

```cpp
// 原来的代码（云端 API）：
//   std::string text = CloudASR(audio_data);
//   std::string reply = CloudLLM(text);
//   std::vector<uint8_t> audio = CloudTTS(reply);

// 替换后（本地推理）：
LocalInferenceEngine engine;
engine.Initialize("/path/to/models");

while (robot_running) {
    auto pcm = GetAudioFromTencentHardware(); // 这行不变

    if (engine.IsSpeech(pcm)) {               // 本地 VAD
        std::string text = engine.Recognize(pcm);  // 本地 ASR
        if (!text.empty()) {
            std::string reply = engine.GenerateReply(text); // 本地 LLM
            auto tts_audio = engine.Synthesize(reply, 0);   // 本地 TTS
            PlayToSpeaker(tts_audio);            // 这行不变
        }
    }
}
```

### 3.3 渐进式迁移策略

不要一次性替换所有云端 API，逐步切换：

```
第 1 步：只替换 TTS  → 云端 ASR → 云端 LLM → 本地 TTS（验证 C++ TTS 链路）
第 2 步：加入本地 ASR → 本地 ASR → 云端 LLM → 本地 TTS（验证 ASR 准确率）
第 3 步：加入本地 LLM → 本地 ASR → 本地 LLM → 本地 TTS（全链路本地）
```

每步都保留云端 API 作为 fallback：

```cpp
std::string text = engine.Recognize(pcm);
if (text.empty()) {
    text = CloudASR(pcm);  // fallback 到云端
}
```

### 3.4 关键技术细节

**音频格式对齐**：

```
腾讯硬件输出 → 你需要确认的格式
    ↓
转换成 sherpa-onnx 需要的格式：
  - 采样率: 16000 Hz
  - 位深:   16 bit
  - 声道:   单声道 (mono)
  - 格式:   PCM signed int16
```

**LLM 对话记忆管理**：

```cpp
// 参考 15.1 的 ChatMemory 类
class ChatMemory {
    std::vector<std::pair<std::string, std::string>> history_;
    size_t max_tokens_;

    std::string BuildPrompt(const std::string& user_input) {
        std::string context;
        for (auto& [user, assistant] : history_) {
            context += "User: " + user + "\nAssistant: " + assistant + "\n";
        }
        context += "User: " + user_input + "\nAssistant: ";
        // 截断到 max_tokens
        return TruncateToTokens(context, max_tokens_);
    }
};
```

**唤醒词检测**（参考 15.1 的 KWS 逻辑）：

```cpp
// 1. ASR 结果转拼音
std::string pinyin = ToPinyin(asr_text);
// 2. 匹配唤醒词
if (pinyin.find("ni hao xiao qian") != std::string::npos) {
    is_awake = true;
}
// 3. C++ 拼音库: 可以用数据表做简单映射，不需要引入 Python 依赖
```

**Phase 3 完成标志**：
- [ ] sherpa-onnx 在机器人平台上编译通过
- [ ] llama.cpp 在机器人平台上编译通过，能加载 Qwen2.5 GGUF 模型
- [ ] `LocalInferenceEngine` 类完成并通过单元测试
- [ ] 至少一个模块（建议从 TTS 开始）成功替换云端 API
- [ ] 端到端延迟可接受（建议 < 3s）
- [ ] 云端 API fallback 机制工作正常

---

## 四、硬件要求速查

| 组件 | 最低配置 | 推荐配置 |
|------|---------|---------|
| CPU | x86_64, 4 核 | x86_64, 8 核+ |
| RAM | 8 GB | 16 GB+ |
| GPU | 无（CPU 推理也可） | NVIDIA 8GB+ 显存 |
| 存储 | 10 GB 空闲 | 20 GB SSD |
| 操作系统 | Linux (Ubuntu 20.04+) | Linux (Ubuntu 22.04) |
| 麦克风 | 任何支持 16kHz 采样的设备 | 腾讯硬件（已有） |

---

## 五、关键提醒

1. **Python 是你的实验工具，不是你的交付物。** 你不需要成为 Python 专家，能跑实验就够了。

2. **不要试图用 C++ 重写 CosyVoice。** CosyVoice 的 C++ 路径不成熟。用 sherpa-onnx 的 Kokoro/Matcha-TTS 替代，效果接近且免去维护地狱。

3. **从 TTS 开始集成。** TTS 是三个模块中最简单的（文本进、音频出），先集成它跑通 C++ 链路，建立信心后再加 ASR 和 LLM。

4. **保留云端 fallback。** 本地模型出问题时自动切到云端，确保机器人不会"变哑巴"。

5. **GPU 不是必需的。** 如果机器人是嵌入式平台没有 GPU，所有模型都可以 CPU 推理：
   - SenseVoice CPU: RTF ~0.3（实时率的 30%）
   - Qwen2.5-0.5B CPU: 勉强可用
   - Kokoro CPU: RTF ~1.0（基本实时）
   - 总延迟会从 1-2s 变成 5-10s，看能不能接受

6. **先用 1.5B LLM 验证，不行再上 7B。** 1.5B 在限定领域的简单对话够用，显存只需 2GB。7B 量化版需要 6-8GB。

---

## 六、附录：C++ 库参考链接

| 库 | 用途 | 链接 |
|---|------|------|
| sherpa-onnx | ASR + VAD + 声纹 + TTS | https://github.com/k2-fsa/sherpa-onnx |
| sherpa-onnx C API 文档 | API 参考 | https://k2-fsa.github.io/sherpa/onnx/c-api/html/ |
| sherpa-onnx SenseVoice | ASR 模型页 | https://k2-fsa.github.io/sherpa/onnx/sense-voice/ |
| llama.cpp | LLM 推理 | https://github.com/ggerganov/llama.cpp |
| Qwen2.5 GGUF 模型 | LLM 模型下载 | https://huggingface.co/models?search=Qwen2.5-Instruct-GGUF |
| WebRTC VAD (C) | 备选 VAD | `webrtc/common_audio/vad/` |
| PortAudio | 备选音频 I/O | http://www.portaudio.com/ |

---

> **最后更新**：2026-07-01
> **适用对象**：C++ 背景、有机器人语音交互需求、想从云端 API 迁移到本地推理的开发者
