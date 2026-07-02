"""
ASR-LLM-TTS 功能模块

每个模块对应一个 C++ 实现方向，接口设计参考 C++ 类的风格:
    - 构造函数接收配置 → init/initialize 加载模型 → 方法调用执行推理
    - 每个类独立，不相互依赖

模块映射:
    asr.py      →  sherpa-onnx SenseVoice C API
    llm.py      →  llama.cpp
    tts.py      →  sherpa-onnx Kokoro / espeak
    kws.py      →  C++ 拼音查找表 + 字符串匹配
    speaker.py  →  sherpa-onnx speaker verification
    memory.py   →  std::vector<pair<string,string>>
    audio_io.py →  硬件 SDK
    pipeline.py →  机器人主控类
    config.py   →  配置结构体
"""

from .config import PipelineConfig
from .asr import ASREngine
from .llm import LLMEngine
from .tts import TTSEngine
from .kws import WakeWordDetector
from .speaker import SpeakerVerifier
from .memory import ChatMemory
from .audio_io import AudioRecorder, AudioPlayer
from .pipeline import VoicePipeline
