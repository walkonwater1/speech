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

### 运行前准备

```bash
# 1. 激活 conda 环境（Piper 神经网络 TTS 需要）
source ~/miniconda3/etc/profile.d/conda.sh && conda activate chatAudio

# 2. NUC 上启动 Ollama（如果 LLM 部署在另一台机器则跳过）
ollama serve
```

### 运行

```bash
# 必须从项目根目录启动（模型路径使用相对路径）
cd /path/to/ASR-LLM-TTS
./setup.sh --run

# 或者直接运行已编译的二进制
./src/build/voice_pipeline
```

### 模型选择

CPU only 设备（NUC / 无 GPU）推荐以下 Qwen2.5 模型：

| 模型 | 大小 | 速度 | 质量 | 适用场景 |
|------|------|------|------|---------|
| `qwen2.5:0.5b` | 0.5GB | ⚡最快 | 简单对话 | 极限低延迟 |
| `qwen2.5:1.5b` | 1GB | 很快 | 勉强能用 | 资源受限 |
| `qwen2.5:3b` | 2GB | 快 | 日常够用 | 日常首选 |
| `qwen2.5:7b` | 4.7GB | 中等 | 好 | 追求质量 |
| `qwen2.5:14b` | 9GB | 慢 | 更好 | 32GB+ 内存可试 |

```bash
# 按需拉取，可同时安装多个切换对比
ollama pull qwen2.5:3b   # 推荐：速度与质量平衡
ollama pull qwen2.5:7b   # 质量更好
ollama pull qwen2.5:0.5b # 速度最快
```

> **建议**：先从 `qwen2.5:3b` 开始，速度快且日常对话够用；追求更好回答质量再切到 `7b`。

### TTS 后端配置

`config.json` 中 `tts` 字段控制语音合成：

```json
"tts": {
    "rate": 200,                           // 语速（仅 espeak）
    "voice": "cmn+f3",                     // 音色（espeak）/ Piper 由模型决定
    "backend": "piper",                    // "piper" 或 "espeak"
    "piper_model": "~/pretrained_models/piper/zh_CN/zh_CN-xiao_ya-medium.onnx"
}
```

| 后端 | 速度 | 音质 | 说明 |
|------|------|------|------|
| `piper` | ~0.04s 合成 | 自然女声（小雅） | 默认，pypinyin 词典快速模式 |
| `espeak` | <0.01s | 电音/机械 | 纯 C++，无需 Python |

Piper 后端使用 **常驻 Python 进程**（模型只加载一次），搭配 **pypinyin 词典拼音**（~5ms）替代 g2pw BERT 多音字消歧（~2s），实现接近 espeak 的响应速度 + 神经网络的自然音质。

### 多机部署

本项目支持 LLM 与 ASR/TTS 分离部署：

```
┌─ Desktop VM ─────────────────────┐      ┌─ NUC (192.168.10.69) ──┐
│  ASR (SenseVoice)                │      │                         │
│  TTS (Piper / espeak-ng)         │ LAN  │  Ollama (Qwen2.5)      │
│  KWS + 声纹 + VAD                │─────▶│  HTTP API :11434       │
│  VoicePipeline (管线编排)         │      │                         │
└──────────────────────────────────┘      └─────────────────────────┘
```

修改 `config.json` 中 `llm.host` 指向 NUC IP 即可，LLM 推理延迟取决于模型大小和 NUC 负载。

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
麦克风 → PCM → SenseVoice(ASR) → 拼音匹配(唤醒词) → CAM++(声纹) → Ollama(LLM) → Piper/espeak-ng(TTS) → 播放
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
│   ├── tts_engine.h            # TTSEngine — 语音合成（Piper 神经网络 / espeak-ng 双后端）
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
├── piper_server.py              # Piper 常驻服务（pypinyin 快速模式）
└── third_party/                # sherpa-onnx 库 + 模型（需手动下载）
```

## 环境

| 组件 | 方案 | 说明 |
|---|---|---|
| ASR | SenseVoice Small int8 (~228MB) | sherpa-onnx C API |
| LLM | Ollama (Qwen2.5 系列) | HTTP API，支持跨机器部署，0.5b~14b |
| TTS | Piper (xiaoya 女声) | pypinyin 快速模式 ~0.04s，常驻 Python 进程 |
| TTS (备用) | espeak-ng | 系统库，完全离线，电音但 <0.01s |
| VAD | 能量 RMS 阈值 | 纯 C++，无外部依赖 |
| KWS | 400+ 汉字拼音表 | 纯 C++，无外部依赖 |
| 声纹 | CAM++ (~27MB) | sherpa-onnx speaker embedding |
| 音频 | ALSA (arecord/aplay) | Linux 原生 |
| 硬件 | CPU only | 无需 GPU |

## 优化路线图

> 当前状态：Demo 阶段，功能跑通，但离嵌入式板卡部署尚有差距。

---

### P0 — 上板前必须解决

#### 1. 音频 I/O 去 shell 化

**现状**：`popen("arecord ...")` / `popen("aplay ...")` 调系统命令，fork 子进程，延迟不可控。需要反复 `unsetenv("LD_LIBRARY_PATH")` 规避 conda 库冲突。

**目标**：
- 直接调用 ALSA C API（`snd_pcm_open` / `snd_pcm_readi` / `snd_pcm_writei`），去掉 arecord/aplay
- `AudioRecorder` / `AudioPlayer` 抽象为接口，支持注入不同底层实现（板载麦克风 SDK）
- 添加音频预处理链：**AGC（自动增益控制）+ 降噪 + 回声消除**

#### 2. VAD 升级到 ML 方案

**现状**：`EnergyVAD` 只算 RMS 功率 + 固定阈值（`energy_threshold=0.003`），真实环境中风扇、空调、旁人说话都可能导致误触发或漏检。

**目标**：
- 替换为 **WebRTC VAD**（GMM 模型，极轻量，纯 C 嵌入）或 **Silero VAD**（ONNX，~1MB）
- 添加动态噪声门限自适应（前 N 帧 RMS 均值作为基线 + 在线更新）

#### 3. 错误恢复与看门狗

**现状**：各模块出错只打 `std::cerr` + `return false`，无统一错误恢复策略。Ollama 超时/挂掉整条管线卡死。

**目标**：
- 每个模块加超时 + 重试机制
- Ollama 加 health check，挂了给用户语音提示
- 看门狗线程监控 capture/process 线程健康，异常自动重启
- 磁盘空间检测，临时文件写 `tmpfs`，避免磨损 SD 卡

---

### P1 — 显著提升体验

#### 4. 音频级唤醒词（KWS）

**现状**：400 个硬编码汉字的拼音查表，ASR 识别出文字后再做子串匹配。ASR 识别错一个字唤醒就失败。

**目标**：
- 用 **sherpa-onnx-kws** 直接在音频层面做唤醒，不需要先过 ASR
- 两级策略：音频唤醒（高灵敏度）→ ASR 确认（低误触发）
- 覆盖常见汉字（目前 400 字 → 3500+ 常用字），改用 pypinyin 动态查表或用开源拼音字典

#### 5. LLM 流式输出 + 句子级 TTS 流水线

**现状**：`"stream": false`，等 LLM 全部生成完再送 TTS，首字延迟 = LLM 总时间 + TTS 合成时间。

**目标**：
- LLM 端改为 `"stream": true`，libcurl 流式回调逐句吐文本
- TTS 端做**句子切分**：LLM 出一个句子 → 立刻送 Piper 合成播放 → LLM 继续生成下一句
- 探索支持原生流式输入的 TTS 引擎

#### 6. 音频预处理

**现状**：原始 PCM 直接送 ASR/VAD，无任何前端处理。

**目标**：
- AGC（自动增益）：近讲/远讲音量自适应
- 单通道降噪（如 RNNoise）：抑制风扇/空调稳态噪声
- 回声消除（如果板子同时播放和录音）

---

### P2 — 锦上添花

#### 7. 去 Python 依赖（Piper C++ 直调）

**现状**：fork Python 子进程跑 `piper-tts` 库，嵌入式上 Python 运行时 + piper 依赖 ≈ 几百 MB 内存。

**目标**：
- Piper 底层是 C++，探索直接调用 Piper C++ API 加载 ONNX 模型
- 砍掉 Python/conda 依赖，最终产物：一个静态链接的二进制

#### 8. 对话体验增强

| 方面 | 目标 |
|------|------|
| **TTS 表现力** | 多音色支持、语速/音调可调，SSML 标记控制停顿 |
| **对话记忆** | 从简单文本拼接升级到语义记忆（向量数据库存关键事实） |
| **多轮澄清** | 歧义时主动追问："你说的是 A 还是 B？" |
| **音效反馈** | 唤醒成功播放短促提示音，让用户确认 |
| **多场景人格** | 可切换 system prompt（陪聊/教学/设备控制等） |

#### 9. 工程化部署

| 方面 | 目标 |
|------|------|
| **进程管理** | systemd service，开机自启，挂了自动拉起 |
| **路径规范** | 安装根目录 `/opt/voice_pipeline/`，配置放 `/etc/voice_pipeline/` |
| **日志系统** | spdlog 或 syslog，带时间戳、级别、轮转 |
| **编译打包** | CMake install target + `.deb` 包 / Docker 镜像 |
| **远程管理** | HTTP API 或 MQTT 通道，远程下发配置、查看运行状态、OTA 升级 |
| **资源监控** | CPU/内存/磁盘用量上报，异常告警 |

---

### 优先级总览

```
P0（上板前必须）:
  ├─ 音频 I/O 走 ALSA C API，去掉 arecord/aplay shell 调用
  ├─ VAD 换 ML 方案（WebRTC VAD / Silero VAD）
  └─ 错误恢复 + 看门狗

P1（显著提升体验）:
  ├─ 音频级唤醒词（sherpa-onnx-kws）
  ├─ LLM streaming + 句子级 TTS 流水线
  └─ 音频预处理（AGC + 降噪）

P2（锦上添花）:
  ├─ Piper C++ 直调，去 Python/conda
  ├─ 对话体验增强（多音色/情感/提示音）
  └─ 工程化部署（systemd/日志/打包/远程管理）
```

---

## 学习路线图

本仓库覆盖了语音 Agent 的完整链路。建议从现有代码出发，按以下路线由浅入深地理解 LLM 与 Agent 的核心概念。

### 第一层：LLM 基础

理解语言模型如何工作，基于现有 `llm_engine.cpp` / `chat_memory.cpp` 扩展。

| 主题 | 做什么 | 学到什么 |
|------|--------|---------|
| **Prompt Engineering** | system prompt 模板化，支持 `{time}` `{weather}` 变量注入，对比不同 prompt 的回复质量 | prompt 结构对模型输出的影响 |
| **Token 与上下文窗口** | 当前 `ChatMemory` 用字符数估算 token 数，改为调 Ollama `/api/tokens` 获取真实 token 计数 | token 是什么、context window 如何管理 |
| **Embedding 语义搜索** | 用 Ollama `/api/embed` 对文本做向量化，实现「语义相似度」替代关键字匹配 | 向量嵌入原理、余弦相似度 |
| **RAG 检索增强生成** | `embedding + 向量存储 + LLM` 完整链路：检索相关文档 → 拼入上下文 → LLM 生成答案 | RAG 架构、chunk 策略、召回率 |
| **结构化输出** | 给 Ollama 加 `format: "json"`，让 LLM 输出固定格式 JSON，解析后做实体提取/意图分类 | JSON mode、输出约束 |

### 第二层：Agent 演进

从关键字路由（当前实现）逐步演进到 LLM 自主决策的 Agent。

| 主题 | 做什么 | 学到什么 |
|------|--------|---------|
| **理解当前 Agent 架构** | 当前 `SkillManager` 就是 Agent 的核心循环：`感知(ASR) → 路由(match) → 工具(execute) → 推理(LLM) → 行动(TTS)`。对比它与 OpenAI function calling 的异同 | Agent 的本质定义 |
| **Function Calling** | 把 `Skill::match()` 替换为：发所有工具 schema 给 LLM，LLM 返回 `{tool: "weather", args: {city: "北京"}}`。需要切到更强模型（`qwen2.5:7b` 或云端 API） | 工具调用协议、LLM 决策 vs 规则匹配 |
| **ReAct 模式** | 实现 `思考 → 行动 → 观察 → 思考...` 多步推理循环。例如："先查天气 → 再查日历 → 综合建议今天穿什么" | 思维链、多步推理、工具编排 |
| **Reflection 反思** | LLM 生成回复后，再调一次 LLM 做自检："回复是否有事实错误？语气是否合适？" | 自校正、幻觉检测 |
| **Multi-Agent 对话** | 两个 Agent 互相对话：一个扮演用户提需求，一个扮演助手回复，观察涌现行为 | 多 Agent 协作、角色扮演 |

### 第三层：语音交互深入

| 主题 | 做什么 | 学到什么 |
|------|--------|---------|
| **流式 ASR** | 当前是离线 ASR（整段 WAV 一次性识别），改成 sherpa-onnx streaming API — 边说边出字 | 流式推理、音频分帧 |
| **情感识别** | SenseVoice 输出自带情感标签（`<|HAPPY|>` `<|ANGRY|>` 等），提取后影响 LLM 回复情绪 | 多模态特征利用 |
| **声纹库** | 当前只支持 1 个声纹注册，扩展为多用户身份管理 + "谁在说话"自动识别 | 说话人识别、embedding 比对 |
| **TTS 韵律控制** | 给 Piper 传 SSML 标签（语速/停顿/重音），让合成语音更有表现力 | TTS 前端处理、SSML |

### 第四层：系统工程

| 主题 | 做什么 | 学到什么 |
|------|--------|---------|
| **WebSocket 服务化** | 把 CLI 交互改成 WebSocket 服务，前端可以做成网页版语音助手 | 实时通信、服务端架构 |
| **本地模型嵌入** | 把 LLM 从远程 Ollama 换成 llama.cpp C API 直接嵌入进程，消除网络延迟 | 模型推理引擎、内存管理 |
| **可观测性** | 给每个模块加 metrics（LLM 耗时、ASR 置信度、TTS 延迟），输出结构化日志 | 生产级监控 |
| **配置热更新** | 不重启进程即可切换 LLM 模型、调整 VAD 阈值、开关技能 | 动态配置管理 |

### 推荐学习顺序

```
1. Prompt Engineering（1-2 天）       ← 理解 LLM 行为
2. Token 与上下文管理（1 天）          ← 理解模型输入限制
3. Embedding + RAG（2-3 天）          ← 理解语义检索
4. Function Calling（2-3 天）         ← Agent 的核心跃迁
5. ReAct 多步推理（2 天）              ← 真正的自主 Agent
6. Multi-Agent（1-2 天）              ← 涌现行为观察
```

每一步都在当前仓库基础上增量实现，不破坏已有功能。做完前 4 步你就完整理解了从「关键字路由的 Demo」到「LLM 自主决策的 Agent」的演进过程。
